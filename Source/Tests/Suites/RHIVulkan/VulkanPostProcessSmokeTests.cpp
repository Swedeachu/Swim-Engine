#include "Engine/Platform/PlatformSystem.h"
#include "Engine/Systems/Renderer/PostProcess/PostProcessReference.h"
#include "Engine/Systems/Renderer/PostProcess/PostProcessor.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/VulkanRhiBackend.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraphExecutor.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraphTransfers.h"
#include "Tests/Fixtures/VulkanSmokeDiagnostics.h"
#include "Tests/Framework/Test.h"

#if defined(SWIM_POST_HISTOGRAM_SPIRV_PATH) && defined(SWIM_POST_EXPOSURE_SPIRV_PATH) && defined(SWIM_POST_BLOOM_DOWNSAMPLE_SPIRV_PATH) && \
	defined(SWIM_POST_BLOOM_UPSAMPLE_SPIRV_PATH) && defined(SWIM_POST_COMPOSITE_SPIRV_PATH) &&                                             \
	defined(SWIM_POST_COMPOSITE_HDR_SPIRV_PATH) && defined(SWIM_ENVIRONMENT_SKY_SPIRV_PATH) &&                                             \
	defined(SWIM_ENVIRONMENT_DOWNSAMPLE_SPIRV_PATH) && defined(SWIM_ENVIRONMENT_PREFILTER_SPIRV_PATH) &&                                   \
	defined(SWIM_ENVIRONMENT_IRRADIANCE_SPIRV_PATH) && defined(SWIM_ENVIRONMENT_BRDF_LUT_SPIRV_PATH)
