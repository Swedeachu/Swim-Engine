#include "Engine/Platform/PlatformSystem.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/VulkanRhiBackend.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraphExecutor.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraphTransfers.h"
#include "Engine/Systems/Renderer/Temporal/TemporalAntiAliasing.h"
#include "Engine/Systems/Renderer/Temporal/TemporalReference.h"
#include "Tests/Fixtures/VulkanSmokeDiagnostics.h"
#include "Tests/Framework/Test.h"

#if defined(SWIM_TEMPORAL_RESOLVE_SPIRV_PATH) && defined(SWIM_ENVIRONMENT_SKY_SPIRV_PATH) &&                                               \
	defined(SWIM_ENVIRONMENT_DOWNSAMPLE_SPIRV_PATH) && defined(SWIM_ENVIRONMENT_PREFILTER_SPIRV_PATH) &&                                   \
	defined(SWIM_ENVIRONMENT_IRRADIANCE_SPIRV_PATH) && defined(SWIM_ENVIRONMENT_BRDF_LUT_SPIRV_PATH)
#include "Tests/Fixtures/VulkanEnvironmentFixture.h"
#define SWIM_TEMPORAL_SMOKE_AVAILABLE 1
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
#ifdef SWIM_TEMPORAL_SMOKE_AVAILABLE
	namespace Smoke = Swim::Testing::EnvironmentSmoke;
	namespace Ta = Swim::Render::Temporal;

	// IEEE binary16 bits of value, rounded to nearest even.
	std::uint16_t FloatToHalf(float value)
	{
		const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
		const auto sign = std::uint16_t((bits >> 16) & 0x8000u);
		const std::uint32_t magnitude = bits & 0x7fffffffu;
		if (magnitude >= 0x47800000u) // >= 65536, infinity or NaN.
		{
			return std::uint16_t(sign | (magnitude > 0x7f800000u ? 0x7e00u : 0x7c00u));
		}
		if (magnitude < 0x38800000u) // Below 2^-14: subnormal (value * 2^24 is exact in float).
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

	// One synthetic frame, already rounded to what the GPU textures hold.
	struct Inputs
	{
		Ta::ColorImage Color;
		Ta::DepthImage Depth;
		Ta::VelocityImage Velocity;
		std::vector<std::uint16_t> ColorHalves;
		std::vector<std::uint16_t> VelocityHalves;
	};

	// A scene that aliases and moves: an HDR checkerboard (8-pixel squares, a gradient
	// over five stops and a strip of negative values) panning left by 1.5 pixels per
	// frame, and a bright rectangle moving diagonally in front of it. Each pixel samples
	// the scene at its centre minus the jitter; the motion vectors are exact.
	Inputs MakeInputs(std::uint32_t width, std::uint32_t height, int frame, const Ta::Float2& jitter)
	{
		constexpr float pan = 1.5f;
		const std::array<float, 2> rectangleSpeed{ 2.25f, 0.75f };
		Inputs inputs{ Ta::ColorImage(width, height), Ta::DepthImage(width, height), Ta::VelocityImage(width, height), {}, {} };
		const float left = float(width) * 0.2f + rectangleSpeed[0] * float(frame);
		const float top = float(height) * 0.3f + rectangleSpeed[1] * float(frame);
		for (std::uint32_t y = 0; y < height; ++y)
		{
			for (std::uint32_t x = 0; x < width; ++x)
			{
				const float sx = float(x) + 0.5f - jitter[0];
				const float sy = float(y) + 0.5f - jitter[1];
				Ta::Float4 color{};
				float depth = 0.05f;
				Ta::Float2 motion{ -pan / float(width), 0.0f };
				if (sx >= left && sx < left + float(width) * 0.15f && sy >= top && sy < top + float(height) * 0.2f)
				{
					color = { 40.0f, 24.0f, 6.0f, 1.0f };
					depth = 0.6f;
					motion = { rectangleSpeed[0] / float(width), rectangleSpeed[1] / float(height) };
				}
				else
				{
					const float px = sx + pan * float(frame); // Scene coordinates.
					const bool dark = (int(std::floor(px / 8.0f)) + int(std::floor(sy / 8.0f))) % 2 != 0;
					const float level = 0.05f * std::exp2(5.0f * float(x) / float(width));
					color = dark ? Ta::Float4{ level * 0.1f, level * 0.12f, level * 0.15f, 1.0f }
								 : Ta::Float4{ level, level * 0.9f, level * 0.7f, 1.0f };
					if (y >= height - 4)
					{
						color = { -0.5f, 0.25f, -1.0f, 1.0f }; // Sanitized to zero by both sides.
					}
				}
				for (int c = 0; c < 4; ++c)
				{
					inputs.ColorHalves.push_back(FloatToHalf(color[c]));
					inputs.Color.At(x, y)[c] = Smoke::HalfToFloat(inputs.ColorHalves.back());
				}
				for (int c = 0; c < 2; ++c)
				{
					inputs.VelocityHalves.push_back(FloatToHalf(motion[c]));
					inputs.Velocity.At(x, y)[c] = Smoke::HalfToFloat(inputs.VelocityHalves.back());
				}
				inputs.Depth.At(x, y) = depth;
			}
		}
		return inputs;
	}

	bool Close(float actual, float expected, float relative, float absolute)
	{
		return std::abs(actual - expected) <= absolute + relative * std::abs(expected);
	}
#endif

	// Critical-path item 75 on a real device. Jittered, moving HDR frames go through
	// TemporalAntiAliasing (dilated motion vectors, bilinear history reprojection,
	// YCoCg variance clipping, luminance-weighted blend) and every output texel is
	// compared with Temporal::ResolveTexel over the same inputs and the GPU's own
	// previous output. Covers the first frame, history, a reset, a resize with R32Float
	// depth, and a 1080p timing frame.
	void RunTemporalSmoke(const Swim::Rhi::GraphicsSystemDesc& graphicsDesc)
	{
#ifndef SWIM_TEMPORAL_SMOKE_AVAILABLE
		SWIM_REQUIRE_MESSAGE(false, "TAA smoke requires generated Slang artifacts");
#else
		using namespace Swim;
		using namespace Swim::Render;

		Platform::PlatformSystem platform;
		SWIM_REQUIRE_MESSAGE(platform.Initialize(), "TAA smoke requires working SDL Vulkan platform services");
		auto graphics = RhiVulkan::CreateGraphicsSystem(graphicsDesc);
		SWIM_REQUIRE(graphics);
		Testing::RequireVulkanSmokeValidation(*graphics, graphicsDesc.Checks);
		auto device = graphics->GetAdapter(0).CreateDevice();
		SWIM_REQUIRE(device);

		const auto resolve =
			Smoke::MakeCompute(*device, SWIM_TEMPORAL_RESOLVE_SPIRV_PATH, SWIM_TEMPORAL_RESOLVE_REFLECTION_PATH, "TAA resolve");
		TemporalAntiAliasingDesc desc;
		desc.Resolve = { resolve.Pipeline.get(), resolve.Layout.get(), resolve.Space };
		desc.DebugName = "TAA";
		TemporalAntiAliasing taa(*device, desc);
		RenderGraphExecutor executor(*device);
		TemporalSettings settings;

		struct FrameSpec
		{
			const char* Name;
			std::uint32_t Width;
			std::uint32_t Height;
			Rhi::Format DepthFormat;
			bool ExpectHistory;
			bool Compare; // False: timing only.
		};

		std::optional<Ta::ColorImage> previous;
		int sceneFrame = 0;
		const auto frame = [&](const FrameSpec& spec)
		{
			const auto jitter = taa.GetJitterPixels(settings);
			const auto inputs = MakeInputs(spec.Width, spec.Height, sceneFrame++, jitter);
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
			TemporalFrame temporal;
			temporal.Settings = settings;
			temporal.Color = texture(Rhi::Format::RGBA16Float, Rhi::TextureUsage::ColorAttachment, "TAA color");
			temporal.Depth = texture(spec.DepthFormat,
				spec.DepthFormat == Rhi::Format::D32Float ? Rhi::TextureUsage::DepthStencilAttachment : Rhi::TextureUsage::ColorAttachment,
				"TAA depth");
			temporal.Velocity = texture(Rhi::Format::RG16Float, Rhi::TextureUsage::ColorAttachment, "TAA velocity");
			AddTextureUpload(graph, "TAA color upload", std::as_bytes(std::span(inputs.ColorHalves)), temporal.Color, whole);
			AddTextureUpload(graph, "TAA depth upload", std::as_bytes(std::span(inputs.Depth.Texels)), temporal.Depth, whole);
			AddTextureUpload(graph, "TAA velocity upload", std::as_bytes(std::span(inputs.VelocityHalves)), temporal.Velocity, whole);
			const auto resources = taa.Record(graph, temporal);
			SWIM_CHECK_EQUAL(resources.HistoryValid, spec.ExpectHistory);
			SWIM_CHECK((resources.JitterPixels == jitter));
			const auto readback = AddTextureReadback(graph, "TAA output", resources.Output, whole);
			executor.Execute(graph.Compile());
			const auto timings = executor.ReadTimings();
			std::vector<std::uint16_t> raw(std::size_t(spec.Width) * spec.Height * 4);
			SWIM_REQUIRE(executor.TryReadback(readback.Buffer, std::as_writable_bytes(std::span(raw))) == Rhi::ReadbackStatus::Ready);
			Ta::ColorImage output(spec.Width, spec.Height);
			for (std::size_t i = 0; i < output.Texels.size(); ++i)
			{
				for (int c = 0; c < 4; ++c)
				{
					output.Texels[i][c] = Smoke::HalfToFloat(raw[i * 4 + c]);
				}
			}

			std::uint32_t compared = 0, mismatches = 0, historyTexels = 0;
			float worst = 0.0f;
			double meanChange = 0.0; // Mean |output - current| / (1 + current): how much history contributed.
			if (spec.Compare)
			{
				const Ta::ColorImage* history = resources.HistoryValid ? &*previous : nullptr;
				for (std::uint32_t y = 0; y < spec.Height; ++y)
				{
					for (std::uint32_t x = 0; x < spec.Width; ++x)
					{
						const auto expected = Ta::ResolveTexel(inputs.Color, inputs.Depth, inputs.Velocity, history, settings, x, y);
						const auto& actual = output.At(x, y);
						bool mismatch = false;
						for (int c = 0; c < 4; ++c)
						{
							++compared;
							mismatch = mismatch || !Close(actual[c], expected[c], 3.0e-3f, 1.0e-4f);
							worst = std::max(worst, std::abs(actual[c] - expected[c]) / std::max(std::abs(expected[c]), 1.0e-2f));
						}
						mismatches += mismatch ? 1u : 0u;
						const auto current = Ta::Sanitize(inputs.Color.At(x, y));
						const float change = std::abs(actual[0] - current[0]) / (1.0f + current[0]);
						meanChange += change;
						historyTexels += change > 1.0e-3f ? 1u : 0u;
					}
				}
				meanChange /= double(spec.Width) * spec.Height;
				SWIM_CHECK(mismatches <= spec.Width * spec.Height / 1000);
				if (resources.HistoryValid)
				{
					SWIM_CHECK(historyTexels > spec.Width * spec.Height / 10); // History visibly contributes...
				}
				else
				{
					SWIM_CHECK_EQUAL(historyTexels, 0u); // ...and is ignored without it.
				}
			}
			std::printf("             [taa %s] %ux%u, jitter (%.3f, %.3f), history %s: %u values, %u texel mismatches, worst relative "
						"%.2e; %u texels from history, mean change %.3e\n",
				spec.Name, spec.Width, spec.Height, double(jitter[0]), double(jitter[1]), resources.HistoryValid ? "yes" : "no", compared,
				mismatches, double(worst), historyTexels, meanChange);
			bool measured = false;
			const double resolveMs = PassMilliseconds(timings, "TAA resolve", &measured);
			if (measured)
			{
				std::printf("             [taa %s] GPU: resolve %.3f ms\n", spec.Name, resolveMs);
			}
			previous = std::move(output);
		};

		constexpr std::uint32_t width = 256;
		constexpr std::uint32_t height = 144;
		frame({ "first", width, height, Rhi::Format::D32Float, false, true });
		for (int i = 0; i < 6; ++i)
		{
			frame({ "moving", width, height, Rhi::Format::D32Float, true, true });
		}
		taa.ResetHistory();
		frame({ "reset", width, height, Rhi::Format::D32Float, false, true });
		frame({ "after reset", width, height, Rhi::Format::D32Float, true, true });
		frame({ "resized r32", 200, 120, Rhi::Format::R32Float, false, true });
		frame({ "resized r32", 200, 120, Rhi::Format::R32Float, true, true });
		SWIM_CHECK_EQUAL(taa.GetFrameIndex(), std::uint64_t(11));
		frame({ "1080p timing", 1920, 1080, Rhi::Format::D32Float, false, false });
		frame({ "1080p timing", 1920, 1080, Rhi::Format::D32Float, true, false });
		executor.Trim();
#endif
	}

	[[maybe_unused]] const bool registered = []
	{
		const char* enabled = std::getenv("SWIM_RUN_RHI_SMOKE");
		if (enabled != nullptr && std::string_view(enabled) == "1")
		{
			Swim::Testing::TestRegistry::Get().Add({ "RHI.Vulkan.Smoke", "TemporalAntiAliasingMatchesTheCpuReference", SWIM_TEST_LOCATION,
				+[]
				{
					Swim::Testing::RunValidatedVulkanSmoke(&RunTemporalSmoke);
				} });
		}
		return true;
	}();
} // namespace
