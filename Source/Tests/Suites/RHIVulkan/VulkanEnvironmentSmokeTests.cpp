#include "Engine/Platform/PlatformSystem.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/VulkanRhiBackend.h"
#include "Tests/Fixtures/VulkanSmokeDiagnostics.h"
#include "Tests/Framework/Test.h"

#if defined(SWIM_ENVIRONMENT_SKY_SPIRV_PATH) && defined(SWIM_ENVIRONMENT_DOWNSAMPLE_SPIRV_PATH) &&                                         \
	defined(SWIM_ENVIRONMENT_PREFILTER_SPIRV_PATH) && defined(SWIM_ENVIRONMENT_IRRADIANCE_SPIRV_PATH) &&                                   \
	defined(SWIM_ENVIRONMENT_BRDF_LUT_SPIRV_PATH)
#include "Tests/Fixtures/VulkanEnvironmentFixture.h"
#define SWIM_ENVIRONMENT_SMOKE_AVAILABLE 1
#endif

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string_view>

namespace
{
	// Critical-path item 61 on a real device. Three environments are built on the
	// GPU by EnvironmentBuilder (default sky, a white furnace, then a brighter sky
	// with the sun moved) and every stage is compared with its CPU definition fed the
	// GPU's own inputs:
	//  1. sky mip 0 against ProceduralSky::Evaluate at texel centers;
	//  2. every source mip against the box filter of the GPU mip above;
	//  3. every prefiltered texel against PrefilterDirection over the GPU source
	//     (CubeImage emulates seamless trilinear sampling);
	//  4. SH irradiance against ProjectIrradianceSh of the GPU mip;
	//  5. the BRDF LUT against BuildBrdfLut.
	// The furnace additionally proves the prefilter and irradiance preserve a
	// uniform radiance exactly.
	void RunEnvironmentSmoke(const Swim::Rhi::GraphicsSystemDesc& graphicsDesc)
	{
#ifndef SWIM_ENVIRONMENT_SMOKE_AVAILABLE
		SWIM_REQUIRE_MESSAGE(false, "Environment smoke requires generated Slang artifacts");
#else
		using namespace Swim;
		using namespace Swim::Render;
		namespace Env = Swim::Render::Environment;
		namespace Smoke = Swim::Testing::EnvironmentSmoke;

		Platform::PlatformSystem platform;
		SWIM_REQUIRE_MESSAGE(platform.Initialize(), "Environment smoke requires working SDL Vulkan platform services");
		auto graphics = RhiVulkan::CreateGraphicsSystem(graphicsDesc);
		SWIM_REQUIRE(graphics);
		Testing::RequireVulkanSmokeValidation(*graphics, graphicsDesc.Checks);
		auto device = graphics->GetAdapter(0).CreateDevice();
		SWIM_REQUIRE(device);
		Smoke::EnvironmentPrograms programs(*device);
		RenderGraphExecutor executor(*device);

		EnvironmentMapDesc map;
		map.SourceSize = 64;
		map.PrefilteredSize = 32;
		map.PrefilteredMipCount = 5;
		map.PrefilterSampleCount = 64;
		map.IrradianceFaceSize = 16;
		constexpr std::uint32_t lutSize = 32;
		constexpr std::uint32_t lutSamples = 256;
		const std::uint32_t sourceMips = Env::EnvironmentSourceMipCount(map.SourceSize);

		Env::ProceduralSky moved;
		moved.SunDirection = { -0.6f, 0.4f, -0.5f };
		moved.SunSharpness = 24.0f;
		moved.Intensity = 2.0f;
		const std::array<Env::ProceduralSky, 3> skies{ Env::ProceduralSky{}, Env::ProceduralSky::Uniform(0.75f), moved };
		for (std::size_t frame = 0; frame < skies.size(); ++frame)
		{
			const auto& sky = skies[frame];
			const bool furnace = frame == 1;
			RenderGraph graph;
			const auto environment = programs.Builder->Record(graph, sky, map);
			const auto lut = programs.Builder->RecordBrdfLut(graph, lutSize, lutSamples);
			const auto sourceReadback = Smoke::AddCubeReadback(graph, environment.Source, map.SourceSize, sourceMips);
			const auto prefilteredReadback =
				Smoke::AddCubeReadback(graph, environment.Prefiltered, map.PrefilteredSize, map.PrefilteredMipCount);
			const auto irradianceReadback =
				AddBufferReadback(graph, "Irradiance readback", environment.Irradiance, 0, EnvironmentIrradianceBindings::OutputBytes);
			const auto lutReadback = AddTextureReadback(graph, "LUT readback", lut, { 0, {}, {}, { lutSize, lutSize, 1 } });
			executor.Execute(graph.Compile());
			executor.Wait();

			const auto source = Smoke::ReadCube(executor, sourceReadback);
			const auto prefiltered = Smoke::ReadCube(executor, prefilteredReadback);
			const auto irradiance = Smoke::ReadIrradiance(executor, irradianceReadback);
			const auto gpuLut = Smoke::ReadImage(executor, lutReadback, lutSize, lutSize);

			// 1. Sky: every mip-0 texel is the sky at its center (half-float storage).
			float worstSky = 0.0f;
			for (std::uint32_t face = 0; face < 6; ++face)
			{
				for (std::uint32_t y = 0; y < map.SourceSize; ++y)
				{
					for (std::uint32_t x = 0; x < map.SourceSize; ++x)
					{
						const auto expected = sky.Evaluate(Env::CubeTexelDirection(face, x, y, map.SourceSize));
						const auto& actual = source.Texel(0, face, x, y);
						for (int c = 0; c < 3; ++c)
						{
							worstSky = std::max(worstSky, Smoke::RelativeError(actual[c], expected[c], 1.0e-3f));
						}
						SWIM_CHECK_EQUAL(actual[3], 1.0f);
					}
				}
			}
			std::printf("             [environment %zu] sky worst relative error %.2e\n", frame, worstSky);
			SWIM_CHECK(worstSky < 3.0e-3f);

			// 2. Mips: the box filter of the GPU mip above.
			float worstMip = 0.0f;
			for (std::uint32_t mip = 1; mip < sourceMips; ++mip)
			{
				const std::uint32_t size = source.GetMipSize(mip);
				for (std::uint32_t face = 0; face < 6; ++face)
				{
					for (std::uint32_t y = 0; y < size; ++y)
					{
						for (std::uint32_t x = 0; x < size; ++x)
						{
							for (int c = 0; c < 4; ++c)
							{
								const float expected = 0.25f *
									(source.Texel(mip - 1, face, 2 * x, 2 * y)[c] + source.Texel(mip - 1, face, 2 * x + 1, 2 * y)[c] +
										source.Texel(mip - 1, face, 2 * x, 2 * y + 1)[c] +
										source.Texel(mip - 1, face, 2 * x + 1, 2 * y + 1)[c]);
								worstMip = std::max(worstMip, Smoke::RelativeError(source.Texel(mip, face, x, y)[c], expected, 1.0e-3f));
							}
						}
					}
				}
			}
			std::printf("             [environment %zu] mip worst relative error %.2e\n", frame, worstMip);
			SWIM_CHECK(worstMip < 2.0e-3f);

			// 3. Prefilter: the CPU definition over the GPU's source cube.
			const auto expectedPrefiltered =
				Env::BuildPrefilteredCube(source, map.PrefilteredSize, map.PrefilteredMipCount, map.PrefilterSampleCount);
			float worstPrefilter = 0.0f;
			double sumPrefilter = 0.0;
			std::size_t countPrefilter = 0;
			for (std::uint32_t mip = 0; mip < map.PrefilteredMipCount; ++mip)
			{
				const std::uint32_t size = prefiltered.GetMipSize(mip);
				float worstInMip = 0.0f;
				for (std::uint32_t face = 0; face < 6; ++face)
				{
					for (std::uint32_t y = 0; y < size; ++y)
					{
						for (std::uint32_t x = 0; x < size; ++x)
						{
							const auto& actual = prefiltered.Texel(mip, face, x, y);
							const auto& expected = expectedPrefiltered.Texel(mip, face, x, y);
							for (int c = 0; c < 3; ++c)
							{
								const float error = Smoke::RelativeError(actual[c], expected[c], 0.01f);
								worstInMip = std::max(worstInMip, error);
								sumPrefilter += error;
								++countPrefilter;
								if (furnace)
								{
									SWIM_CHECK(std::abs(actual[c] - 0.75f) < 1.0e-3f);
								}
							}
						}
					}
				}
				std::printf("             [environment %zu] prefiltered mip %u (roughness %.2f) worst relative error %.2e\n", frame, mip,
					Env::PrefilterMipRoughness(mip, map.PrefilteredMipCount), worstInMip);
				worstPrefilter = std::max(worstPrefilter, worstInMip);
			}
			const double meanPrefilter = sumPrefilter / double(countPrefilter);
			std::printf("             [environment %zu] prefiltered mean relative error %.2e\n", frame, meanPrefilter);
			SWIM_CHECK(worstPrefilter < 0.05f);
			SWIM_CHECK(meanPrefilter < 5.0e-3);

			// 4. Irradiance: the CPU projection of the GPU's mip; then evaluated.
			std::uint32_t irradianceMip = 0;
			while ((map.SourceSize >> irradianceMip) > map.IrradianceFaceSize)
			{
				++irradianceMip;
			}
			const auto expectedIrradiance = Env::ProjectIrradianceSh(source, irradianceMip);
			const float scale = std::max({ std::abs(expectedIrradiance.Coefficients[0][0]), std::abs(expectedIrradiance.Coefficients[0][1]),
				std::abs(expectedIrradiance.Coefficients[0][2]) });
			for (std::uint32_t i = 0; i < 9; ++i)
			{
				for (int c = 0; c < 3; ++c)
				{
					SWIM_CHECK(std::abs(irradiance.Coefficients[i][c] - expectedIrradiance.Coefficients[i][c]) < 5.0e-3f * scale);
				}
			}
			std::mt19937 random(61 + std::uint32_t(frame));
			std::normal_distribution<float> normal(0.0f, 1.0f);
			for (int i = 0; i < 64; ++i)
			{
				const auto n = Env::Normalize({ normal(random), normal(random), normal(random) });
				const auto actual = irradiance.Evaluate(n);
				const auto expected = expectedIrradiance.Evaluate(n);
				for (int c = 0; c < 3; ++c)
				{
					SWIM_CHECK(Smoke::RelativeError(actual[c], expected[c], 0.01f * scale) < 1.0e-2f);
					if (furnace)
					{
						SWIM_CHECK(std::abs(actual[c] - 0.75f) < 5.0e-3f);
					}
				}
			}

			// 5. BRDF LUT.
			const auto expectedLut = Env::BuildBrdfLut(lutSize, lutSamples);
			float worstLut = 0.0f;
			for (std::size_t i = 0; i < expectedLut.Texels.size(); ++i)
			{
				for (int c = 0; c < 2; ++c)
				{
					worstLut = std::max(worstLut, std::abs(gpuLut.Texels[i][c] - expectedLut.Texels[i][c]));
				}
				SWIM_CHECK_EQUAL(gpuLut.Texels[i][3], 1.0f);
			}
			std::printf("             [environment %zu] BRDF LUT worst absolute error %.2e\n", frame, worstLut);
			SWIM_CHECK(worstLut < 3.0e-3f);
		}
		executor.Trim();
#endif
	}

	[[maybe_unused]] const bool registered = []
	{
		const char* enabled = std::getenv("SWIM_RUN_RHI_SMOKE");
		if (enabled != nullptr && std::string_view(enabled) == "1")
		{
			Swim::Testing::TestRegistry::Get().Add({ "RHI.Vulkan.Smoke", "EnvironmentMapsMatchTheirCpuReferences", SWIM_TEST_LOCATION,
				+[]
				{
					Swim::Testing::RunValidatedVulkanSmoke(&RunEnvironmentSmoke);
				} });
		}
		return true;
	}();
} // namespace
