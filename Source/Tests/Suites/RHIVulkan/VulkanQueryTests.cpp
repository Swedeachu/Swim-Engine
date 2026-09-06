#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Queries/VulkanQueryPool.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Resources/VulkanTextureView.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/VulkanQueue.h"
#include "Tests/Fixtures/VulkanCommandCapture.h"
#include "Tests/Framework/Test.h"

#include <array>
#include <limits>

using namespace Swim;

namespace
{
	struct QueryCapture;
	QueryCapture* current = nullptr;

	struct QueryCapture : Testing::VulkanCommandCapture
	{
		VkQueryPoolCreateInfo CreateInfo{};
		VkResult CreateResult = VK_SUCCESS;
		VkResult ReadResult = VK_SUCCESS;
		std::uint32_t Creates = 0;
		std::uint32_t Destroys = 0;
		std::uint32_t Reads = 0;
		std::uint32_t First = 0;
		std::uint32_t Count = 0;
		std::size_t DataSize = 0;
		VkDeviceSize Stride = 0;
		VkQueryResultFlags Flags = 0;
		std::vector<std::array<std::uint64_t, 2>> Payload{ { 100, 1 }, { 120, 1 } };
		std::vector<std::string> Operations;
		std::vector<VkPipelineStageFlags2> Stages;
		std::vector<std::uint32_t> Indices;
		std::string Name;

		QueryCapture()
		{
			current = this;
			State->QueueFamilies.Compute = 1;
			State->QueueFamilies.Transfer = 2;
			State->QueueProperties = {
				{ VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT, 1, 48, {} },
				{ VK_QUEUE_COMPUTE_BIT, 1, 36, {} },
				{ VK_QUEUE_TRANSFER_BIT, 1, 64, {} }
			};
			State->Device.physical_device.properties.limits.timestampPeriod = 0.5f;
			State->Instance = std::make_shared<RhiVulkan::VulkanInstanceState>();
			State->Instance->Diagnostics.DebugUtilsEnabled = true;
			State->Instance->Dispatch.vkSetDebugUtilsObjectNameEXT = +[](VkDevice, const VkDebugUtilsObjectNameInfoEXT* info) -> VkResult
			{
				current->Name = info->pObjectName;
				return VK_SUCCESS;
			};
			State->Dispatch.vkCreateQueryPool = +[](VkDevice, const VkQueryPoolCreateInfo* info, const VkAllocationCallbacks*, VkQueryPool* pool) -> VkResult
			{
				++current->Creates;
				current->CreateInfo = *info;
				*pool = RhiVulkan::FromNativeHandle<VkQueryPool>(77);
				return current->CreateResult;
			};
			State->Dispatch.vkDestroyQueryPool = +[](VkDevice, VkQueryPool, const VkAllocationCallbacks*) { ++current->Destroys; };
			State->Dispatch.vkCmdResetQueryPool = +[](VkCommandBuffer, VkQueryPool, std::uint32_t first, std::uint32_t count)
			{
				current->Operations.push_back("reset");
				current->First = first;
				current->Count = count;
			};
			State->Dispatch.vkCmdWriteTimestamp2 = +[](VkCommandBuffer, VkPipelineStageFlags2 stage, VkQueryPool, std::uint32_t index)
			{
				current->Operations.push_back("write");
				current->Stages.push_back(stage);
				current->Indices.push_back(index);
			};
			State->Dispatch.vkGetQueryPoolResults = +[](VkDevice, VkQueryPool, std::uint32_t first, std::uint32_t count,
				std::size_t size, void* data, VkDeviceSize stride, VkQueryResultFlags flags) -> VkResult
			{
				++current->Reads;
				current->First = first;
				current->Count = count;
				current->DataSize = size;
				current->Stride = stride;
				current->Flags = flags;
				auto* results = static_cast<std::array<std::uint64_t, 2>*>(data);
				for (std::uint32_t index = 0; index < count; ++index)
				{
					results[index] = current->Payload[index];
				}
				return current->ReadResult;
			};
		}

		std::unique_ptr<RhiVulkan::VulkanQueryPool> Create(Rhi::QueueType queue = Rhi::QueueType::Graphics)
		{
			return RhiVulkan::VulkanQueryPool::Create(State, { Rhi::QueryType::Timestamp, 4, "timings", queue });
		}
	};
}

