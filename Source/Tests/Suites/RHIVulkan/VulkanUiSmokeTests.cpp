#include "Engine/Platform/PlatformSystem.h"
#include "Engine/Systems/Renderer/ForwardPlus/ForwardPlusBindings.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/VulkanRhiBackend.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderCommandContext.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraphExecutor.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraphTransfers.h"
#include "Tests/Fixtures/VulkanSmokeDiagnostics.h"
#include "Tests/Framework/Test.h"

// The UI smoke needs the SwimUiQuad artifacts and the text/UI module (compiled into
// SwimTests only when the text dependencies are, which also defines the font path).
#if defined(SWIM_UI_QUAD_SPIRV_PATH) && defined(SWIM_UI_QUAD_REFLECTION_PATH) && defined(SWIM_TEXT_FONT_FIXTURE_PATH) &&                   \
	defined(SWIM_TEXT_FALLBACK_FONT_FIXTURE_PATH)
#include "Engine/Systems/Renderer/UiRendering/UiRenderer.h"
#include "Tests/Fixtures/TextFontFixture.h"
#include "Tests/Fixtures/VulkanEnvironmentFixture.h"
#define SWIM_UI_SMOKE_AVAILABLE 1
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace
{
#ifdef SWIM_UI_SMOKE_AVAILABLE
	namespace Smoke = Swim::Testing::EnvironmentSmoke;
	namespace R = Swim::Render::Ui;

	double PassMilliseconds(const std::vector<Swim::Render::GraphPassTiming>& timings, std::string_view prefix)
	{
		double total = 0.0;
		double previousEnd = 0.0;
		for (const auto& timing : timings)
		{
			if (!timing.EndOffsetNanoseconds)
			{
				continue;
			}
			const double end = *timing.EndOffsetNanoseconds;
			if (timing.Name.starts_with(prefix))
			{
				total += std::max(end - previousEnd, 0.0) * 1.0e-6;
			}
			previousEnd = std::max(previousEnd, end);
		}
		return total;
	}

	struct UiProgram
	{
		std::unique_ptr<Swim::Rhi::ShaderProgram> Program;
		std::unique_ptr<Swim::Rhi::PipelineLayout> Layout;
		std::map<Swim::Rhi::Format, std::unique_ptr<Swim::Rhi::GraphicsPipeline>> Pipelines;

		Swim::Render::UiRenderProgram Get(Swim::Rhi::Format format) const { return { Pipelines.at(format).get(), Layout.get() }; }
	};

	UiProgram LoadUi(Swim::Rhi::Device& device, const Swim::Rhi::DescriptorSchemaDesc& bindlessSpace)
	{
		using namespace Swim;
		const auto reflected = Smoke::ReflectProgram(SWIM_UI_QUAD_REFLECTION_PATH);
		const auto bytes = Smoke::ReadSpirv(SWIM_UI_QUAD_SPIRV_PATH);
		const auto& draw = reflected.Interface;
		const std::array<Rhi::ShaderStageArtifact, 2> stages{ { { Rhi::ShaderStageMask::Vertex, "vertexMain", bytes },
			{ Rhi::ShaderStageMask::Fragment, "fragmentMain", bytes } } };
		UiProgram program;
		program.Program = device.CreateShaderProgram({ stages, { draw.DescriptorSchemas, draw.PushConstants }, "UI quads" });
		SWIM_REQUIRE(program.Program);
		program.Layout = device.CreatePipelineLayout({ program.Program.get(), "UI quads", { &bindlessSpace, 1 } });
		SWIM_REQUIRE(program.Layout);
		for (const auto format : { Rhi::Format::RGBA8Unorm, Rhi::Format::RGBA16Float, Rhi::Format::RGBA8UnormSrgb })
		{
			program.Pipelines[format] =
				device.CreateGraphicsPipeline(Render::UiRenderer::PipelineDesc(format, *program.Program, *program.Layout));
			SWIM_REQUIRE(program.Pipelines[format]);
		}
		return program;
	}

	// A 16 x 16 premultiplied RGBA8 frame for a nine-slice: an opaque 4-texel border
	// ring and a translucent checkered centre.
	constexpr std::uint32_t ImageSize = 16;

	std::vector<std::uint8_t> MakeImage()
	{
		std::vector<std::uint8_t> texels(ImageSize * ImageSize * 4);
		for (std::uint32_t y = 0; y < ImageSize; ++y)
		{
			for (std::uint32_t x = 0; x < ImageSize; ++x)
			{
				const bool border = x < 4 || y < 4 || x >= ImageSize - 4 || y >= ImageSize - 4;
				const bool checker = ((x / 2) + (y / 2)) % 2 == 0;
				const std::uint8_t alpha = border ? 255 : (checker ? 160 : 64);
				const float a = alpha / 255.0f;
				const std::array<float, 3> straight =
					border ? std::array<float, 3>{ 0.9f, 0.8f, 0.2f } : std::array<float, 3>{ 0.1f, 0.6f, 0.9f };
				auto* texel = &texels[(std::size_t(y) * ImageSize + x) * 4];
				for (int c = 0; c < 3; ++c)
				{
					texel[c] = std::uint8_t(std::lround(straight[c] * a * 255.0f));
				}
				texel[3] = alpha;
			}
		}
		return texels;
	}

	// The document under test: a rounded, bordered card with clipped text (Latin, Arabic,
	// fallback Greek/Hebrew), a nine-slice image, and a translucent overlay on top.
	struct Scene
	{
		Swim::UI::UiDocument Document;
		Swim::UI::UiNodeId Title;
		Swim::UI::UiNodeId Body;

		Scene(const std::shared_ptr<const Swim::Text::FontCollection>& fonts, std::uint32_t imageTexture, std::uint32_t imageSampler)
		{
			using namespace Swim::UI;
			auto& ui = Document;
			const auto card = ui.Create(ui.GetRoot());
			UiStyle cardStyle;
			cardStyle.Absolute = true;
			cardStyle.Offset = { 6, 6 };
			cardStyle.Width = UiLength::Pixels(150);
			cardStyle.Height = UiLength::Pixels(84);
			cardStyle.Padding = { 8, 6, 8, 6 };
			cardStyle.Background = { 0.8f, 0.25f, 0.15f, 0.9f };
			cardStyle.CornerRadius = 10.0f;
			cardStyle.BorderWidth = 2.0f;
			cardStyle.BorderColor = { 1.0f, 1.0f, 1.0f, 1.0f };
			cardStyle.Clip = true;
			cardStyle.Gap = 2.0f;
			ui.SetStyle(card, cardStyle);
			Title = ui.Create(card);
			ui.SetText(Title, fonts, "Swim UI 1.0 \xC3\xA9", 18.0f);
			Body = ui.Create(card);
			UiStyle bodyStyle;
			bodyStyle.TextColor = { 1.0f, 0.95f, 0.4f, 1.0f };
			bodyStyle.TextWrap = Swim::Text::TextWrap::Word;
			ui.SetStyle(Body, bodyStyle);
			ui.SetText(Body, fonts,
				"\xD8\xB3\xD9\x84\xD8\xA7\xD9\x85 \xCE\xA9\xCE\xBC\xCE\xAD\xCE\xB3\xCE\xB1 \xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D and a long "
				"line that "
				"wraps and is clipped by the card",
				13.0f);
			const auto frame = ui.Create(ui.GetRoot());
			UiStyle frameStyle;
			frameStyle.Absolute = true;
			frameStyle.Offset = { 162, 6 };
			frameStyle.Width = UiLength::Pixels(40);
			frameStyle.Height = UiLength::Pixels(56);
			ui.SetStyle(frame, frameStyle);
			UiImage image;
			image.Texture = imageTexture;
			image.Sampler = imageSampler;
			image.Size = { 16, 16 };
			image.Slice = { 5, 5, 5, 5 };
			image.SliceUv = { 0.25f, 0.25f, 0.25f, 0.25f };
			ui.SetImage(frame, image);
			const auto overlay = ui.Create(ui.GetRoot());
			UiStyle overlayStyle;
			overlayStyle.Absolute = true;
			overlayStyle.Offset = { 120, 48 };
			overlayStyle.Width = UiLength::Pixels(70);
			overlayStyle.Height = UiLength::Pixels(40);
			overlayStyle.Background = { 0.2f, 0.4f, 1.0f, 0.5f };
			overlayStyle.CornerRadius = 6.0f;
			ui.SetStyle(overlay, overlayStyle);
		}
	};

	// Pixels whose centre lies within 1/64 px of a clipped quad edge may round either way
	// on hardware; they are excluded from the comparison.
	std::vector<bool> AmbiguousPixels(std::span<const Swim::Render::GpuUiQuad> quads, std::uint32_t width, std::uint32_t height)
	{
		std::vector<bool> ambiguous(std::size_t(width) * height, false);
		const auto closeTo = [](float centre, float edge)
		{
			return std::abs(centre - edge) < 1.0f / 64.0f;
		};
		for (const auto& quad : quads)
		{
			const float x0 = std::max(quad.Rect[0], quad.Clip[0]);
			const float y0 = std::max(quad.Rect[1], quad.Clip[1]);
			const float x1 = std::min(quad.Rect[2], quad.Clip[2]);
			const float y1 = std::min(quad.Rect[3], quad.Clip[3]);
			for (std::uint32_t y = 0; y < height; ++y)
			{
				for (std::uint32_t x = 0; x < width; ++x)
				{
					const float cx = float(x) + 0.5f;
					const float cy = float(y) + 0.5f;
					const bool inX = cx > x0 - 0.1f && cx < x1 + 0.1f;
					const bool inY = cy > y0 - 0.1f && cy < y1 + 0.1f;
					if ((inY && (closeTo(cx, x0) || closeTo(cx, x1))) || (inX && (closeTo(cy, y0) || closeTo(cy, y1))))
					{
						ambiguous[std::size_t(y) * width + x] = true;
					}
				}
			}
		}
		return ambiguous;
	}

	struct Comparison
	{
		std::uint32_t Compared = 0;
		std::uint32_t Lit = 0;
		std::uint32_t Outliers = 0;
		float Worst = 0.0f;
	};

	// Stored texels (already decoded to floats) against the CPU canvas.
	Comparison Compare(const std::vector<R::Float4>& gpu, const R::Canvas& cpu, const std::vector<bool>& ambiguous, const R::Float4& clear,
		float tolerance, float unit, bool srgbStore)
	{
		Comparison result;
		for (std::size_t i = 0; i < gpu.size(); ++i)
		{
			if (ambiguous[i])
			{
				continue;
			}
			++result.Compared;
			bool outlier = false;
			bool lit = false;
			for (int c = 0; c < 4; ++c)
			{
				float expected = cpu.Texels[i][c];
				if (srgbStore && c < 3)
				{
					expected = R::SrgbOetf(std::clamp(expected, 0.0f, 1.0f));
				}
				// In units of UI white (scRGB white is paper white / 80), relative above it.
				const float error = std::abs(gpu[i][c] - expected) / std::max(unit, std::abs(expected));
				result.Worst = std::max(result.Worst, error);
				outlier = outlier || error > tolerance;
				lit = lit || std::abs(expected - clear[c]) > 0.02f;
			}
			result.Outliers += outlier ? 1u : 0u;
			result.Lit += lit ? 1u : 0u;
		}
		return result;
	}
#endif

	// Critical-path item 79 on a real device: a retained UI document with rounded and
	// bordered solids, clipped paragraph text (bidi, fallback), a nine-slice image and a
	// translucent overlay is drawn by UiRenderer in one instanced draw, into SDR (sRGB
	// encoded), HDR10 (PQ), scRGB and hardware-sRGB (linear) targets. Every frame is read
	// back and compared texel by texel with Ui::Rasterize over the same instances. The
	// atlas then grows (a partial page upload), is released through the timeline and
	// replaced; a 1080p text wall times the pass.
	void RunUiSmoke(const Swim::Rhi::GraphicsSystemDesc& graphicsDesc)
	{
#ifndef SWIM_UI_SMOKE_AVAILABLE
		SWIM_REQUIRE_MESSAGE(false, "UI smoke requires generated Slang artifacts and the text/UI module");
#else
		using namespace Swim;
		using namespace Swim::Render;

		Platform::PlatformSystem platform;
		SWIM_REQUIRE_MESSAGE(platform.Initialize(), "UI smoke requires working SDL Vulkan platform services");
		auto graphics = RhiVulkan::CreateGraphicsSystem(graphicsDesc);
		SWIM_REQUIRE(graphics);
		Testing::RequireVulkanSmokeValidation(*graphics, graphicsDesc.Checks);
		SWIM_REQUIRE_MESSAGE(
			graphics->GetAdapter(0).GetInfo().Capabilities.BindlessDescriptors, "Adapter lacks bindless descriptor support");
		auto device = graphics->GetAdapter(0).CreateDevice();
		SWIM_REQUIRE(device);
		const auto bindlessSpace = ForwardPlusBindlessSpace(16, 4); // The space shared with Forward+ and particles.
		const auto program = LoadUi(*device, bindlessSpace);
		RenderGraphExecutor executor(*device);

		// Bindless: a white fallback, the nine-slice image and a linear clamp sampler.
		const auto makeTexture =
			[&](std::uint32_t width, std::uint32_t height, Rhi::Format format, Rhi::TextureUsage usage, const char* name)
		{
			Rhi::TextureDesc desc;
			desc.Extent = { width, height, 1 };
			desc.PixelFormat = format;
			desc.Usage = usage;
			desc.DebugName = name;
			auto texture = device->CreateTexture(desc);
			SWIM_REQUIRE(texture);
			return texture;
		};
		const auto sampledUsage = Rhi::TextureUsage::Sampled | Rhi::TextureUsage::TransferDestination;
		auto white = makeTexture(1, 1, Rhi::Format::RGBA8Unorm, sampledUsage, "UI fallback");
		auto image = makeTexture(ImageSize, ImageSize, Rhi::Format::RGBA8Unorm, sampledUsage, "UI nine-slice");
		Rhi::TextureViewDesc viewDesc;
		viewDesc.PixelFormat = Rhi::Format::RGBA8Unorm;
		auto whiteView = device->CreateTextureView(*white, viewDesc);
		auto imageView = device->CreateTextureView(*image, viewDesc);
		Rhi::SamplerDesc samplerDesc{};
		samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = Rhi::SamplerAddressMode::ClampToEdge;
		samplerDesc.MipFilter = Rhi::Filter::Nearest;
		samplerDesc.MaxLod = 0.0f;
		auto sampler = device->CreateSampler(samplerDesc);
		SWIM_REQUIRE(whiteView && imageView && sampler);
		const auto imageTexels = MakeImage();
		{
			RenderGraph uploads;
			const auto whiteTexture = uploads.ImportTexture(*white, Rhi::ResourceState::Undefined);
			const auto imageTexture = uploads.ImportTexture(*image, Rhi::ResourceState::Undefined);
			const std::array<std::uint8_t, 4> opaque{ 255, 255, 255, 255 };
			AddTextureUpload(uploads, "White upload", std::as_bytes(std::span(opaque)), whiteTexture, { 0, {}, {}, { 1, 1, 1 } });
			AddTextureUpload(
				uploads, "Image upload", std::as_bytes(std::span(imageTexels)), imageTexture, { 0, {}, {}, { ImageSize, ImageSize, 1 } });
			uploads.Export(whiteTexture, Rhi::ResourceState::ShaderRead);
			uploads.Export(imageTexture, Rhi::ResourceState::ShaderRead);
			executor.Execute(uploads.Compile());
			executor.Wait();
		}
		BindlessTableDesc bindlessDesc;
		bindlessDesc.Layout = program.Layout.get();
		bindlessDesc.Space = UiRenderBindings::BindlessSpace;
		bindlessDesc.FallbackTexture = whiteView.get();
		bindlessDesc.FallbackSampler = sampler.get();
		BindlessResourceTable bindless(*device, bindlessDesc);
		const auto imageHandle = bindless.RegisterTexture(*imageView);
		const auto samplerHandle = bindless.RegisterSampler(*sampler);

		auto fonts = Testing::LoadTextFontChain();
		Scene scene(fonts, bindless.GetIndex(imageHandle), bindless.GetIndex(samplerHandle));
		auto atlas = std::make_unique<Text::GlyphAtlas>();
		UiAtlasTextures atlasTextures(*device, bindless);
		UiRenderer renderer;

		constexpr std::uint32_t Width = 256;
		constexpr std::uint32_t Height = 128;
		constexpr float Dpi = 1.25f;

		// The CPU sampler: atlas pages as uploaded (RGB + opaque alpha) and the image.
		const auto makeSampler = [&](const UiAtlasFrame& frame)
		{
			std::map<std::uint32_t, std::vector<std::uint8_t>> pages;
			for (std::uint32_t page = 0; page < frame.Pages.size(); ++page)
			{
				const auto view = atlas->GetPage(page);
				auto& rgba = pages[frame.TextureIndices[page]];
				rgba.resize(std::size_t(view.Size) * view.Size * 4);
				for (std::size_t i = 0; i < std::size_t(view.Size) * view.Size; ++i)
				{
					rgba[i * 4 + 0] = view.Pixels[i * 3 + 0];
					rgba[i * 4 + 1] = view.Pixels[i * 3 + 1];
					rgba[i * 4 + 2] = view.Pixels[i * 3 + 2];
					rgba[i * 4 + 3] = 255;
				}
			}
			const std::uint32_t pageSize = frame.PageSize;
			const std::uint32_t imageIndex = bindless.GetIndex(imageHandle);
			return [pages = std::move(pages), pageSize, imageIndex, &imageTexels](std::uint32_t texture, std::uint32_t, float u, float v)
			{
				if (texture == imageIndex)
				{
					return R::SampleBilinear(imageTexels, ImageSize, ImageSize, u, v);
				}
				const auto found = pages.find(texture);
				return found == pages.end() ? R::Float4{ 1, 1, 1, 1 } : R::SampleBilinear(found->second, pageSize, pageSize, u, v);
			};
		};

		struct Target
		{
			Rhi::Format Format;
			UiCompositionSettings Composition;
			R::Float4 Clear;
			const char* Name;
		};

		UiCompositionSettings sdr;
		UiCompositionSettings hdr10;
		hdr10.Encoding = UiOutputEncoding::Hdr10;
		hdr10.PaperWhiteNits = 203.0f;
		UiCompositionSettings scrgb;
		scrgb.Encoding = UiOutputEncoding::ScRgb;
		scrgb.PaperWhiteNits = 240.0f;
		UiCompositionSettings linear;
		linear.Encoding = UiOutputEncoding::Linear;
		const std::array<Target, 5> targets{ {
			{ Rhi::Format::RGBA8Unorm, sdr, { 0.05f, 0.06f, 0.1f, 1.0f }, "sdr" },
			{ Rhi::Format::RGBA16Float, hdr10, { 0.1f, 0.1f, 0.1f, 1.0f }, "hdr10" },
			{ Rhi::Format::RGBA16Float, scrgb, { 0.2f, 0.1f, 0.0f, 1.0f }, "scrgb" },
			{ Rhi::Format::RGBA8UnormSrgb, linear, { 0.01f, 0.02f, 0.03f, 1.0f }, "linear-srgb" },
			{ Rhi::Format::RGBA8Unorm, sdr, { 0.05f, 0.06f, 0.1f, 1.0f }, "sdr-grown" },
		} };
		std::uint32_t totalCompared = 0;
		std::uint32_t totalOutliers = 0;
		std::optional<Rhi::TimelinePoint> lastUse;
		for (std::size_t t = 0; t < targets.size(); ++t)
		{
			const auto& target = targets[t];
			if (t == 4)
			{
				// New glyphs land on the existing page: a partial row-band upload.
				scene.Document.SetText(scene.Title, fonts, "Grown: xyzQWJK 42%", 18.0f);
			}
			scene.Document.Layout({ float(Width), float(Height) }, Dpi);
			const auto& paint = scene.Document.Paint(*atlas);
			RenderGraph graph;
			const auto atlasFrame = atlasTextures.Update(graph, *atlas);
			if (t == 0)
			{
				SWIM_CHECK_EQUAL(atlasFrame.UploadedRows, atlas->GetDesc().PageSize); // The initializing upload.
			}
			if (t == 4)
			{
				SWIM_CHECK(atlasFrame.UploadedRows > 0u && atlasFrame.UploadedRows < atlas->GetDesc().PageSize);
			}
			if (t > 0 && t < 4)
			{
				SWIM_CHECK_EQUAL(atlasFrame.UploadedPages, 0u); // Nothing new: nothing uploaded.
			}
			const auto sampledImage = graph.ImportTexture(*image, Rhi::ResourceState::ShaderRead);
			Rhi::TextureDesc colorDesc;
			colorDesc.Extent = { Width, Height, 1 };
			colorDesc.PixelFormat = target.Format;
			colorDesc.Usage = Rhi::TextureUsage::ColorAttachment | Rhi::TextureUsage::TransferSource;
			colorDesc.DebugName = "UI target";
			const auto color = graph.CreateTexture(colorDesc);
			UiRenderFrame frame;
			frame.Paint = paint;
			frame.Target = color;
			frame.DpiScale = Dpi;
			frame.Composition = target.Composition;
			frame.Atlas = &atlasFrame;
			frame.Images = std::span(&sampledImage, 1);
			frame.Clear = true;
			// Clear values are linear; an *Srgb attachment stores them encoded.
			frame.ClearColor = { target.Clear[0], target.Clear[1], target.Clear[2], target.Clear[3] };
			SWIM_REQUIRE(renderer.Record(graph, frame, program.Get(target.Format), bindless.GetTable()).has_value());
			const auto readback = AddTextureReadback(graph, "UI readback", color, { 0, {}, {}, { Width, Height, 1 } });
			lastUse = executor.Execute(graph.Compile());
			atlasTextures.CommitFrame();
			executor.Wait();

			const auto& quads = renderer.GetLastQuads();
			SWIM_CHECK(renderer.GetStats().Glyphs > 20u && renderer.GetStats().Images == 9u && renderer.GetStats().Solids >= 2u);
			std::vector<R::Float4> gpu(std::size_t(Width) * Height);
			if (target.Format == Rhi::Format::RGBA16Float)
			{
				std::vector<std::uint16_t> halves(gpu.size() * 4);
				SWIM_REQUIRE(
					executor.TryReadback(readback.Buffer, std::as_writable_bytes(std::span(halves))) == Rhi::ReadbackStatus::Ready);
				for (std::size_t i = 0; i < gpu.size(); ++i)
				{
					for (int c = 0; c < 4; ++c)
					{
						gpu[i][c] = Smoke::HalfToFloat(halves[i * 4 + c]);
					}
				}
			}
			else
			{
				std::vector<std::uint8_t> bytes(gpu.size() * 4);
				SWIM_REQUIRE(executor.TryReadback(readback.Buffer, std::as_writable_bytes(std::span(bytes))) == Rhi::ReadbackStatus::Ready);
				for (std::size_t i = 0; i < gpu.size(); ++i)
				{
					for (int c = 0; c < 4; ++c)
					{
						gpu[i][c] = bytes[i * 4 + c] / 255.0f;
					}
				}
			}
			R::Canvas canvas{ Width, Height, std::vector<R::Float4>(gpu.size(), target.Clear) };
			R::Rasterize(canvas, quads, renderer.GetLastConstants(), makeSampler(atlasFrame));
			const auto ambiguous = AmbiguousPixels(quads, Width, Height);
			const bool srgbStore = target.Format == Rhi::Format::RGBA8UnormSrgb;
			// 2.5 8-bit LSB of UI white for every encoding (sRGB stores quantize after a
			// linear blend; MSDF coverage filtered with the hardware's subtexel precision
			// dominates on float targets). Larger edge differences: at most 1 % of the
			// compared pixels.
			const float tolerance = 2.5f / 255.0f;
			const float unit = target.Composition.Encoding == UiOutputEncoding::ScRgb ? renderer.GetLastConstants().WhiteScale : 1.0f;
			R::Float4 clearStored = target.Clear;
			if (srgbStore)
			{
				for (int c = 0; c < 3; ++c)
				{
					clearStored[c] = R::SrgbOetf(target.Clear[c]);
				}
			}
			const auto result = Compare(gpu, canvas, ambiguous, clearStored, tolerance, unit, srgbStore);
			std::printf("             [ui %s] %zu quads (%u glyphs): %u pixels compared, %u lit, %u outliers (worst %.2e)\n", target.Name,
				quads.size(), renderer.GetStats().Glyphs, result.Compared, result.Lit, result.Outliers, double(result.Worst));
			SWIM_CHECK(result.Lit > result.Compared / 10);
			SWIM_CHECK(result.Outliers <= result.Compared / 100);
			totalCompared += result.Compared;
			totalOutliers += result.Outliers;
		}
		std::printf("             [ui] %u pixels compared over %zu frames, %u outliers\n", totalCompared, targets.size(), totalOutliers);

		// Timeline-safe retirement: the atlas pages retire after the last frame that
		// sampled them; a replacement atlas starts over with fresh pages.
		SWIM_REQUIRE(lastUse.has_value());
		atlasTextures.Release(*lastUse);
		SWIM_CHECK_EQUAL(atlasTextures.GetStats().RetiringPages, 1u);
		executor.Wait();
		SWIM_CHECK_EQUAL(atlasTextures.Collect(), 1u);
		SWIM_CHECK_EQUAL(atlasTextures.GetStats().RetiringPages, 0u);
		atlas = std::make_unique<Text::GlyphAtlas>();
		{
			scene.Document.Layout({ float(Width), float(Height) }, Dpi);
			const auto& paint = scene.Document.Paint(*atlas); // A new atlas repaints every node.
			RenderGraph graph;
			const auto atlasFrame = atlasTextures.Update(graph, *atlas);
			SWIM_CHECK_EQUAL(atlasFrame.UploadedRows, atlas->GetDesc().PageSize);
			const auto sampledImage = graph.ImportTexture(*image, Rhi::ResourceState::ShaderRead);
			Rhi::TextureDesc colorDesc;
			colorDesc.Extent = { Width, Height, 1 };
			colorDesc.PixelFormat = Rhi::Format::RGBA8Unorm;
			colorDesc.Usage = Rhi::TextureUsage::ColorAttachment | Rhi::TextureUsage::Sampled;
			const auto color = graph.CreateTexture(colorDesc);
			UiRenderFrame frame;
			frame.Paint = paint;
			frame.Target = color;
			frame.DpiScale = Dpi;
			frame.Atlas = &atlasFrame;
			frame.Images = std::span(&sampledImage, 1);
			frame.Clear = true;
			SWIM_REQUIRE(renderer.Record(graph, frame, program.Get(Rhi::Format::RGBA8Unorm), bindless.GetTable()).has_value());
			graph.Export(color, Rhi::ResourceState::ShaderRead);
			executor.Execute(graph.Compile());
			atlasTextures.CommitFrame();
			executor.Wait();
		}

		// Timing: a 1080p text wall at DPI 1.5 (thousands of glyph instances, one draw).
		{
			UI::UiDocument wall;
			UI::UiStyle column;
			column.Padding = { 12, 12, 12, 12 };
			wall.SetStyle(wall.GetRoot(), column);
			const auto paragraph = wall.Create(wall.GetRoot());
			UI::UiStyle wrapped;
			wrapped.TextWrap = Text::TextWrap::Word;
			wall.SetStyle(paragraph, wrapped);
			std::string text;
			for (int i = 0; i < 90; ++i)
			{
				text += "The quick brown fox jumps over the lazy dog; 0123456789. ";
			}
			wall.SetText(paragraph, fonts, text, 14.0f);
			wall.Layout({ 1920, 1080 }, 1.5f);
			const auto& paint = wall.Paint(*atlas);
			for (int frameIndex = 0; frameIndex < 4; ++frameIndex)
			{
				RenderGraph graph;
				const auto atlasFrame = atlasTextures.Update(graph, *atlas);
				Rhi::TextureDesc colorDesc;
				colorDesc.Extent = { 1920, 1080, 1 };
				colorDesc.PixelFormat = Rhi::Format::RGBA8Unorm;
				colorDesc.Usage = Rhi::TextureUsage::ColorAttachment | Rhi::TextureUsage::Sampled;
				const auto color = graph.CreateTexture(colorDesc);
				UiRenderFrame frame;
				frame.Paint = paint;
				frame.Target = color;
				frame.DpiScale = 1.5f;
				frame.Atlas = &atlasFrame;
				frame.Clear = true;
				SWIM_REQUIRE(renderer.Record(graph, frame, program.Get(Rhi::Format::RGBA8Unorm), bindless.GetTable()).has_value());
				graph.Export(color, Rhi::ResourceState::ShaderRead);
				executor.Execute(graph.Compile());
				atlasTextures.CommitFrame();
				executor.Wait();
				if (frameIndex == 3)
				{
					std::printf("             [ui 1080p] %u glyph instances: draw %.3f ms\n", renderer.GetStats().Glyphs,
						PassMilliseconds(executor.ReadTimings(), "UI draw"));
					SWIM_CHECK(renderer.GetStats().Glyphs > 3000u);
				}
			}
		}
		executor.Wait();
		atlasTextures.Release({});
		atlasTextures.Collect();
		bindless.Drain();
		executor.Trim();
#endif
	}

	[[maybe_unused]] const bool registered = []
	{
		const char* enabled = std::getenv("SWIM_RUN_RHI_SMOKE");
		if (enabled != nullptr && std::string_view(enabled) == "1")
		{
			Swim::Testing::TestRegistry::Get().Add({ "RHI.Vulkan.Smoke", "UiRendererMatchesTheCpuReference", SWIM_TEST_LOCATION,
				+[]
				{
					Swim::Testing::RunValidatedVulkanSmoke(&RunUiSmoke);
				} });
		}
		return true;
	}();
} // namespace
