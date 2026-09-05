#include "Engine/Platform/PlatformSystem.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/VulkanRhiBackend.h"
#include "Engine/Systems/Renderer/RHI/RhiFrameLifetime.h"
#include "Tests/Fixtures/VulkanSmokeDiagnostics.h"
#include "Tests/Framework/Test.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <string_view>

namespace
{

	void RunTimestampSmoke(const Swim::Rhi::GraphicsSystemDesc& graphicsDesc)
	{
		using namespace Swim;
		Platform::PlatformSystem platform;
		SWIM_REQUIRE_MESSAGE(platform.Initialize(), "Timestamp smoke requires a working SDL desktop video driver");
		auto graphics = RhiVulkan::CreateGraphicsSystem(graphicsDesc);
		SWIM_REQUIRE_MESSAGE(graphics, "Timestamp smoke requires the full Swim Vulkan baseline and validation");
		SWIM_REQUIRE(graphics->IsValidationEnabled());
		auto device = graphics->GetAdapter(0).CreateDevice();
		SWIM_REQUIRE(device);
		unsigned testedQueues = 0;
		for (auto type : { Rhi::QueueType::Graphics, Rhi::QueueType::Compute, Rhi::QueueType::Transfer })
		{
			const auto info = device->GetQueue(type).GetTimestampInfo();
			if (!info.IsSupported())
			{
				std::fprintf(stderr, "[Swim timestamps] queue %u: timestamp lifecycle unsupported\n", static_cast<unsigned>(type));
				SWIM_CHECK(!device->CreateQueryPool({ Rhi::QueryType::Timestamp, 2, "unsupported", type }));
				continue;
			}
			++testedQueues;
			auto queries = device->CreateQueryPool({ Rhi::QueryType::Timestamp, 2, "timestamp smoke", type });
			SWIM_REQUIRE(queries);
			constexpr std::size_t byteCount = 4096;
			auto upload = device->CreateBuffer({ byteCount, Rhi::BufferUsage::TransferSource, Rhi::MemoryPreference::CpuToGpu, "timed upload" });
			auto readback = device->CreateBuffer({ byteCount, Rhi::BufferUsage::TransferDestination, Rhi::MemoryPreference::GpuToCpu, "timed readback" });
			SWIM_REQUIRE(upload && readback);
			std::array<std::byte, byteCount> pattern{};
			for (std::size_t index = 0; index < pattern.size(); ++index)
			{
				pattern[index] = static_cast<std::byte>(index % 251);
			}
			upload->Write(0, pattern);
			// Last owner declared: drain in-flight commands before buffers/pool die.
			auto frames = Rhi::FrameContextRing::Create(*device, { type, 2 });
			SWIM_REQUIRE(frames);
			std::array<Rhi::TimestampResult, 2> results{};
			// Execute reset before polling: this must not return stale availability.
			frames->BeginFrame();
			auto& reset = frames->CreateCommandList();
			reset.Begin();
			reset.ResetQueries(*queries, 0, 2);
			reset.End();
			frames->SubmitCurrent();
			frames->Drain();
			SWIM_REQUIRE(queries->ReadTimestamps(0, results) == Rhi::QueryReadStatus::NotReady);
			SWIM_CHECK(!results[0].Available && !results[1].Available);

			for (unsigned frame = 0; frame < 8; ++frame)
			{
				frames->BeginFrame();
				auto& commands = frames->CreateCommandList();
				commands.Begin();
				commands.BeginDebugLabel("Timed copy and query reuse");
				commands.ResetQueries(*queries, 0, 2);
				commands.WriteTimestamp(*queries, 0, Rhi::TimestampStage::Begin);
				// Transfer commands currently belong to the graphics RHI path.
				// Other supported queues exercise timestamp recording/reuse alone.
				if (type == Rhi::QueueType::Graphics)
				{
					if (frame == 0)
					{
						commands.Transition(*upload, Rhi::ResourceState::HostWrite, Rhi::ResourceState::CopySource);
					}
					commands.Transition(*readback, frame == 0 ? Rhi::ResourceState::Undefined : Rhi::ResourceState::HostRead,
						Rhi::ResourceState::CopyDestination);
					commands.CopyBuffer(*upload, *readback, { 0, 0, byteCount });
					commands.Transition(*readback, Rhi::ResourceState::CopyDestination, Rhi::ResourceState::HostRead);
				}
				commands.WriteTimestamp(*queries, 1);
				commands.EndDebugLabel();
				commands.End();
				frames->SubmitCurrent();
				frames->Drain();
				SWIM_REQUIRE(queries->ReadTimestamps(0, results) == Rhi::QueryReadStatus::Ready);
				const auto elapsed = info.ElapsedNanoseconds(results[0], results[1]);
				SWIM_REQUIRE(elapsed.has_value());
				SWIM_CHECK(*elapsed >= 0.0);
				if (type == Rhi::QueueType::Graphics)
				{
					std::array<std::byte, byteCount> actual{};
					readback->Read(0, actual);
					SWIM_CHECK(actual == pattern);
				}
				std::fprintf(stderr, "[Swim timestamps] queue %u frame %u: %.3f ns, %u valid bits\n",
					static_cast<unsigned>(type), frame, *elapsed, info.ValidBits);
			}
			// Reusing a completed pool must make old available results unavailable.
			frames->BeginFrame();
			auto& reuse = frames->CreateCommandList();
			reuse.Begin();
			reuse.ResetQueries(*queries, 0, 2);
			reuse.End();
			frames->SubmitCurrent();
			frames->Drain();
			SWIM_REQUIRE(queries->ReadTimestamps(0, results) == Rhi::QueryReadStatus::NotReady);
			SWIM_CHECK(!results[0].Available && !results[1].Available);
		}
		SWIM_REQUIRE_MESSAGE(testedQueues > 0, "Timestamp smoke needs at least one timestamp-capable graphics/compute family");
	}

	[[maybe_unused]] const bool registered = []
	{
		const char* enabled = std::getenv("SWIM_RUN_RHI_SMOKE");
		if (enabled != nullptr && std::string_view(enabled) == "1")
		{
			Swim::Testing::TestRegistry::Get().Add({ "RHI.Vulkan.Smoke", "TimestampReadbackAndReuse", SWIM_TEST_LOCATION,
				+[] { Swim::Testing::RunValidatedVulkanSmoke(&RunTimestampSmoke); } });
		}
		return true;
	}();

} // namespace
