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
#include "Engine/Systems/Renderer/UiRendering/UiRenderSurfaces.h"
#include "Engine/Systems/UI/UiCanvas.h"
#include "Engine/Systems/UI/UiWidgets.h"
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
		std::map<Swim::Rhi::Format, std::unique_ptr<Swim::Rhi::GraphicsPipeline>> DepthPipelines; // D32Float scene depth.

		Swim::Render::UiRenderProgram Get(Swim::Rhi::Format format) const
		{
			const auto depth = DepthPipelines.find(format);
			return { Pipelines.at(format).get(), Layout.get(), depth == DepthPipelines.end() ? nullptr : depth->second.get() };
		}
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
		program.DepthPipelines[Rhi::Format::RGBA16Float] = device.CreateGraphicsPipeline(
			Render::UiRenderer::PipelineDesc(Rhi::Format::RGBA16Float, *program.Program, *program.Layout, Rhi::Format::D32Float));
		SWIM_REQUIRE(program.DepthPipelines[Rhi::Format::RGBA16Float]);
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

#ifdef SWIM_UI_SMOKE_AVAILABLE
	float SrgbEotf(float encoded)
	{
		return encoded <= 0.04045f ? encoded / 12.92f : std::pow((encoded + 0.055f) / 1.055f, 2.4f);
	}

	// Pixels whose canvas point lies within 1/50 of a pixel footprint of a clipped quad
	// edge (or that see no canvas) may round either way on hardware.
	std::vector<bool> AmbiguousProjected(std::span<const Swim::Render::GpuUiQuad> quads, const Swim::Render::GpuUiDrawConstants& constants,
		std::uint32_t width, std::uint32_t height)
	{
		std::vector<bool> ambiguous(std::size_t(width) * height, false);
		for (std::uint32_t y = 0; y < height; ++y)
		{
			for (std::uint32_t x = 0; x < width; ++x)
			{
				const auto sample = R::CanvasAt(constants, float(x) + 0.5f, float(y) + 0.5f);
				auto flag = ambiguous[std::size_t(y) * width + x]; // A std::vector<bool> proxy.
				if (!sample)
				{
					flag = true;
					continue;
				}
				const float ex = 0.02f * sample->FootprintX;
				const float ey = 0.02f * sample->FootprintY;
				for (const auto& quad : quads)
				{
					const float x0 = std::max(quad.Rect[0], quad.Clip[0]);
					const float y0 = std::max(quad.Rect[1], quad.Clip[1]);
					const float x1 = std::min(quad.Rect[2], quad.Clip[2]);
					const float y1 = std::min(quad.Rect[3], quad.Clip[3]);
					const bool inX = sample->X > x0 - ex && sample->X < x1 + ex;
					const bool inY = sample->Y > y0 - ey && sample->Y < y1 + ey;
					if ((inY && (std::abs(sample->X - x0) < ex || std::abs(sample->X - x1) < ex)) ||
						(inX && (std::abs(sample->Y - y0) < ey || std::abs(sample->Y - y1) < ey)))
					{
						flag = true;
						break;
					}
				}
			}
		}
		return ambiguous;
	}
