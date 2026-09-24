#include "Engine/Platform/PlatformSystem.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/VulkanRhiBackend.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraphExecutor.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraphTransfers.h"
#include "Engine/Systems/Renderer/ScreenSpace/ScreenSpaceEffects.h"
#include "Engine/Systems/Renderer/ScreenSpace/ScreenSpaceReference.h"
#include "Tests/Fixtures/ScreenSpaceFixture.h"
#include "Tests/Fixtures/VulkanSmokeDiagnostics.h"
#include "Tests/Framework/Test.h"

#if defined(SWIM_SCREEN_SPACE_AO_SPIRV_PATH) && defined(SWIM_SCREEN_SPACE_BLUR_SPIRV_PATH) &&                                              \
	defined(SWIM_SCREEN_SPACE_COMPOSITE_SPIRV_PATH) && defined(SWIM_ENVIRONMENT_SKY_SPIRV_PATH) &&                                         \
	defined(SWIM_ENVIRONMENT_DOWNSAMPLE_SPIRV_PATH) && defined(SWIM_ENVIRONMENT_PREFILTER_SPIRV_PATH) &&                                   \
	defined(SWIM_ENVIRONMENT_IRRADIANCE_SPIRV_PATH) && defined(SWIM_ENVIRONMENT_BRDF_LUT_SPIRV_PATH)
#include "Tests/Fixtures/VulkanEnvironmentFixture.h"
#define SWIM_SCREEN_SPACE_SMOKE_AVAILABLE 1
#endif

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace
{
#ifdef SWIM_SCREEN_SPACE_SMOKE_AVAILABLE
	namespace Smoke = Swim::Testing::EnvironmentSmoke;
	namespace Ss = Swim::Render::ScreenSpace;
	namespace Scene = Swim::Testing::ScreenSpaceScene;

	// IEEE binary16 bits of value, rounded to nearest even.
	std::uint16_t FloatToHalf(float value)
	{
		const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
		const auto sign = std::uint16_t((bits >> 16) & 0x8000u);
		const std::uint32_t magnitude = bits & 0x7fffffffu;
		if (magnitude >= 0x47800000u)
		{
			return std::uint16_t(sign | (magnitude > 0x7f800000u ? 0x7e00u : 0x7c00u));
		}
		if (magnitude < 0x38800000u)
		{
			return std::uint16_t(sign | std::uint16_t(std::nearbyint(std::bit_cast<float>(magnitude) * 16777216.0f)));
		}
		const std::uint32_t mantissa = magnitude & 0x7fffffu;
		std::uint32_t half = ((((magnitude >> 23) - 127u + 15u)) << 10) | (mantissa >> 13);
		const std::uint32_t rest = mantissa & 0x1fffu;
		half += rest > 0x1000u || (rest == 0x1000u && (half & 1u)) ? 1u : 0u;
		return std::uint16_t(sign | half);
	}

	double PassMilliseconds(const std::vector<Swim::Render::GraphPassTiming>& timings, std::string_view prefix, bool* measured = nullptr)
	{
		double total = 0.0;
		bool any = false;
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
				any = true;
			}
			previousEnd = std::max(previousEnd, end);
		}
		if (measured)
		{
			*measured = any;
		}
		return total;
	}

	// Rounds an RGBA image through binary16, returning the halves (the CPU copy keeps the rounded values).
	std::vector<std::uint16_t> ToHalves(Ss::ColorImage& image)
	{
		std::vector<std::uint16_t> halves;
		halves.reserve(image.Texels.size() * 4);
		for (auto& texel : image.Texels)
		{
			for (auto& value : texel)
			{
				halves.push_back(FloatToHalf(value));
				value = Smoke::HalfToFloat(halves.back());
			}
		}
		return halves;
	}

	// A floor, a wall, a pillar and a floating slab: creases, gaps and depth edges.
	Scene::Scene MakeScene()
	{
		Scene::Scene scene;
		scene.Boxes.push_back({ { -12.0f, 0.0f, -12.0f }, { -3.0f, 5.0f, 12.0f } }); // Wall.
		scene.Boxes.push_back({ { 0.0f, 0.0f, -1.0f }, { 1.0f, 3.0f, 0.0f } });		 // Pillar.
		scene.Boxes.push_back({ { -2.5f, 0.6f, 1.0f }, { -0.5f, 0.9f, 3.0f } });	 // Slab over the floor.
		return scene;
	}

	bool Close(float actual, float expected, float relative, float absolute)
	{
		return std::abs(actual - expected) <= absolute + relative * std::abs(expected);
	}