SWIM_TEST("RHI.Vulkan.Queries", "PoolCreationOwnsNameAndDestroysOnlySuccessfulNativeObjects")
{
	QueryCapture capture;
	std::string name = "frame timings";
	auto pool = RhiVulkan::VulkanQueryPool::Create(capture.State, { Rhi::QueryType::Timestamp, 4, name });
	SWIM_REQUIRE(pool);
	name.assign("changed");
	SWIM_CHECK_EQUAL(pool->GetDesc().DebugName, std::string_view("frame timings"));
	SWIM_CHECK_EQUAL(capture.Name, std::string("frame timings"));
	SWIM_CHECK_EQUAL(capture.CreateInfo.queryType, VK_QUERY_TYPE_TIMESTAMP);
	SWIM_CHECK_EQUAL(capture.CreateInfo.queryCount, 4u);
	SWIM_CHECK_EQUAL(pool->GetTimestampInfo().NanosecondsPerTick, 0.5);
	SWIM_CHECK_EQUAL(pool->GetTimestampInfo().ValidBits, 48u);
	pool.reset();
	SWIM_CHECK_EQUAL(capture.Destroys, 1u);
	capture.CreateResult = VK_ERROR_OUT_OF_DEVICE_MEMORY;
	SWIM_CHECK(!capture.Create());
	SWIM_CHECK_EQUAL(capture.Destroys, 1u);
}

SWIM_TEST("RHI.Vulkan.Queries", "CapabilitiesArePerFamilyAndRejectUnsupportedResetQueues")
{
	QueryCapture capture;
	RhiVulkan::VulkanQueue graphics(capture.State, Rhi::QueueType::Graphics, VK_NULL_HANDLE, 0, {});
	RhiVulkan::VulkanQueue compute(capture.State, Rhi::QueueType::Compute, VK_NULL_HANDLE, 1, {});
	RhiVulkan::VulkanQueue transfer(capture.State, Rhi::QueueType::Transfer, VK_NULL_HANDLE, 2, {});
	SWIM_CHECK_EQUAL(graphics.GetTimestampInfo().ValidBits, 48u);
	SWIM_CHECK_EQUAL(compute.GetTimestampInfo().ValidBits, 36u);
	SWIM_CHECK(!transfer.GetTimestampInfo().IsSupported());
	SWIM_CHECK(!capture.Create(Rhi::QueueType::Transfer));
	SWIM_CHECK(capture.Create(Rhi::QueueType::Compute));
	capture.State->QueueFamilies.Transfer = 0;
	SWIM_CHECK(capture.Create(Rhi::QueueType::Transfer));
	for (auto bits : { 0u, 65u })
	{
		capture.State->QueueProperties[0].timestampValidBits = bits;
		SWIM_CHECK(!capture.Create());
	}
	capture.State->QueueProperties[0].timestampValidBits = 64;
	capture.State->Device.physical_device.properties.limits.timestampPeriod = 0.0f;
	SWIM_CHECK(!capture.Create());
	capture.State->Device.physical_device.properties.limits.timestampPeriod = std::numeric_limits<float>::quiet_NaN();
	SWIM_CHECK(!capture.Create());
	SWIM_CHECK(!RhiVulkan::GetVulkanTimestampInfo(*capture.State, UINT32_MAX).IsSupported());
}

SWIM_TEST("RHI.Vulkan.Queries", "InvalidPoolDescriptionsNeverReachDriver")
{
	QueryCapture capture;
	SWIM_CHECK(!RhiVulkan::VulkanQueryPool::Create(capture.State, {}));
	for (auto type : { Rhi::QueryType::Occlusion, Rhi::QueryType::PipelineStatistics, static_cast<Rhi::QueryType>(255) })
	{
		SWIM_CHECK(!RhiVulkan::VulkanQueryPool::Create(capture.State, { type, 2, {} }));
	}
	SWIM_CHECK(!capture.Create(static_cast<Rhi::QueueType>(255)));
	SWIM_CHECK(!RhiVulkan::VulkanQueryPool::Create({}, { Rhi::QueryType::Timestamp, 2, {} }));
	SWIM_CHECK_EQUAL(capture.Creates, 0u);
}

SWIM_TEST("RHI.Vulkan.Queries", "ResetAndBoundaryWritesForwardRangesAndStages")
{
	QueryCapture capture;
	auto pool = capture.Create();
	SWIM_REQUIRE(pool);
	auto& commands = *capture.Commands;
	commands.Begin();
	commands.ResetQueries(*pool, 1, 2);
	commands.WriteTimestamp(*pool, 1, Rhi::TimestampStage::Begin);
	commands.WriteTimestamp(*pool, 2);
	commands.End();
	SWIM_CHECK(capture.Operations == std::vector<std::string>({ "reset", "write", "write" }));
	SWIM_CHECK_EQUAL(capture.First, 1u);
	SWIM_CHECK_EQUAL(capture.Count, 2u);
	SWIM_CHECK(capture.Stages == std::vector<VkPipelineStageFlags2>({ VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT }));
	SWIM_CHECK(capture.Indices == std::vector<std::uint32_t>({ 1, 2 }));
	++capture.Pool->Generation;
	commands.Begin();
	commands.ResetQueries(*pool, 0, 4);
	commands.WriteTimestamp(*pool, 0);
	commands.End();
	SWIM_CHECK_EQUAL(capture.Operations.size(), 5u);
}

