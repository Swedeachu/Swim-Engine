#include "Engine/Platform/PlatformSystem.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/VulkanRhiBackend.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderCommandContext.h"
#include "Tests/Fixtures/VulkanSmokeDiagnostics.h"
#include "Tests/Framework/Test.h"

#if defined(SWIM_ENVIRONMENT_SKY_SPIRV_PATH) && defined(SWIM_ENVIRONMENT_DOWNSAMPLE_SPIRV_PATH) &&                                         \
	defined(SWIM_ENVIRONMENT_PREFILTER_SPIRV_PATH) && defined(SWIM_ENVIRONMENT_IRRADIANCE_SPIRV_PATH) &&                                   \
	defined(SWIM_ENVIRONMENT_BRDF_LUT_SPIRV_PATH) && defined(SWIM_RHI_PBR_GALLERY_SPIRV_PATH)
#include "Tests/Fixtures/PbrGalleryFixture.h"
#include "Tests/Fixtures/VulkanEnvironmentFixture.h"
#define SWIM_PBR_GALLERY_SMOKE_AVAILABLE 1
#endif

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

namespace
{
#ifdef SWIM_PBR_GALLERY_SMOKE_AVAILABLE
	// Mirrors PbrGallery.slang's records (std430).
	struct GalleryView
	{
		float LightDirection[4];
		float LightRadiance[4];
		float Intensity = 1.0f;
		float Rotation = 0.0f;
		std::uint32_t PrefilteredMipCount = 0;
		std::uint32_t Width = 0;
		std::uint32_t Height = 0;
		std::uint32_t Reserved[3] = {};
	};

	static_assert(sizeof(GalleryView) == 64);

	struct GallerySphere
	{
		float CenterRadius[4];
		float BaseColorMetallic[4];
		float Roughness[4];
	};

	static_assert(sizeof(GallerySphere) == 48);
#endif