#include "Tests/Fixtures/VulkanEnvironmentFixture.h"
#define SWIM_POST_PROCESS_SMOKE_AVAILABLE 1
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
#ifdef SWIM_POST_PROCESS_SMOKE_AVAILABLE
	namespace Smoke = Swim::Testing::EnvironmentSmoke;
	namespace Pp = Swim::Render::Post;

	// Binary16 bits of a value that is already exactly representable (Pp::RoundToHalf output).
	std::uint16_t HalfBits(float value)
	{
		const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
		const std::uint16_t sign = std::uint16_t((bits >> 16) & 0x8000u);
		const float magnitude = std::abs(value);
		if (magnitude == 0.0f)
		{
			return sign;
		}
		if (std::isinf(magnitude))
		{
			return std::uint16_t(sign | 0x7c00u);
		}
		if (magnitude < std::ldexp(1.0f, -14))
		{
			return std::uint16_t(sign | std::uint16_t(magnitude * 16777216.0f));
		}
		const std::uint32_t exponent = ((bits >> 23) & 0xffu) - 127u + 15u;
		return std::uint16_t(sign | (exponent << 10) | ((bits >> 13) & 0x3ffu));
	}

	// A wide-range HDR test image: a sky gradient over ten stops, colored bands, a black
	// strip, and emissive disks up to a few hundred times brighter than the sky.
	Pp::Image MakeScene(std::uint32_t width, std::uint32_t height, float scale)
	{
		Pp::Image image(width, height);
		const std::array<std::array<float, 5>, 4> disks{ { { 0.2f, 0.3f, 0.06f, 400.0f, 0.0f }, { 0.7f, 0.25f, 0.04f, 60.0f, 1.0f },
			{ 0.5f, 0.7f, 0.1f, 8.0f, 2.0f }, { 0.85f, 0.8f, 0.02f, 1500.0f, 0.0f } } };
		for (std::uint32_t y = 0; y < height; ++y)
		{
			for (std::uint32_t x = 0; x < width; ++x)
			{
				const float u = (float(x) + 0.5f) / float(width);
				const float v = (float(y) + 0.5f) / float(height);
				float r = 0.01f * std::exp2(10.0f * u) * (0.6f + 0.4f * v);
				float g = r * (0.8f + 0.2f * std::sin(20.0f * v));
				float b = r * (0.6f + 0.4f * u);
				if (v > 0.9f && u < 0.3f)
				{
					r = g = b = 0.0f; // Black: the histogram's ignored bin.
				}
				for (const auto& disk : disks)
				{
					const float dx = (u - disk[0]) * float(width) / float(height);
					const float dy = v - disk[1];
					if (dx * dx + dy * dy < disk[2] * disk[2])
					{
						const std::array<float, 3> tint = disk[4] == 0.0f ? std::array<float, 3>{ 1.0f, 0.9f, 0.7f }
							: disk[4] == 1.0f							  ? std::array<float, 3>{ 0.3f, 0.6f, 1.0f }
																		  : std::array<float, 3>{ 1.0f, 0.2f, 0.1f };
						r = disk[3] * tint[0];
						g = disk[3] * tint[1];
						b = disk[3] * tint[2];
					}
				}
				image.At(x, y) = Pp::RoundToHalf(Pp::Float4{ r * scale, g * scale, b * scale, 1.0f });
			}
		}
		return image;
	}

	Swim::Render::PostProgram ProgramOf(const Smoke::ComputeProgram& program)
	{
		return { program.Pipeline.get(), program.Layout.get(), program.Space };
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

	bool Close(float actual, float expected, float relative, float absolute)
	{
		return std::abs(actual - expected) <= absolute + relative * std::abs(expected);
	}
#endif

	// Critical-path items 73 and 74 on a real device. A wide-range HDR image goes through
	// PostProcessor (luminance histogram, auto or manual exposure with adaptation, the
	// Karis-averaged bloom chains, grading, four tone mappers and three output encodings)
	// and every stage is read back and compared with Post:: (PostProcessReference.h):
	//  - the histogram bin by bin, and the exposure state recomputed from the GPU's histogram;
	//  - each bloom level recomputed from the GPU's own input level;
	//  - every output texel: SDR within one 8-bit step, HDR within half precision;
	//  - adaptation between frames toward a brighter image, then a 1080p timing frame.
	void RunPostProcessSmoke(const Swim::Rhi::GraphicsSystemDesc& graphicsDesc)
	{
#ifndef SWIM_POST_PROCESS_SMOKE_AVAILABLE
		SWIM_REQUIRE_MESSAGE(false, "Post-process smoke requires generated Slang artifacts");
#else
		using namespace Swim;
		using namespace Swim::Render;

		Platform::PlatformSystem platform;
		SWIM_REQUIRE_MESSAGE(platform.Initialize(), "Post-process smoke requires working SDL Vulkan platform services");
		auto graphics = RhiVulkan::CreateGraphicsSystem(graphicsDesc);
		SWIM_REQUIRE(graphics);
		Testing::RequireVulkanSmokeValidation(*graphics, graphicsDesc.Checks);
		auto device = graphics->GetAdapter(0).CreateDevice();
		SWIM_REQUIRE(device);

		const auto histogram =
			Smoke::MakeCompute(*device, SWIM_POST_HISTOGRAM_SPIRV_PATH, SWIM_POST_HISTOGRAM_REFLECTION_PATH, "Post histogram");
		const auto exposure =
			Smoke::MakeCompute(*device, SWIM_POST_EXPOSURE_SPIRV_PATH, SWIM_POST_EXPOSURE_REFLECTION_PATH, "Post exposure");
		const auto down = Smoke::MakeCompute(
			*device, SWIM_POST_BLOOM_DOWNSAMPLE_SPIRV_PATH, SWIM_POST_BLOOM_DOWNSAMPLE_REFLECTION_PATH, "Post bloom downsample");
		const auto up = Smoke::MakeCompute(
			*device, SWIM_POST_BLOOM_UPSAMPLE_SPIRV_PATH, SWIM_POST_BLOOM_UPSAMPLE_REFLECTION_PATH, "Post bloom upsample");
		const auto composite =
			Smoke::MakeCompute(*device, SWIM_POST_COMPOSITE_SPIRV_PATH, SWIM_POST_COMPOSITE_REFLECTION_PATH, "Post composite");
		const auto compositeHdr =
			Smoke::MakeCompute(*device, SWIM_POST_COMPOSITE_HDR_SPIRV_PATH, SWIM_POST_COMPOSITE_HDR_REFLECTION_PATH, "Post composite HDR");
		PostProcessorDesc desc;
		desc.Histogram = ProgramOf(histogram);
		desc.Exposure = ProgramOf(exposure);
		desc.BloomDownsample = ProgramOf(down);
		desc.BloomUpsample = ProgramOf(up);
		desc.Composite = ProgramOf(composite);
		desc.CompositeHdr = ProgramOf(compositeHdr);
		desc.DebugName = "Post";
		PostProcessor post(*device, desc);
		RenderGraphExecutor executor(*device);

		struct FrameSpec
		{
			const char* Name;
			std::uint32_t Width;
			std::uint32_t Height;
			float Scale; // Scene brightness.
			PostProcessSettings Settings;
			float DeltaTime;
			bool Compare; // False: timing only.
		};

		std::optional<GpuExposureState> previousState;
		const auto frame = [&](const FrameSpec& spec)
		{
			const auto image = MakeScene(spec.Width, spec.Height, spec.Scale);
			std::vector<std::uint16_t> halves;
			halves.reserve(image.Texels.size() * 4);
			for (const auto& texel : image.Texels)
			{
				for (const float v : texel)
				{
					halves.push_back(HalfBits(v));
				}
			}
			const bool hdr = IsHdrEncoding(spec.Settings.Output.Encoding);
			RenderGraph graph;
			Rhi::TextureDesc sourceDesc;
			sourceDesc.Extent = { spec.Width, spec.Height, 1 };
			sourceDesc.PixelFormat = Rhi::Format::RGBA16Float;
			sourceDesc.Usage = Rhi::TextureUsage::Sampled | Rhi::TextureUsage::TransferDestination;
			sourceDesc.DebugName = "Post source";
			const auto source = graph.CreateTexture(sourceDesc);
			AddTextureUpload(
				graph, "Post source upload", std::as_bytes(std::span(halves)), source, { 0, {}, {}, { spec.Width, spec.Height, 1 } });
			Rhi::TextureDesc outputDesc = sourceDesc;
			outputDesc.PixelFormat = hdr ? Rhi::Format::RGBA16Float : Rhi::Format::RGBA8Unorm;
			outputDesc.Usage = Rhi::TextureUsage::Storage | Rhi::TextureUsage::TransferSource;
			outputDesc.DebugName = "Post output";
			const auto output = graph.CreateTexture(outputDesc);
			const bool reset = !previousState.has_value();
			const auto resources = post.Record(graph, { source, output, spec.Settings, spec.DeltaTime });
			SWIM_CHECK_EQUAL(resources.ExposureReset, reset);
			const Rhi::BufferTextureCopyRegion whole{ 0, {}, {}, { spec.Width, spec.Height, 1 } };
			const auto outputReadback = AddTextureReadback(graph, "Output", output, whole);
			const auto stateReadback = AddBufferReadback(graph, "Exposure state", resources.ExposureState, 0, sizeof(GpuExposureState));
			std::optional<GraphReadback> histogramReadback;
			if (resources.Histogram)
			{
				histogramReadback = AddBufferReadback(graph, "Histogram", *resources.Histogram, 0, PostHistogramBins * 4);
			}
			std::vector<GraphReadback> downReadbacks, upReadbacks;
			if (spec.Compare)
			{
				for (std::uint32_t i = 0; i < resources.BloomLevels; ++i)
				{
					const Rhi::BufferTextureCopyRegion level{ 0, {}, {}, { spec.Width >> (i + 1), spec.Height >> (i + 1), 1 } };
					downReadbacks.push_back(AddTextureReadback(graph, "Bloom down", resources.BloomDown[i], level));
					if (i + 1 < resources.BloomLevels)
					{
						upReadbacks.push_back(AddTextureReadback(graph, "Bloom up", resources.BloomUp[i], level));
					}
				}
			}
			executor.Execute(graph.Compile());
			const auto timings = executor.ReadTimings();
			const auto read = [&](const GraphReadback& readback, auto& target)
			{
				SWIM_REQUIRE(
					executor.TryReadback(readback.Buffer, std::as_writable_bytes(std::span(target))) == Rhi::ReadbackStatus::Ready);
			};
			std::array<GpuExposureState, 1> state{};
			read(stateReadback, state);
			const auto readLevel = [&](const GraphReadback& readback, std::uint32_t width, std::uint32_t height)
			{
				std::vector<std::uint16_t> raw(std::size_t(width) * height * 4);
				read(readback, raw);
				Pp::Image level(width, height);
				for (std::size_t i = 0; i < level.Texels.size(); ++i)
				{
					for (int c = 0; c < 4; ++c)
					{
						level.Texels[i][c] = Smoke::HalfToFloat(raw[i * 4 + c]);
					}
				}
				return level;
			};

			// 1. Histogram and exposure.
			Pp::Histogram gpuBins{};
			std::uint32_t binDifference = 0;
			const auto& exposureSettings = spec.Settings.Exposure;
			if (histogramReadback)
			{
				read(*histogramReadback, gpuBins);
				const auto cpuBins = Pp::BuildHistogram(image, exposureSettings);
				std::uint64_t total = 0;
				for (std::uint32_t b = 0; b < PostHistogramBins; ++b)
				{
					total += gpuBins[b];
					binDifference += std::uint32_t(std::abs(std::int64_t(gpuBins[b]) - std::int64_t(cpuBins[b])));
				}
				SWIM_CHECK_EQUAL(total, std::uint64_t(spec.Width) * spec.Height);
				SWIM_CHECK(binDifference <= spec.Width * spec.Height / 1000); // Only log2 ulps at bin edges.
			}
			const auto cpuState =
				Pp::UpdateExposure(gpuBins, exposureSettings, previousState.value_or(GpuExposureState{}), spec.DeltaTime, reset);
			SWIM_CHECK_EQUAL(state[0].Valid, 1u);
			SWIM_CHECK(Close(state[0].Ev100, cpuState.Ev100, 1.0e-4f, 1.0e-3f));
			SWIM_CHECK(Close(state[0].Exposure, cpuState.Exposure, 1.0e-3f, 0.0f));
			SWIM_CHECK(Close(state[0].AverageLog2Luminance, cpuState.AverageLog2Luminance, 1.0e-4f, 1.0e-3f));
			const float gpuExposure = state[0].Exposure;

			std::uint32_t bloomTexels = 0, bloomMismatches = 0, compared = 0, mismatches = 0, maxStep = 0;
			float worst = 0.0f;
			double sumEncoded = 0.0;
			if (spec.Compare)
			{
				// 2. Bloom, each level from the GPU's own input.
				std::vector<Pp::Image> gpuDown, gpuUp;
				for (std::uint32_t i = 0; i < resources.BloomLevels; ++i)
				{
					gpuDown.push_back(readLevel(downReadbacks[i], spec.Width >> (i + 1), spec.Height >> (i + 1)));
				}
				for (std::uint32_t i = 0; i < upReadbacks.size(); ++i)
				{
					gpuUp.push_back(readLevel(upReadbacks[i], spec.Width >> (i + 1), spec.Height >> (i + 1)));
				}
				const auto compareLevel = [&](const Pp::Image& gpu, const Pp::Image& cpu)
				{
					for (std::size_t t = 0; t < gpu.Texels.size(); ++t)
					{
						for (int c = 0; c < 3; ++c)
						{
							++bloomTexels;
							bloomMismatches += Close(gpu.Texels[t][c], cpu.Texels[t][c], 4.0e-3f, 1.0e-4f) ? 0u : 1u;
						}
					}
				};
				const auto& b = spec.Settings.Bloom;
				for (std::uint32_t i = 0; i < gpuDown.size(); ++i)
				{
					const auto& from = i == 0 ? image : gpuDown[i - 1];
					compareLevel(gpuDown[i],
						Pp::BloomDownsample(from, spec.Width >> (i + 1), spec.Height >> (i + 1), i == 0, gpuExposure, b.Threshold, b.Knee));
				}
				for (std::uint32_t i = 0; i < gpuUp.size(); ++i)
				{
					const auto& low = i + 2 == gpuDown.size() ? gpuDown.back() : gpuUp[i + 1];
					compareLevel(gpuUp[i], Pp::BloomUpsample(low, gpuDown[i]));
				}
				SWIM_CHECK(bloomMismatches <= bloomTexels / 1000);

				// 3. Output, from the GPU's exposure and bloom.
				const auto params = Pp::BuildPostParams(spec.Settings, resources.BloomLevels);
				const Pp::Image* bloom = gpuUp.empty() ? (gpuDown.empty() ? nullptr : &gpuDown[0]) : &gpuUp[0];
				std::vector<float> actual(std::size_t(spec.Width) * spec.Height * 4);
				if (hdr)
				{
					const auto gpu = readLevel(outputReadback, spec.Width, spec.Height);
					for (std::size_t i = 0; i < gpu.Texels.size(); ++i)
					{
						for (int c = 0; c < 4; ++c)
						{
							actual[i * 4 + c] = gpu.Texels[i][c];
						}
					}
				}
				else
				{
					std::vector<std::uint8_t> bytes(actual.size());
					read(outputReadback, bytes);
					for (std::size_t i = 0; i < bytes.size(); ++i)
					{
						actual[i] = float(bytes[i]) / 255.0f;
					}
				}
				for (std::uint32_t y = 0; y < spec.Height; ++y)
				{
					for (std::uint32_t x = 0; x < spec.Width; ++x)
					{
						const Pp::Float3 bloomValue =
							bloom ? Pp::TentUpsample(*bloom, x, y, spec.Width, spec.Height) : Pp::Float3{ 0, 0, 0 };
						const auto expected = Pp::CompositeTexel(params, gpuExposure, image.At(x, y), bloomValue, x, y);
						const std::size_t index = (std::size_t(y) * spec.Width + x) * 4;
						for (int c = 0; c < 4; ++c)
						{
							++compared;
							const float a = actual[index + c];
							if (hdr)
							{
								const float e = Pp::RoundToHalf(expected[c]);
								const bool ok = Close(a, e, 4.0e-3f, 2.0e-4f);
								mismatches += ok ? 0u : 1u;
								worst = std::max(worst, std::abs(a - e) / std::max(std::abs(e), 1.0e-3f));
							}
							else
							{
								const auto step = std::uint32_t(std::abs(std::lround(a * 255.0f) - std::lround(expected[c] * 255.0f)));
								maxStep = std::max(maxStep, step);
								mismatches += step > 1 ? 1u : 0u;
							}
							if (c < 3)
							{
								sumEncoded += a;
							}
						}
					}
				}
				SWIM_CHECK_EQUAL(mismatches, 0u);
			}
			std::printf(
				"             [post %s] %ux%u: EV100 %.3f (CPU %.3f), exposure %.4g, avg log2 L %.3f; histogram bin difference %u; "
				"%u bloom levels, %u/%u bloom mismatches; %u output values, %u mismatches, max SDR step %u, worst HDR relative %.2e; "
				"mean encoded %.3f\n",
				spec.Name, spec.Width, spec.Height, double(state[0].Ev100), double(cpuState.Ev100), double(state[0].Exposure),
				double(state[0].AverageLog2Luminance), binDifference, resources.BloomLevels, bloomMismatches, bloomTexels, compared,
				mismatches, maxStep, double(worst), compared ? sumEncoded / (double(compared) * 0.75) : 0.0);
			bool measured = false;
			const double compositeMs = PassMilliseconds(timings, "Post composite", &measured);
			if (measured)
			{
				std::printf("             [post %s] GPU: histogram %.3f ms, exposure %.3f ms, bloom %.3f ms, composite %.3f ms\n",
					spec.Name, PassMilliseconds(timings, "Post histogram"), PassMilliseconds(timings, "Post exposure"),
					PassMilliseconds(timings, "Post bloom"), compositeMs);
			}
			previousState = state[0];
			return state[0];
		};

		constexpr std::uint32_t width = 320;
		constexpr std::uint32_t height = 180;
		PostProcessSettings automatic;
		const auto first = frame({ "auto sdr", width, height, 1.0f, automatic, 1.0f / 60.0f, true });
		// Brighter scene: the exposure adapts toward it but does not get there in 0.1 s.
		const auto adapting = frame({ "adapting", width, height, 16.0f, automatic, 0.1f, true });
		SWIM_CHECK(adapting.Ev100 > first.Ev100 && adapting.Ev100 < first.Ev100 + 4.0f);

		PostProcessSettings graded;
		graded.Exposure.Mode = ExposureMode::Manual;
		graded.Exposure.ManualEv100 = 4.0f;
		graded.Bloom.MipCount = 3;
		graded.Bloom.Intensity = 0.1f;
		graded.Grading.Temperature = 30.0f;
		graded.Grading.Tint = -10.0f;
		graded.Grading.Contrast = 1.2f;
		graded.Grading.Slope = { 1.05f, 1.0f, 0.95f };
		graded.Grading.Offset = { 0.0f, 0.005f, 0.01f };
		graded.Grading.Power = { 1.0f, 1.1f, 0.9f };
		graded.Grading.Saturation = 1.3f;
		graded.ToneMap.Operator = ToneMapper::Aces;
		graded.Output.Dither = false;
		const auto manual = frame({ "graded aces", width, height, 1.0f, graded, 1.0f / 60.0f, true });
		SWIM_CHECK_EQUAL(manual.Ev100, 4.0f);

		PostProcessSettings hdr10;
		hdr10.Output.Encoding = OutputEncoding::Hdr10;
		post.ResetExposureHistory();
		previousState.reset();
		frame({ "hdr10", width, height, 1.0f, hdr10, 1.0f / 60.0f, true });

		PostProcessSettings scRgb;
		scRgb.Bloom.Enabled = false;
		scRgb.ToneMap.Operator = ToneMapper::Reinhard;
		scRgb.Output.Encoding = OutputEncoding::ScRgb;
		frame({ "scrgb reinhard", width, height, 1.0f, scRgb, 1.0f / 60.0f, true });

		frame({ "1080p timing", 1920, 1080, 1.0f, automatic, 1.0f / 60.0f, false });
		executor.Trim();
#endif
	}

	[[maybe_unused]] const bool registered = []
	{
		const char* enabled = std::getenv("SWIM_RUN_RHI_SMOKE");
		if (enabled != nullptr && std::string_view(enabled) == "1")
		{
			Swim::Testing::TestRegistry::Get().Add({ "RHI.Vulkan.Smoke", "PostProcessMatchesTheCpuReference", SWIM_TEST_LOCATION,
				+[]
				{
					Swim::Testing::RunValidatedVulkanSmoke(&RunPostProcessSmoke);
				} });
		}
		return true;
	}();
} // namespace