SWIM_TEST("RHI.Vulkan.Queries", "InvalidRangesFamilyDeviceAndRecordingStateNeverDispatch")
{
	QueryCapture capture;
	auto pool = capture.Create();
	auto computePool = capture.Create(Rhi::QueueType::Compute);
	SWIM_REQUIRE(pool && computePool);
	auto& commands = *capture.Commands;
	SWIM_CHECK_THROWS(commands.ResetQueries(*pool, 0, 2), std::logic_error);
	SWIM_CHECK_THROWS(commands.WriteTimestamp(*pool, 0), std::logic_error);
	commands.Begin();
	SWIM_CHECK_THROWS(commands.ResetQueries(*pool, 0, 0), std::invalid_argument);
	SWIM_CHECK_THROWS(commands.ResetQueries(*pool, 3, UINT32_MAX), std::invalid_argument);
	SWIM_CHECK_THROWS(commands.ResetQueries(*pool, UINT32_MAX, 2), std::invalid_argument);
	SWIM_CHECK_THROWS(commands.WriteTimestamp(*pool, 4), std::invalid_argument);
	SWIM_CHECK_THROWS(commands.WriteTimestamp(*pool, 0, static_cast<Rhi::TimestampStage>(255)), std::invalid_argument);
	SWIM_CHECK_THROWS(commands.ResetQueries(*computePool, 0, 2), std::invalid_argument);
	SWIM_CHECK_THROWS(commands.WriteTimestamp(*computePool, 0), std::invalid_argument);
	auto other = std::make_shared<RhiVulkan::VulkanDeviceState>();
	other->QueueFamilies = capture.State->QueueFamilies;
	other->QueueProperties = capture.State->QueueProperties;
	other->Device.physical_device.properties = capture.State->Device.physical_device.properties;
	other->Dispatch = capture.State->Dispatch;
	auto foreign = RhiVulkan::VulkanQueryPool::Create(other, { Rhi::QueryType::Timestamp, 2, {} });
	SWIM_REQUIRE(foreign);
	SWIM_CHECK_THROWS(commands.ResetQueries(*foreign, 0, 2), std::invalid_argument);
	SWIM_CHECK_THROWS(commands.WriteTimestamp(*foreign, 0), std::invalid_argument);
	commands.End();
	SWIM_CHECK_THROWS(commands.WriteTimestamp(*pool, 0), std::logic_error);
	++capture.Pool->Generation;
	commands.Begin();
	++capture.Pool->Generation;
	SWIM_CHECK_THROWS(commands.ResetQueries(*pool, 0, 2), std::logic_error);
	SWIM_CHECK_THROWS(commands.WriteTimestamp(*pool, 0), std::logic_error);
	SWIM_CHECK(capture.Operations.empty());
}

SWIM_TEST("RHI.Vulkan.Queries", "QueryCommandsRejectActiveRendering")
{
	QueryCapture capture;
	auto pool = capture.Create();
	SWIM_REQUIRE(pool);
	Rhi::TextureDesc desc{};
	desc.Extent = { 8, 8, 1 };
	desc.PixelFormat = Rhi::Format::RGBA8Unorm;
	desc.Usage = Rhi::TextureUsage::ColorAttachment;
	RhiVulkan::VulkanTexture texture(capture.State, RhiVulkan::FromNativeHandle<VkImage>(50), desc);
	RhiVulkan::VulkanTextureView view(capture.State, texture, RhiVulkan::FromNativeHandle<VkImageView>(51), {});
	Rhi::RenderingAttachmentDesc attachment{};
	attachment.View = &view;
	capture.Commands->Begin();
	capture.Commands->BeginRendering({ { &attachment, 1 }, nullptr, { 8, 8 } });
	SWIM_CHECK_THROWS(capture.Commands->ResetQueries(*pool, 0, 2), std::logic_error);
	SWIM_CHECK_THROWS(capture.Commands->WriteTimestamp(*pool, 0), std::logic_error);
	capture.Commands->EndRendering();
	capture.Commands->End();
	SWIM_CHECK(capture.Operations.empty());
}