#endif

	// Critical-path item 76 on a real device. Ray-cast inputs (reverse-Z depth, world
	// normals, HDR color and its indirect part) go through ScreenSpaceEffects (GTAO,
	// the depth-aware blur, AO on indirect light and exponential height fog) and every
	// stage is read back and compared with ScreenSpace:: (ScreenSpaceReference.h), each
	// from the GPU's own previous stage.
	void RunScreenSpaceSmoke(const Swim::Rhi::GraphicsSystemDesc& graphicsDesc)
	{
#ifndef SWIM_SCREEN_SPACE_SMOKE_AVAILABLE
		SWIM_REQUIRE_MESSAGE(false, "Screen-space smoke requires generated Slang artifacts");
#else
		using namespace Swim;
		using namespace Swim::Render;

		Platform::PlatformSystem platform;
		SWIM_REQUIRE_MESSAGE(platform.Initialize(), "Screen-space smoke requires working SDL Vulkan platform services");
		auto graphics = RhiVulkan::CreateGraphicsSystem(graphicsDesc);
		SWIM_REQUIRE(graphics);
		Testing::RequireVulkanSmokeValidation(*graphics, graphicsDesc.Checks);
		auto device = graphics->GetAdapter(0).CreateDevice();
		SWIM_REQUIRE(device);

		const auto ao =
			Smoke::MakeCompute(*device, SWIM_SCREEN_SPACE_AO_SPIRV_PATH, SWIM_SCREEN_SPACE_AO_REFLECTION_PATH, "Screen-space AO");
		const auto blur =
			Smoke::MakeCompute(*device, SWIM_SCREEN_SPACE_BLUR_SPIRV_PATH, SWIM_SCREEN_SPACE_BLUR_REFLECTION_PATH, "Screen-space blur");
		const auto composite = Smoke::MakeCompute(
			*device, SWIM_SCREEN_SPACE_COMPOSITE_SPIRV_PATH, SWIM_SCREEN_SPACE_COMPOSITE_REFLECTION_PATH, "Screen-space composite");
		ScreenSpaceEffectsDesc desc;
		desc.AmbientOcclusion = { ao.Pipeline.get(), ao.Layout.get(), ao.Space };
		desc.Blur = { blur.Pipeline.get(), blur.Layout.get(), blur.Space };
		desc.Composite = { composite.Pipeline.get(), composite.Layout.get(), composite.Space };
		desc.DebugName = "Screen space";
		const ScreenSpaceEffects effects(desc);
		RenderGraphExecutor executor(*device);
		const auto scene = MakeScene();

		struct FrameSpec
		{
			const char* Name;
			std::uint32_t Width;
			std::uint32_t Height;
			Rhi::Format DepthFormat;
			ScreenSpaceSettings Settings;
			std::array<float, 2> JitterPixels;
			std::uint32_t NoiseFrame;
			bool Compare; // False: timing only.
		};

		const auto frame = [&](const FrameSpec& spec)
		{
			auto view = Scene::View({ 4.0f, 2.5f, 7.0f }, { -1.0f, 0.5f, 0.0f }, float(spec.Width) / float(spec.Height));
			view.Jitter = { spec.JitterPixels[0] * 2.0f / float(spec.Width), -spec.JitterPixels[1] * 2.0f / float(spec.Height) };
			auto inputs = Scene::Render(scene, view, spec.Width, spec.Height, 0.4f);
			// Color = direct (a sun) + indirect (ambient * albedo), albedo a 1 m checker.
			Ss::ColorImage color(spec.Width, spec.Height), indirect(spec.Width, spec.Height);
			for (std::size_t i = 0; i < inputs.Hits.size(); ++i)
			{
				const auto& hit = inputs.Hits[i];
				if (hit.T == 0.0f)
				{
					color.Texels[i] = { 0.3f, 0.5f, 0.9f, 1.0f }; // Sky.
					continue;
				}
				const bool dark = (int(std::floor(hit.Position[0])) + int(std::floor(hit.Position[2]))) % 2 != 0;
				const Ss::Float3 albedo = dark ? Ss::Float3{ 0.2f, 0.25f, 0.3f } : Ss::Float3{ 0.8f, 0.7f, 0.6f };
				const float sun = std::max(0.0f, 0.5f * hit.Normal[0] + 0.8f * hit.Normal[1] + 0.33f * hit.Normal[2]) * 3.0f;
				for (int c = 0; c < 3; ++c)
				{
					indirect.Texels[i][c] = 0.6f * albedo[c];
					color.Texels[i][c] = sun * albedo[c] + indirect.Texels[i][c];
				}
				color.Texels[i][3] = 1.0f;
				indirect.Texels[i][3] = 1.0f;
			}
			const auto colorHalves = ToHalves(color);
			const auto indirectHalves = ToHalves(indirect);
			const auto normalHalves = ToHalves(inputs.Normal);

			RenderGraph graph;
			const Rhi::BufferTextureCopyRegion whole{ 0, {}, {}, { spec.Width, spec.Height, 1 } };
			const auto texture = [&](Rhi::Format format, Rhi::TextureUsage usage, const char* name)
			{
				Rhi::TextureDesc textureDesc;
				textureDesc.Extent = { spec.Width, spec.Height, 1 };
				textureDesc.PixelFormat = format;
				textureDesc.Usage = usage | Rhi::TextureUsage::Sampled | Rhi::TextureUsage::TransferDestination;
				textureDesc.DebugName = name;
				return graph.CreateTexture(textureDesc);
			};
			ScreenSpaceFrame input;
			input.Color = texture(Rhi::Format::RGBA16Float, Rhi::TextureUsage::ColorAttachment, "Screen-space color");
			input.Indirect = texture(Rhi::Format::RGBA16Float, Rhi::TextureUsage::ColorAttachment, "Screen-space indirect");
			input.Normal = texture(Rhi::Format::RGBA16Float, Rhi::TextureUsage::ColorAttachment, "Screen-space normal");
			input.Depth = texture(spec.DepthFormat,
				spec.DepthFormat == Rhi::Format::D32Float ? Rhi::TextureUsage::DepthStencilAttachment : Rhi::TextureUsage::ColorAttachment,
				"Screen-space depth");
			AddTextureUpload(graph, "Color upload", std::as_bytes(std::span(colorHalves)), input.Color, whole);
			AddTextureUpload(graph, "Indirect upload", std::as_bytes(std::span(indirectHalves)), input.Indirect, whole);
			AddTextureUpload(graph, "Normal upload", std::as_bytes(std::span(normalHalves)), input.Normal, whole);
			AddTextureUpload(graph, "Depth upload", std::as_bytes(std::span(inputs.Depth.Texels)), input.Depth, whole);
			input.View = view;
			input.Settings = spec.Settings;
			input.NoiseFrame = spec.NoiseFrame;
			const auto resources = effects.Record(graph, input);
			const auto& params = resources.ParamsRecord;
			if (resources.Passthrough)
			{
				SWIM_CHECK(resources.Output == input.Color && !resources.CompositePass);
				std::printf("             [screen space %s] passthrough: nothing recorded\n", spec.Name);
				return;
			}
			std::optional<GraphReadback> rawReadback, aoReadback;
			if (spec.Compare && resources.AmbientOcclusion)
			{
				rawReadback = AddTextureReadback(graph, "AO raw", *resources.AmbientOcclusionRaw, whole);
				aoReadback = AddTextureReadback(graph, "AO", *resources.AmbientOcclusion, whole);
			}
			const auto outputReadback = AddTextureReadback(graph, "Output", resources.Output, whole);
			executor.Execute(graph.Compile());
			const auto timings = executor.ReadTimings();
			const auto read = [&](const GraphReadback& readback, auto& target)
			{
				SWIM_REQUIRE(
					executor.TryReadback(readback.Buffer, std::as_writable_bytes(std::span(target))) == Rhi::ReadbackStatus::Ready);
			};

			std::uint32_t rawOutliers = 0, blurOutliers = 0, outputMismatches = 0, texels = spec.Width * spec.Height;
			float rawWorst = 0.0f, blurWorst = 0.0f, outputWorst = 0.0f;
			double creaseSum = 0.0, openSum = 0.0;
			std::uint32_t creaseCount = 0, openCount = 0;
			if (spec.Compare)
			{
				std::optional<Ss::ScalarImage> gpuAo;
				if (rawReadback)
				{
					Ss::ScalarImage gpuRaw(spec.Width, spec.Height);
					gpuAo.emplace(spec.Width, spec.Height);
					read(*rawReadback, gpuRaw.Texels);
					read(*aoReadback, gpuAo->Texels);
					for (std::uint32_t y = 0; y < spec.Height; ++y)
					{
						for (std::uint32_t x = 0; x < spec.Width; ++x)
						{
							// 1. GTAO over the same inputs (trigonometry and pixel snapping may differ slightly).
							const float raw = Ss::GtaoTexel(params, inputs.Depth, inputs.Normal, x, y);
							const float rawError = std::abs(gpuRaw.At(x, y) - raw);
							rawWorst = std::max(rawWorst, rawError);
							rawOutliers += rawError > 0.02f ? 1u : 0u;
							// 2. The blur over the GPU's raw visibility.
							const float blurred = Ss::BlurTexel(params, gpuRaw, inputs.Depth, x, y);
							const float blurError = std::abs(gpuAo->At(x, y) - blurred);
							blurWorst = std::max(blurWorst, blurError);
							blurOutliers += blurError > 1.0e-3f ? 1u : 0u;
							const auto& hit = inputs.Hits[std::size_t(y) * spec.Width + x];
							if (hit.T > 0.0f && hit.Normal[1] > 0.5f)
							{
								if (hit.Position[0] < -2.9f && std::abs(hit.Position[2]) < 6.0f)
								{
									creaseSum += gpuAo->At(x, y);
									++creaseCount;
								}
								else if (hit.Position[0] > 2.0f)
								{
									openSum += gpuAo->At(x, y);
									++openCount;
								}
							}
						}
					}
					SWIM_CHECK(rawOutliers <= texels / 100);
					SWIM_CHECK(blurOutliers <= texels / 1000);
					SWIM_REQUIRE(creaseCount > 0u && openCount > 0u);
					SWIM_CHECK(creaseSum / creaseCount + 0.2 < openSum / openCount); // The wall's base is darker than the open floor.
				}
				// 3. The composite over the GPU's visibility.
				std::vector<std::uint16_t> raw(std::size_t(texels) * 4);
				read(outputReadback, raw);
				for (std::uint32_t y = 0; y < spec.Height; ++y)
				{
					for (std::uint32_t x = 0; x < spec.Width; ++x)
					{
						const std::size_t i = std::size_t(y) * spec.Width + x;
						const auto expected = Ss::CompositeTexel(
							params, color.Texels[i], indirect.Texels[i], gpuAo ? gpuAo->At(x, y) : 1.0f, inputs.Depth.Texels[i], x, y);
						bool mismatch = false;
						for (int c = 0; c < 4; ++c)
						{
							const float actual = Smoke::HalfToFloat(raw[i * 4 + c]);
							mismatch = mismatch || !Close(actual, expected[c], 3.0e-3f, 1.0e-4f);
							outputWorst = std::max(outputWorst, std::abs(actual - expected[c]) / std::max(std::abs(expected[c]), 1.0e-2f));
						}
						outputMismatches += mismatch ? 1u : 0u;
					}
				}
				SWIM_CHECK(outputMismatches <= texels / 1000);
			}
			std::printf("             [screen space %s] %ux%u: AO raw worst %.2e (%u outliers), blur worst %.2e (%u), crease %.3f vs open "
						"%.3f; output worst relative %.2e (%u mismatches)\n",
				spec.Name, spec.Width, spec.Height, double(rawWorst), rawOutliers, double(blurWorst), blurOutliers,
				creaseCount ? creaseSum / creaseCount : 0.0, openCount ? openSum / openCount : 0.0, double(outputWorst), outputMismatches);
			bool measured = false;
			const double compositeMs = PassMilliseconds(timings, "Screen space composite", &measured);
			if (measured)
			{
				std::printf("             [screen space %s] GPU: AO %.3f ms, blur %.3f ms, composite %.3f ms\n", spec.Name,
					PassMilliseconds(timings, "Screen space AO") - PassMilliseconds(timings, "Screen space AO blur"),
					PassMilliseconds(timings, "Screen space AO blur"), compositeMs);
			}
		};

		constexpr std::uint32_t width = 256;
		constexpr std::uint32_t height = 144;
		ScreenSpaceSettings aoOnly;
		aoOnly.AmbientOcclusion.Radius = 1.0f;
		frame({ "ao", width, height, Rhi::Format::D32Float, aoOnly, { 0.0f, 0.0f }, 0, true });

		ScreenSpaceSettings full;
		full.AmbientOcclusion.Radius = 1.5f;
		full.AmbientOcclusion.SliceCount = 4;
		full.AmbientOcclusion.StepCount = 8;
		full.AmbientOcclusion.Power = 1.5f;
		full.Fog.Enabled = true;
		full.Fog.Density = 0.08f;
		full.Fog.HeightFalloff = 0.4f;
		full.Fog.BaseHeight = 0.5f;
		full.Fog.SunColor = { 3.0f, 2.4f, 1.8f };
		full.Fog.SunDirection = { -0.6f, -0.3f, -0.74f };
		full.Fog.StartDistance = 1.0f;
		full.Fog.MaxDistance = 60.0f;
		frame({ "ao + fog, jittered, r32f depth", width, height, Rhi::Format::R32Float, full, { 0.31f, -0.27f }, 5, true });

		ScreenSpaceSettings fogOnly = full;
		fogOnly.AmbientOcclusion.Enabled = false;
		frame({ "fog only", width, height, Rhi::Format::D32Float, fogOnly, { 0.0f, 0.0f }, 0, true });

		ScreenSpaceSettings off;
		off.AmbientOcclusion.Enabled = false;
		frame({ "off", width, height, Rhi::Format::D32Float, off, { 0.0f, 0.0f }, 0, true });

		ScreenSpaceSettings timing = aoOnly;
		timing.Fog.Enabled = true;
		frame({ "1080p timing", 1920, 1080, Rhi::Format::D32Float, timing, { 0.0f, 0.0f }, 0, false });
		executor.Trim();
#endif
	}

	[[maybe_unused]] const bool registered = []
	{
		const char* enabled = std::getenv("SWIM_RUN_RHI_SMOKE");
		if (enabled != nullptr && std::string_view(enabled) == "1")
		{
			Swim::Testing::TestRegistry::Get().Add({ "RHI.Vulkan.Smoke", "ScreenSpaceEffectsMatchTheCpuReference", SWIM_TEST_LOCATION,
				+[]
				{
					Swim::Testing::RunValidatedVulkanSmoke(&RunScreenSpaceSmoke);
				} });
		}
		return true;
	}();
} // namespace