	// Critical-path item 62: the PBR image-regression gallery on a real device. A 6 x
	// 4 grid of analytic spheres (gold, red plastic, white dielectric, occluded
	// half-metal copper; roughness 0 .. 1 across) is rendered by PbrGallery.slang
	// with StandardPbr + the GPU-built environment (item 61), and compared per pixel
	// with the CPU reference renderer (Tests/Fixtures/PbrGalleryFixture.h) fed the
	// GPU's own prefiltered cube, SH and LUT. Frames:
	//  1. default sky + a sun-aligned directional light;
	//  2. environment only, intensity 0.6, rotated 1.1 rad around +Y;
	//  3. white furnace (uniform radiance 1, no light).
	// Beyond the per-pixel match, the GPU image itself must meet the gallery
	// expectations: white dielectrics are exactly 1 in the furnace, nothing exceeds 1
	// there, and metal highlights dim monotonically with roughness. Set
	// SWIM_PBR_GALLERY_DUMP=<directory> to write the GPU, CPU and difference images
	// (.pfm linear and .bmp tone-mapped).
	void RunPbrGallerySmoke(const Swim::Rhi::GraphicsSystemDesc& graphicsDesc)
	{
#ifndef SWIM_PBR_GALLERY_SMOKE_AVAILABLE
		SWIM_REQUIRE_MESSAGE(false, "PBR gallery smoke requires generated Slang artifacts");
#else
		using namespace Swim;
		using namespace Swim::Render;
		using S = Rhi::ResourceState;
		namespace Env = Swim::Render::Environment;
		namespace Smoke = Swim::Testing::EnvironmentSmoke;
		namespace Gallery = Swim::Testing::PbrGallery;

		const auto galleryInterface = Smoke::ReflectProgram(SWIM_RHI_PBR_GALLERY_REFLECTION_PATH);
		const auto galleryBytes = Smoke::ReadSpirv(SWIM_RHI_PBR_GALLERY_SPIRV_PATH);

		Platform::PlatformSystem platform;
		SWIM_REQUIRE_MESSAGE(platform.Initialize(), "PBR gallery smoke requires working SDL Vulkan platform services");
		auto graphics = RhiVulkan::CreateGraphicsSystem(graphicsDesc);
		SWIM_REQUIRE(graphics);
		Testing::RequireVulkanSmokeValidation(*graphics, graphicsDesc.Checks);
		auto device = graphics->GetAdapter(0).CreateDevice();
		SWIM_REQUIRE(device);
		Smoke::EnvironmentPrograms programs(*device);

		const auto& draw = galleryInterface.Interface;
		const std::array<Rhi::ShaderStageArtifact, 2> stages{ { { Rhi::ShaderStageMask::Vertex, "vertexMain", galleryBytes },
			{ Rhi::ShaderStageMask::Fragment, "fragmentMain", galleryBytes } } };
		auto program = device->CreateShaderProgram({ stages, { draw.DescriptorSchemas, draw.PushConstants }, "PBR gallery" });
		SWIM_REQUIRE(program);
		auto layout = device->CreatePipelineLayout({ program.get(), "PBR gallery layout" });
		SWIM_REQUIRE(layout);
		const Rhi::Format colorFormat = Rhi::Format::RGBA16Float;
		Rhi::GraphicsPipelineDesc pipelineDesc{};
		pipelineDesc.Program = program.get();
		pipelineDesc.Layout = layout.get();
		pipelineDesc.ColorFormats = { &colorFormat, 1 };
		pipelineDesc.Raster.Cull = Rhi::CullMode::None;
		// Impostors do not overlap: no depth attachment (the RHI defaults depth test/write on).
		pipelineDesc.DepthStencil.DepthTest = false;
		pipelineDesc.DepthStencil.DepthWrite = false;
		pipelineDesc.DebugName = "PBR gallery";
		auto pipeline = device->CreateGraphicsPipeline(pipelineDesc);
		SWIM_REQUIRE(pipeline);

		RenderGraphExecutor executor(*device);
		EnvironmentMapDesc map;
		map.SourceSize = 128;
		map.PrefilteredSize = 64;
		map.PrefilteredMipCount = 6;
		map.PrefilterSampleCount = 128;
		map.IrradianceFaceSize = 16;
		constexpr std::uint32_t lutSize = 64;
		constexpr std::uint32_t lutSamples = 512;

		const auto galleryLayout = Gallery::MakeLayout(64);
		const std::uint32_t width = galleryLayout.Width;
		const std::uint32_t height = galleryLayout.Height;
		std::vector<GallerySphere> spheres;
		for (const auto& sphere : galleryLayout.Spheres)
		{
			spheres.push_back({ { sphere.CenterX, sphere.CenterY, sphere.Radius, 0 },
				{ sphere.BaseColor[0], sphere.BaseColor[1], sphere.BaseColor[2], sphere.Metallic },
				{ sphere.Roughness, sphere.Occlusion, 0, 0 } });
		}

		const char* dump = std::getenv("SWIM_PBR_GALLERY_DUMP");

		struct FrameSpec
		{
			const char* Name;
			Env::ProceduralSky Sky;
			Gallery::Frame Frame;
		};

		std::array<FrameSpec, 3> frames{};
		frames[0].Name = "lit";
		frames[0].Frame.LightDirection = frames[0].Sky.SunDirection;
		frames[0].Frame.LightRadiance = { 3, 3, 3 };
		frames[1].Name = "rotated";
		frames[1].Frame.Environment = { 0.6f, 1.1f };
		frames[2].Name = "furnace";
		frames[2].Sky = Env::ProceduralSky::Uniform(1.0f);

		for (const auto& spec : frames)
		{
			RenderGraph graph;
			const auto environment = programs.Builder->Record(graph, spec.Sky, map);
			const auto lut = programs.Builder->RecordBrdfLut(graph, lutSize, lutSamples);
			GalleryView view{};
			const auto light = Env::Normalize(spec.Frame.LightDirection);
			for (int c = 0; c < 3; ++c)
			{
				view.LightDirection[c] = light[c];
				view.LightRadiance[c] = spec.Frame.LightRadiance[c];
			}
			view.Intensity = spec.Frame.Environment.Intensity;
			view.Rotation = spec.Frame.Environment.Rotation;
			view.PrefilteredMipCount = map.PrefilteredMipCount;
			view.Width = width;
			view.Height = height;
			const auto viewUpload = graph.CreateUpload(std::as_bytes(std::span(&view, 1)), "Gallery view", Rhi::BufferUsage::Storage, 16);
			const auto sphereUpload =
				graph.CreateUpload(std::as_bytes(std::span(spheres)), "Gallery spheres", Rhi::BufferUsage::Storage, 16);
			const auto target = graph.CreateTexture({ Rhi::TextureDimension::Texture2D, { width, height, 1 }, colorFormat,
				Rhi::TextureUsage::ColorAttachment | Rhi::TextureUsage::TransferSource, 1, 1, Rhi::SampleCount::X1, "Gallery target" });
			graph.AddPass(
				"PBR gallery", Rhi::QueueType::Graphics,
				[&](RenderGraphBuilder& b)
				{
					b.Read(sphereUpload, S::ShaderRead);
					b.Read(viewUpload, S::ShaderRead);
					b.Read(environment.Irradiance, S::ShaderRead);
					b.Read(environment.Prefiltered, S::ShaderRead);
					b.Read(lut, S::ShaderRead);
					b.Write(target, S::ColorAttachment);
				},
				[&](RenderCommandContext& c)
				{
					auto table = c.Device().CreateDescriptorTable({ layout.get(), 0, 0, "Gallery table" });
					SWIM_REQUIRE(table);
					std::array<Rhi::DescriptorWrite, 6> writes{};
					for (std::uint32_t binding = 0; binding < writes.size(); ++binding)
					{
						writes[binding].Binding = binding;
					}
					const auto sphereRange = c.GetRange(sphereUpload);
					writes[0].BufferResource = sphereRange.Buffer;
					writes[0].BufferOffset = sphereRange.Offset;
					writes[0].BufferRange = sphereRange.Size;
					const auto viewRange = c.GetRange(viewUpload);
					writes[1].BufferResource = viewRange.Buffer;
					writes[1].BufferOffset = viewRange.Offset;
					writes[1].BufferRange = viewRange.Size;
					writes[2].BufferResource = &c.Get(environment.Irradiance);
					Rhi::TextureViewDesc cubeView;
					cubeView.Dimension = Rhi::TextureViewDimension::TextureCube;
					cubeView.PixelFormat = Rhi::Format::RGBA16Float;
					cubeView.MipLevelCount = map.PrefilteredMipCount;
					cubeView.ArrayLayerCount = 6;
					writes[3].TextureResource = &c.CreateView(environment.Prefiltered, cubeView);
					Rhi::TextureViewDesc lutView;
					lutView.PixelFormat = Rhi::Format::RGBA16Float;
					writes[4].TextureResource = &c.CreateView(lut, lutView);
					writes[5].SamplerResource = programs.LinearClamp.get();
					table->Write(writes);
					auto& retained = static_cast<Rhi::DescriptorTable&>(c.Retain(std::move(table)));
					Rhi::RenderingAttachmentDesc color{};
					color.View = &c.CreateView(target);
					color.Load = Rhi::LoadOp::Clear;
					auto& commands = c.Commands();
					commands.BeginRendering({ { &color, 1 }, nullptr, { width, height } });
					commands.BindGraphicsPipeline(*pipeline);
					commands.BindDescriptorTable(0, retained);
					commands.SetViewport({ 0, 0, float(width), float(height) });
					commands.SetScissor({ 0, 0, width, height });
					commands.Draw(6, std::uint32_t(spheres.size()), 0, 0);
					commands.EndRendering();
				});
			const auto prefilteredReadback =
				Smoke::AddCubeReadback(graph, environment.Prefiltered, map.PrefilteredSize, map.PrefilteredMipCount);
			const auto irradianceReadback =
				AddBufferReadback(graph, "Irradiance readback", environment.Irradiance, 0, EnvironmentIrradianceBindings::OutputBytes);
			const auto lutReadback = AddTextureReadback(graph, "LUT readback", lut, { 0, {}, {}, { lutSize, lutSize, 1 } });
			const auto imageReadback = AddTextureReadback(graph, "Gallery image", target, { 0, {}, {}, { width, height, 1 } });
			executor.Execute(graph.Compile());
			executor.Wait();

			// The CPU reference from the GPU's own environment maps.
			const Env::EnvironmentProbe probe(Smoke::ReadIrradiance(executor, irradianceReadback),
				Smoke::ReadCube(executor, prefilteredReadback), Smoke::ReadImage(executor, lutReadback, lutSize, lutSize));
			const auto expected = Gallery::Render(galleryLayout, probe, spec.Frame);
			const auto actual = Smoke::ReadImage(executor, imageReadback, width, height).Texels;

			std::uint32_t compared = 0;
			std::uint32_t mismatched = 0;
			double sumError = 0.0;
			float worst = 0.0f;
			std::uint32_t worstX = 0;
			std::uint32_t worstY = 0;
			std::vector<float> peak(spheres.size(), 0.0f);
			std::vector<Gallery::Float4> difference(actual.size(), Gallery::Float4{ 0, 0, 0, 1 });
			for (std::uint32_t y = 0; y < height; ++y)
			{
				for (std::uint32_t x = 0; x < width; ++x)
				{
					const std::size_t index = std::size_t(y) * width + x;
					const auto coverage = Gallery::Cover(galleryLayout, x, y);
					if (!coverage)
					{
						// Background, away from any silhouette, keeps the clear color.
						bool nearEdge = false;
						for (const auto& sphere : galleryLayout.Spheres)
						{
							const float dx = float(x) + 0.5f - sphere.CenterX;
							const float dy = float(y) + 0.5f - sphere.CenterY;
							nearEdge = nearEdge || std::sqrt(dx * dx + dy * dy) < sphere.Radius + 1.0f;
						}
						if (!nearEdge)
						{
							SWIM_CHECK((actual[index] == Gallery::Float4{ 0, 0, 0, 0 }));
						}
						continue;
					}
					peak[coverage->Sphere] = std::max(peak[coverage->Sphere], Gallery::Luminance(actual[index]));
					if (coverage->EdgeDistance < 1.0f)
					{
						continue;
					}
					++compared;
					bool bad = false;
					for (int c = 0; c < 3; ++c)
					{
						const float error = std::abs(actual[index][c] - expected[index][c]);
						const float relative = error / (std::abs(expected[index][c]) + 0.05f);
						sumError += relative;
						bad = bad || error > 0.01f + 0.03f * std::abs(expected[index][c]);
						difference[index][c] = error * 10.0f;
						if (relative > worst)
						{
							worst = relative;
							worstX = x;
							worstY = y;
						}
					}
					mismatched += bad ? 1u : 0u;
					SWIM_CHECK(std::abs(actual[index][3] - 1.0f) < 1.0e-3f);
				}
			}
			const double meanError = sumError / double(3 * std::max(compared, 1u));
			const std::size_t worstIndex = std::size_t(worstY) * width + worstX;
			std::printf("             [gallery %s] %u pixels compared, %u outside tolerance, mean relative error %.2e, worst %.2e at "
						"(%u, %u): GPU (%.4f %.4f %.4f) CPU (%.4f %.4f %.4f)\n",
				spec.Name, compared, mismatched, meanError, worst, worstX, worstY, actual[worstIndex][0], actual[worstIndex][1],
				actual[worstIndex][2], expected[worstIndex][0], expected[worstIndex][1], expected[worstIndex][2]);
			SWIM_CHECK(compared > 24u * 1800u);
			SWIM_CHECK(mismatched <= compared / 1000u); // Filtering-precision outliers only.
			SWIM_CHECK(meanError < 4.0e-3);

			// Gallery expectations on the GPU image itself.
			if (std::string_view(spec.Name) == "furnace")
			{
				for (std::uint32_t y = 0; y < height; ++y)
				{
					for (std::uint32_t x = 0; x < width; ++x)
					{
						const auto coverage = Gallery::Cover(galleryLayout, x, y);
						if (!coverage || coverage->EdgeDistance < 1.0f)
						{
							continue;
						}
						const auto& pixel = actual[std::size_t(y) * width + x];
						SWIM_CHECK(pixel[0] <= 1.01f && pixel[1] <= 1.01f && pixel[2] <= 1.01f);
						if (coverage->Sphere / galleryLayout.Columns == Gallery::WhiteDielectricRow)
						{
							SWIM_CHECK(std::abs(pixel[0] - 1.0f) < 0.01f && std::abs(pixel[1] - 1.0f) < 0.01f &&
								std::abs(pixel[2] - 1.0f) < 0.01f);
						}
					}
				}
			}
			else if (std::string_view(spec.Name) == "rotated")
			{
				for (std::uint32_t column = 1; column < galleryLayout.Columns; ++column)
				{
					SWIM_CHECK(peak[column] < peak[column - 1]); // Gold: the sun's reflection spreads and dims.
				}
			}

			if (dump != nullptr && *dump != '\0')
			{
				const std::string stem = std::string(dump) + "/pbr-gallery-" + spec.Name;
				SWIM_CHECK(Gallery::WriteImages(stem + "-gpu", actual, width, height));
				SWIM_CHECK(Gallery::WriteImages(stem + "-cpu", expected, width, height));
				SWIM_CHECK(Gallery::WriteImages(stem + "-difference-x10", difference, width, height));
				std::printf("             [gallery %s] images written to %s-*.{pfm,bmp}\n", spec.Name, stem.c_str());
			}
		}
		executor.Trim();
#endif
	}

	[[maybe_unused]] const bool registered = []
	{
		const char* enabled = std::getenv("SWIM_RUN_RHI_SMOKE");
		if (enabled != nullptr && std::string_view(enabled) == "1")
		{
			Swim::Testing::TestRegistry::Get().Add({ "RHI.Vulkan.Smoke", "PbrGalleryMatchesTheCpuReference", SWIM_TEST_LOCATION,
				+[]
				{
					Swim::Testing::RunValidatedVulkanSmoke(&RunPbrGallerySmoke);
				} });
		}
		return true;
	}();
} // namespace