SWIM_TEST("RHI.Vulkan.Queries", "ReadbackUses64BitAvailabilityAndNeverWaitsOrExposesUnavailableData")
{
	QueryCapture capture;
	auto pool = capture.Create();
	SWIM_REQUIRE(pool);
	std::array<Rhi::TimestampResult, 2> results{};
	SWIM_CHECK(pool->ReadTimestamps(1, results) == Rhi::QueryReadStatus::Ready);
	SWIM_CHECK_EQUAL(results[0].Ticks, 100u);
	SWIM_CHECK(results[0].Available && results[1].Available);
	SWIM_CHECK_EQUAL(capture.First, 1u);
	SWIM_CHECK_EQUAL(capture.Count, 2u);
	SWIM_CHECK_EQUAL(capture.DataSize, 32u);
	SWIM_CHECK_EQUAL(capture.Stride, 16u);
	SWIM_CHECK_EQUAL(capture.Flags, static_cast<VkQueryResultFlags>(VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT));
	SWIM_CHECK(pool->GetTimestampInfo().ElapsedNanoseconds(results[0], results[1]) == 10.0);
	capture.ReadResult = VK_NOT_READY;
	capture.Payload = { { UINT64_MAX, 0 }, { 0, 3 } };
	SWIM_CHECK(pool->ReadTimestamps(0, results) == Rhi::QueryReadStatus::NotReady);
	SWIM_CHECK(!results[0].Available && results[0].Ticks == 0);
	SWIM_CHECK(results[1].Available && results[1].Ticks == 0);
	SWIM_CHECK(!pool->GetTimestampInfo().ElapsedNanoseconds(results[0], results[1]));
}

SWIM_TEST("RHI.Vulkan.Queries", "ReadbackErrorsClearOldResultsAndRangeChecksCannotOverflow")
{
	QueryCapture capture;
	auto pool = capture.Create();
	SWIM_REQUIRE(pool);
	std::array<Rhi::TimestampResult, 2> results{};
	SWIM_REQUIRE(pool->ReadTimestamps(0, results) == Rhi::QueryReadStatus::Ready);
	capture.ReadResult = VK_ERROR_UNKNOWN;
	SWIM_CHECK(pool->ReadTimestamps(0, results) == Rhi::QueryReadStatus::Error);
	SWIM_CHECK(!results[0].Available && results[0].Ticks == 0);
	SWIM_CHECK(!results[1].Available && results[1].Ticks == 0);
	SWIM_CHECK(pool->ReadTimestamps(3, results) == Rhi::QueryReadStatus::Error);
	SWIM_CHECK(pool->ReadTimestamps(UINT32_MAX, results) == Rhi::QueryReadStatus::Error);
	SWIM_CHECK(pool->ReadTimestamps(0, {}) == Rhi::QueryReadStatus::Error);
	SWIM_CHECK(!pool->Contains(1, std::numeric_limits<std::size_t>::max()));
	SWIM_CHECK_EQUAL(capture.Reads, 2u);
}

SWIM_TEST("RHI.Vulkan.Queries", "ComputeFamilyRecordsItsOwnQueriesAndRejectsGraphicsPool")
{
	QueryCapture capture;
	auto compute = capture.Create(Rhi::QueueType::Compute);
	auto graphics = capture.Create();
	SWIM_REQUIRE(compute && graphics);
	capture.Pool->FamilyIndex = 1;
	capture.Commands->Begin();
	SWIM_CHECK_THROWS(capture.Commands->WriteTimestamp(*graphics, 0), std::invalid_argument);
	capture.Commands->ResetQueries(*compute, 0, 2);
	capture.Commands->WriteTimestamp(*compute, 0, Rhi::TimestampStage::Begin);
	capture.Commands->WriteTimestamp(*compute, 1);
	capture.Commands->End();
	SWIM_CHECK_EQUAL(capture.Operations.size(), 3u);
	SWIM_CHECK_EQUAL(compute->GetTimestampInfo().ValidBits, 36u);
}

SWIM_TEST("RHI.Vulkan.Queries", "DeviceLossIsTypedAndClearsReadbackWithoutRetryingDriver")
{
	QueryCapture capture;
	auto pool = capture.Create();
	SWIM_REQUIRE(pool);
	std::array<Rhi::TimestampResult, 2> results{};
	SWIM_REQUIRE(pool->ReadTimestamps(0, results) == Rhi::QueryReadStatus::Ready);
	capture.ReadResult = VK_ERROR_DEVICE_LOST;
	SWIM_CHECK_THROWS(pool->ReadTimestamps(0, results), Rhi::DeviceLostError);
	SWIM_CHECK(!results[0].Available && results[0].Ticks == 0);
	SWIM_CHECK(!results[1].Available && results[1].Ticks == 0);
	SWIM_CHECK(capture.State->Diagnostics->IsLost());
	SWIM_CHECK_THROWS(pool->ReadTimestamps(0, results), Rhi::DeviceLostError);
	SWIM_CHECK_EQUAL(capture.Reads, 2u);
}