#endif

	// Critical-path item 79, world canvases on a real device: the smoke document drawn as
	// a world panel into an HDR scene target through orthographic (1:1) and perspective
	// cameras (a rotated panel and an off-axis billboard), depth tested against scene
	// depth (visible, then fully occluded), faded by canvas opacity, and as a render
	// surface with a re-rasterized mip chain that a world panel then samples. Every image
	// is read back and compared with Ui::RasterizeProjected / Ui::Rasterize.
	void RunUiWorldSmoke(const Swim::Rhi::GraphicsSystemDesc& graphicsDesc)
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
		const auto bindlessSpace = ForwardPlusBindlessSpace(16, 4);
		const auto program = LoadUi(*device, bindlessSpace);
		RenderGraphExecutor executor(*device);

		Rhi::TextureDesc whiteDesc;
		whiteDesc.Extent = { 1, 1, 1 };
		whiteDesc.PixelFormat = Rhi::Format::RGBA8Unorm;
		whiteDesc.Usage = Rhi::TextureUsage::Sampled | Rhi::TextureUsage::TransferDestination;
		auto white = device->CreateTexture(whiteDesc);
		auto imageDesc = whiteDesc;
		imageDesc.Extent = { ImageSize, ImageSize, 1 };
		auto image = device->CreateTexture(imageDesc);
		SWIM_REQUIRE(white && image);
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
		const std::uint32_t imageIndex = bindless.GetIndex(imageHandle);

		auto fonts = Testing::LoadTextFontChain();
		Scene scene(fonts, imageIndex, bindless.GetIndex(samplerHandle));
		Text::GlyphAtlas atlas;
		UiAtlasTextures atlasTextures(*device, bindless);
		UiRenderer renderer;
		constexpr std::uint32_t CanvasWidth = 256;
		constexpr std::uint32_t CanvasHeight = 128;
		constexpr float Dpi = 1.25f;
		scene.Document.Layout({ float(CanvasWidth), float(CanvasHeight) }, Dpi);
		const auto& paint = scene.Document.Paint(atlas);
		// Themed controls in their states: a checked checkbox, a hovered toggle, a focused
		// half-way slider, a button and a scroll area with its bar.
		UI::UiDocument widgets;
		constexpr float WidgetDpi = 0.85f;
		{
			auto theme = std::make_shared<UI::UiTheme>();
			theme->Fonts = fonts;
			widgets.SetTheme(theme);
			UI::UiStyle row;
			row.Flow = UI::UiFlow::Row;
			row.Gap = 8.0f;
			row.Padding = { 4, 4, 4, 4 };
			widgets.SetStyle(widgets.GetRoot(), row);
			const auto panel = UI::CreatePanel(widgets, widgets.GetRoot());
			const auto box = UI::CreateCheckbox(widgets, panel, "Subtitles", UI::UiCheckState::Checked);
			const auto toggle = UI::CreateToggle(widgets, panel, "V-sync", true);
			const auto slider =
				UI::CreateSlider(widgets, panel, { .Min = 0.0f, .Max = 1.0f, .Value = 0.5f, .Ticks = 5, .ShowValue = true, .Decimals = 2 });
			UI::CreateButton(widgets, panel, "Apply");
			UI::UiStyle areaStyle;
			areaStyle.Width = UI::UiLength::Pixels(60);
			areaStyle.Height = UI::UiLength::Pixels(60);
			const auto area =
				UI::CreateScrollArea(widgets, widgets.GetRoot(), areaStyle, true, false, UI::UiScrollBarVisibility::Auto, true);
			UI::CreateLabel(widgets, area.Viewport, "One\nTwo\nThree\nFour");
			widgets.Layout({ float(CanvasWidth), float(CanvasHeight) }, WidgetDpi);
			widgets.Focus(slider);
			const auto knob = widgets.GetBounds(widgets.GetControl(toggle).Parts.Track);
			widgets.PointerMove({ (knob.X + 2.0f) * WidgetDpi, (knob.Y + 2.0f) * WidgetDpi }); // Framebuffer pixels.
			SWIM_CHECK(widgets.GetChecked(box) == UI::UiCheckState::Checked);
			widgets.Update(0.0f);
		}
		const auto& widgetPaint = widgets.Paint(atlas);

		const auto cpuSampler = [&](const UiAtlasFrame& frame)
		{
			std::map<std::uint32_t, std::vector<std::uint8_t>> pages;
			for (std::uint32_t page = 0; page < frame.Pages.size(); ++page)
			{
				const auto view = atlas.GetPage(page);
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
		const auto readColors = [&](const GraphReadback& readback, Rhi::Format format, std::uint32_t width, std::uint32_t height)
		{
			std::vector<R::Float4> texels(std::size_t(width) * height);
			if (format == Rhi::Format::RGBA16Float)
			{
				std::vector<std::uint16_t> halves(texels.size() * 4);
				SWIM_REQUIRE(
					executor.TryReadback(readback.Buffer, std::as_writable_bytes(std::span(halves))) == Rhi::ReadbackStatus::Ready);
				for (std::size_t i = 0; i < texels.size(); ++i)
				{
					for (int c = 0; c < 4; ++c)
					{
						texels[i][c] = Smoke::HalfToFloat(halves[i * 4 + c]);
					}
				}
				return texels;
			}
			std::vector<std::uint8_t> bytes(texels.size() * 4);
			SWIM_REQUIRE(executor.TryReadback(readback.Buffer, std::as_writable_bytes(std::span(bytes))) == Rhi::ReadbackStatus::Ready);
			for (std::size_t i = 0; i < texels.size(); ++i)
			{
				for (int c = 0; c < 4; ++c)
				{
					texels[i][c] = bytes[i * 4 + c] / 255.0f;
				}
			}
			return texels;
		};

		// The scene: HDR color and reverse-Z depth, cleared by a scene pass.
		constexpr std::uint32_t Width = 256;
		constexpr std::uint32_t Height = 160;
		const R::Float4 clear{ 0.02f, 0.03f, 0.05f, 1.0f };
		UiCompositionSettings hdrScene;
		hdrScene.Encoding = UiOutputEncoding::Linear;
		hdrScene.LinearScale = 1.5f; // UI white at 1.5 x the scene's SDR white.
		const auto aspect = float(Width) / float(Height);
		const float fovY = 1.0471976f; // 60 degrees.
		const float focal = 1.0f / std::tan(fovY * 0.5f);
		const UI::UiMatrix4 perspective{ focal / aspect, 0, 0, 0, 0, focal, 0, 0, 0, 0, 0, 0.05f, 0, 0, -1, 0 };
		// Orthographic: world [0, 2.56] x [-1.44, 0.16] -> the viewport, depth 0.45 at z = 0.
		const UI::UiMatrix4 orthographic{ 1.0f / 1.28f, 0, 0, 0, 0, 1.0f / 0.8f, 0, 0, 0, 0, -0.01f, 0.4f, 0, 0, 0, 1 };
		const auto cameraAt = [&](UI::UiVec3 position, const UI::UiMatrix4& projection)
		{
			UI::UiCameraView camera;
			camera.View = { 1, 0, 0, -position.X, 0, 1, 0, -position.Y, 0, 0, 1, -position.Z, 0, 0, 0, 1 };
			camera.Projection = projection;
			camera.ViewportWidth = float(Width);
			camera.ViewportHeight = float(Height);
			return camera;
		};
		UI::UiWorldPlacement topLeft;
		topLeft.Pivot = { 0.0f, 0.0f };
		topLeft.UnitsPerPixel = 0.01f;
		const UI::UiPoint canvasSize{ float(CanvasWidth), float(CanvasHeight) };

		struct WorldCase
		{
			const char* Name;
			std::span<const UI::UiPaintQuad> Paint;
			float Dpi;
			UI::UiCanvasMode Mode;
			UI::UiWorldPlacement Placement;
			UI::UiCameraView Camera;
			float DepthClear;
			float Opacity;
			bool Occluded;
		};

		auto rotated = topLeft;
		rotated.Pivot = { 0.5f, 0.5f };
		rotated.Transform = { std::cos(0.45f), 0, std::sin(0.45f), 1.28f, 0, 1, 0, -0.64f, -std::sin(0.45f), 0, std::cos(0.45f), 0 };
		auto billboard = rotated;
		billboard.Transform = { 1, 0, 0, 1.28f, 0, 1, 0, -0.64f, 0, 0, 1, 0 };
		const auto ortho = cameraAt({ 1.28f, -0.64f, 5.0f }, orthographic);
		const std::array<WorldCase, 7> cases{ {
			{ "world-ortho", paint, Dpi, UI::UiCanvasMode::WorldPanel, topLeft, ortho, 0.0f, 1.0f, false },
			{ "world-faded", paint, Dpi, UI::UiCanvasMode::WorldPanel, topLeft, ortho, 0.0f, 0.5f, false },
			{ "world-occluded", paint, Dpi, UI::UiCanvasMode::WorldPanel, topLeft, ortho, 1.0f, 1.0f, true },
			{ "world-rotated", paint, Dpi, UI::UiCanvasMode::WorldPanel, rotated, cameraAt({ 1.28f, -0.64f, 2.6f }, perspective), 0.0f,
				1.0f, false },
			{ "world-billboard", paint, Dpi, UI::UiCanvasMode::Billboard, billboard, cameraAt({ 2.4f, -0.2f, 2.8f }, perspective), 0.0f,
				1.0f, false },
			{ "world-widgets", widgetPaint, WidgetDpi, UI::UiCanvasMode::WorldPanel, topLeft, ortho, 0.0f, 1.0f, false },
			{ "world-widgets-billboard", widgetPaint, WidgetDpi, UI::UiCanvasMode::Billboard, billboard,
				cameraAt({ 2.4f, -0.2f, 2.8f }, perspective), 0.0f, 1.0f, false },
		} };
		std::uint32_t totalCompared = 0;
		std::uint32_t totalOutliers = 0;
		for (const auto& test : cases)
		{
			const auto toWorld = UI::CanvasToWorld(test.Mode, test.Placement, canvasSize, &test.Camera);
			const auto clip = UI::ClipFromCanvas(toWorld, test.Camera);
			RenderGraph graph;
			const auto atlasFrame = atlasTextures.Update(graph, atlas);
			const auto sampledImage = graph.ImportTexture(*image, Rhi::ResourceState::ShaderRead);
			Rhi::TextureDesc colorDesc;
			colorDesc.Extent = { Width, Height, 1 };
			colorDesc.PixelFormat = Rhi::Format::RGBA16Float;
			colorDesc.Usage = Rhi::TextureUsage::ColorAttachment | Rhi::TextureUsage::TransferSource;
			const auto color = graph.CreateTexture(colorDesc);
			Rhi::TextureDesc depthDesc;
			depthDesc.Extent = { Width, Height, 1 };
			depthDesc.PixelFormat = Rhi::Format::D32Float;
			depthDesc.Usage = Rhi::TextureUsage::DepthStencilAttachment;
			const auto depth = graph.CreateTexture(depthDesc);
			const float depthClear = test.DepthClear;
			graph.AddPass(
				"Scene clear", Rhi::QueueType::Graphics,
				[&](RenderGraphBuilder& b)
				{
					b.Write(color, Rhi::ResourceState::ColorAttachment);
					b.Write(depth, Rhi::ResourceState::DepthStencilWrite);
				},
				[color, depth, clear, depthClear](RenderCommandContext& c)
				{
					std::array<Rhi::RenderingAttachmentDesc, 1> colors{};
					colors[0].View = &c.CreateView(color);
					colors[0].Load = Rhi::LoadOp::Clear;
					colors[0].Clear.Value = { clear[0], clear[1], clear[2], clear[3] };
					Rhi::TextureViewDesc depthView;
					depthView.PixelFormat = Rhi::Format::D32Float;
					const Rhi::DepthStencilAttachmentDesc depthAttachment{ &c.CreateView(depth, depthView), Rhi::LoadOp::Clear,
						Rhi::StoreOp::Store, depthClear, 0 };
					c.Commands().BeginRendering({ colors, &depthAttachment, { Width, Height } });
					c.Commands().EndRendering();
				});
			UiRenderFrame frame;
			frame.Paint = test.Paint;
			frame.Target = color;
			frame.Depth = depth;
			frame.DpiScale = test.Dpi;
			frame.ClipFromCanvas = clip;
			frame.Opacity = test.Opacity;
			frame.Composition = hdrScene;
			frame.Atlas = &atlasFrame;
			frame.Images = std::span(&sampledImage, 1);
			SWIM_REQUIRE(renderer.Record(graph, frame, program.Get(Rhi::Format::RGBA16Float), bindless.GetTable()).has_value());
			const auto readback = AddTextureReadback(graph, "UI world readback", color, { 0, {}, {}, { Width, Height, 1 } });
			executor.Execute(graph.Compile());
			atlasTextures.CommitFrame();
			executor.Wait();
			const auto gpu = readColors(readback, Rhi::Format::RGBA16Float, Width, Height);
			const auto& quads = renderer.GetLastQuads();
			const auto& constants = renderer.GetLastConstants();
			SWIM_CHECK_EQUAL(constants.Flags, UiDrawWorld);
			R::Canvas canvas{ Width, Height, std::vector<R::Float4>(gpu.size(), clear) };
			if (!test.Occluded)
			{
				R::RasterizeProjected(canvas, quads, constants, cpuSampler(atlasFrame));
			}
			const auto ambiguous = AmbiguousProjected(quads, constants, Width, Height);
			// The 1:1 cases match to 2.5 LSB like screen overlays; perspective ones compare
			// hardware derivatives (2 x 2 pixel differences) with analytic footprints, so
			// edge ramps and glyph ranges differ more: 6 LSB, at most 3 % outliers.
			const bool projective = test.Camera.Projection[14] != 0.0f;
			const float tolerance = (projective ? 6.0f : 2.5f) / 255.0f;
			const auto result = Compare(gpu, canvas, ambiguous, clear, tolerance, constants.WhiteScale, false);
			std::printf("             [ui %s] %zu quads: %u pixels compared, %u lit, %u outliers (worst %.2e)\n", test.Name, quads.size(),
				result.Compared, result.Lit, result.Outliers, double(result.Worst));
			if (test.Occluded)
			{
				SWIM_CHECK_EQUAL(result.Lit, 0u); // Scene depth in front of the panel hides it entirely.
			}
			else
			{
				SWIM_CHECK(result.Lit > result.Compared / (projective ? 20u : 4u));
			}
			SWIM_CHECK(result.Outliers <= result.Compared / (projective ? 33u : 100u));
			totalCompared += result.Compared;
			totalOutliers += result.Outliers;
		}

		// Render surface: the same document drawn into every mip of an sRGB surface (each
		// level re-rasterized at DPI / 2^mip), then shown 1:1 by a world panel that samples it.
		UiRenderSurfaces surfaces(*device, bindless);
		UiRenderSurfaceDesc surfaceDesc;
		surfaceDesc.Width = CanvasWidth;
		surfaceDesc.Height = CanvasHeight;
		const auto surface = surfaces.Create(surfaceDesc);
		UiSurfaceFrame surfaceFrame;
		std::vector<R::Float4> surfaceTexels; // Mip 0 as stored, decoded to linear.
		{
			RenderGraph graph;
			const auto atlasFrame = atlasTextures.Update(graph, atlas);
			const auto sampledImage = graph.ImportTexture(*image, Rhi::ResourceState::ShaderRead);
			UiSurfaceContent content;
			content.Paint = paint;
			content.PaintRevision = scene.Document.GetPaintRevision();
			content.DpiScale = Dpi;
			content.Atlas = &atlasFrame;
			content.Images = std::span(&sampledImage, 1);
			surfaceFrame =
				surfaces.Record(graph, surface, renderer, program.Get(Rhi::Format::RGBA8UnormSrgb), bindless.GetTable(), content);
			SWIM_CHECK(surfaceFrame.Drawn);
			SWIM_CHECK_EQUAL(surfaceFrame.MipLevels, 9u);
			std::vector<GraphReadback> readbacks;
			for (const std::uint32_t mip : { 0u, 2u })
			{
				readbacks.push_back(AddTextureReadback(graph, "UI surface readback", surfaceFrame.Texture,
					{ 0, { mip, 0 }, {}, { CanvasWidth >> mip, CanvasHeight >> mip, 1 } }));
			}
			executor.Execute(graph.Compile());
			atlasTextures.CommitFrame();
			surfaces.CommitFrame();
			executor.Wait();
			for (std::size_t level = 0; level < readbacks.size(); ++level)
			{
				const std::uint32_t mip = level == 0 ? 0u : 2u;
				const std::uint32_t w = CanvasWidth >> mip;
				const std::uint32_t h = CanvasHeight >> mip;
				const auto gpu = readColors(readbacks[level], Rhi::Format::RGBA8UnormSrgb, w, h);
				if (mip == 0)
				{
					surfaceTexels = gpu;
					for (auto& texel : surfaceTexels)
					{
						for (int c = 0; c < 3; ++c)
						{
							texel[c] = SrgbEotf(texel[c]);
						}
					}
				}
				R::QuadBuildDesc build;
				build.DpiScale = Dpi / float(1u << mip);
				build.AtlasPageSize = atlasFrame.PageSize;
				build.AtlasTextures = atlasFrame.TextureIndices;
				build.AtlasSampler = atlasFrame.SamplerIndex;
				const auto quads = R::BuildQuads(paint, build);
				R::Canvas canvas{ w, h, std::vector<R::Float4>(gpu.size()) };
				R::Rasterize(canvas, quads, R::BuildDrawConstants(w, h, content.Composition), cpuSampler(atlasFrame));
				const auto result = Compare(gpu, canvas, AmbiguousPixels(quads, w, h), {}, 2.5f / 255.0f, 1.0f, true);
				std::printf("             [ui surface mip %u] %ux%u: %u pixels compared, %u lit, %u outliers (worst %.2e)\n", mip, w, h,
					result.Compared, result.Lit, result.Outliers, double(result.Worst));
				SWIM_CHECK(result.Lit > result.Compared / 4u);
				SWIM_CHECK(result.Outliers <= result.Compared / 100u);
				totalCompared += result.Compared;
				totalOutliers += result.Outliers;
			}
		}
		{
			// A world panel showing the surface 1:1 (unchanged paint: the surface is only
			// imported, not drawn again): its decoded texels blended over the scene.
			RenderGraph graph;
			UiSurfaceContent content;
			content.Paint = paint;
			content.PaintRevision = scene.Document.GetPaintRevision();
			const auto shown =
				surfaces.Record(graph, surface, renderer, program.Get(Rhi::Format::RGBA8UnormSrgb), bindless.GetTable(), content);
			SWIM_CHECK(!shown.Drawn);
			const auto panel = UiRenderSurfaces::PanelPaint(shown, canvasSize);
			const auto& camera = cases[0].Camera;
			const auto clip = UI::ClipFromCanvas(UI::CanvasToWorld(UI::UiCanvasMode::WorldPanel, topLeft, canvasSize), camera);
			Rhi::TextureDesc colorDesc;
			colorDesc.Extent = { Width, Height, 1 };
			colorDesc.PixelFormat = Rhi::Format::RGBA16Float;
			colorDesc.Usage = Rhi::TextureUsage::ColorAttachment | Rhi::TextureUsage::TransferSource;
			const auto color = graph.CreateTexture(colorDesc);
			UiRenderFrame frame;
			frame.Paint = panel;
			frame.Target = color;
			frame.ClipFromCanvas = clip;
			frame.Composition = hdrScene;
			frame.Images = std::span(&shown.Texture, 1);
			frame.Clear = true;
			frame.ClearColor = { clear[0], clear[1], clear[2], clear[3] };
			SWIM_REQUIRE(renderer.Record(graph, frame, program.Get(Rhi::Format::RGBA16Float), bindless.GetTable()).has_value());
			const auto readback = AddTextureReadback(graph, "UI panel readback", color, { 0, {}, {}, { Width, Height, 1 } });
			executor.Execute(graph.Compile());
			surfaces.CommitFrame();
			executor.Wait();
			const auto gpu = readColors(readback, Rhi::Format::RGBA16Float, Width, Height);
			const auto quads = renderer.GetLastQuads();
			const auto constants = renderer.GetLastConstants();
			R::Canvas canvas{ Width, Height, std::vector<R::Float4>(gpu.size(), clear) };
			const auto sampleSurface = [&](std::uint32_t, std::uint32_t, float u, float v)
			{
				const float fx = u * float(CanvasWidth) - 0.5f;
				const float fy = v * float(CanvasHeight) - 0.5f;
				const int x0 = int(std::floor(fx));
				const int y0 = int(std::floor(fy));
				const float tx = fx - float(x0);
				const float ty = fy - float(y0);
				const auto texel = [&](int x, int y)
				{
					x = std::clamp(x, 0, int(CanvasWidth) - 1);
					y = std::clamp(y, 0, int(CanvasHeight) - 1);
					return surfaceTexels[std::size_t(y) * CanvasWidth + std::size_t(x)];
				};
				R::Float4 result{};
				for (int c = 0; c < 4; ++c)
				{
					result[c] = (texel(x0, y0)[c] * (1 - tx) + texel(x0 + 1, y0)[c] * tx) * (1 - ty) +
						(texel(x0, y0 + 1)[c] * (1 - tx) + texel(x0 + 1, y0 + 1)[c] * tx) * ty;
				}
				return result;
			};
			R::RasterizeProjected(canvas, quads, constants, sampleSurface);
			const auto result = Compare(
				gpu, canvas, AmbiguousProjected(quads, constants, Width, Height), clear, 2.5f / 255.0f, constants.WhiteScale, false);
			std::printf("             [ui surface panel] %u pixels compared, %u lit, %u outliers (worst %.2e)\n", result.Compared,
				result.Lit, result.Outliers, double(result.Worst));
			SWIM_CHECK(result.Lit > result.Compared / 4u);
			SWIM_CHECK(result.Outliers <= result.Compared / 100u);
			SWIM_CHECK_EQUAL(surfaces.GetStats().DrawnSurfaces, 1u);
			SWIM_CHECK_EQUAL(surfaces.GetStats().SkippedSurfaces, 1u);
			totalCompared += result.Compared;
			totalOutliers += result.Outliers;
		}
		std::printf("             [ui world] %u pixels compared, %u outliers\n", totalCompared, totalOutliers);
		executor.Wait();
		surfaces.Release(surface);
		surfaces.Drain();
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
			Swim::Testing::TestRegistry::Get().Add({ "RHI.Vulkan.Smoke", "UiWorldCanvasesMatchTheCpuReference", SWIM_TEST_LOCATION,
				+[]
				{
					Swim::Testing::RunValidatedVulkanSmoke(&RunUiWorldSmoke);
				} });
		}
		return true;
	}();
} // namespace
