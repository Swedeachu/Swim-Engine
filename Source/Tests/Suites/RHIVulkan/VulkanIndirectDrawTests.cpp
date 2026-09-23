#include "Tests/Fixtures/VulkanPipelineCapture.h"
#include "Tests/Framework/Test.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Resources/VulkanBuffer.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Resources/VulkanTextureView.h"

using namespace Swim;

namespace
{
	struct IndirectCall
	{
		VkBuffer Arguments = VK_NULL_HANDLE;
		VkDeviceSize Offset = 0;
		VkBuffer Count = VK_NULL_HANDLE;
		VkDeviceSize CountOffset = 0;
		std::uint32_t Draws = 0;
		std::uint32_t Stride = 0;
		std::uint32_t Calls = 0;
	};

	IndirectCall* calls = nullptr;

	struct IndirectCapture : Testing::VulkanPipelineCapture
	{
		RhiVulkan::VulkanTexture Target{ State, VK_NULL_HANDLE,
			{ Rhi::TextureDimension::Texture2D, { 16, 16, 1 }, Rhi::Format::RGBA8Unorm, Rhi::TextureUsage::ColorAttachment } };
		RhiVulkan::VulkanTextureView View{ State, Target, VK_NULL_HANDLE,
			{ Rhi::TextureViewDimension::Texture2D, Rhi::Format::RGBA8Unorm } };
		Rhi::RenderingAttachmentDesc Attachment{};
		RhiVulkan::VulkanBuffer Indices{ State, RhiVulkan::FromNativeHandle<VkBuffer>(50), nullptr,
			{ 64, Rhi::BufferUsage::Index, Rhi::MemoryPreference::DeviceLocal, {} } };
		RhiVulkan::VulkanBuffer Arguments{ State, RhiVulkan::FromNativeHandle<VkBuffer>(51), nullptr,
			{ 100, Rhi::BufferUsage::Indirect | Rhi::BufferUsage::Storage, Rhi::MemoryPreference::DeviceLocal, {} } };
		RhiVulkan::VulkanBuffer Counts{ State, RhiVulkan::FromNativeHandle<VkBuffer>(52), nullptr,
			{ 16, Rhi::BufferUsage::Indirect, Rhi::MemoryPreference::DeviceLocal, {} } };
		RhiVulkan::VulkanBuffer Plain{ State, RhiVulkan::FromNativeHandle<VkBuffer>(53), nullptr,
			{ 100, Rhi::BufferUsage::Storage, Rhi::MemoryPreference::DeviceLocal, {} } };
		std::unique_ptr<RhiVulkan::VulkanGraphicsPipeline> Pipeline;
		IndirectCall Recorded;

		IndirectCapture()
		{
			calls = &Recorded;
			State->Device.physical_device.properties.limits.maxDrawIndirectCount = 1000;
			State->Dispatch.vkCmdDrawIndexedIndirect =
				+[](VkCommandBuffer, VkBuffer buffer, VkDeviceSize offset, std::uint32_t draws, std::uint32_t stride)
			{
				*calls = { buffer, offset, VK_NULL_HANDLE, 0, draws, stride, calls->Calls + 1 };
			};
			State->Dispatch.vkCmdDrawIndexedIndirectCount = +[](VkCommandBuffer, VkBuffer buffer, VkDeviceSize offset, VkBuffer count,
																 VkDeviceSize countOffset, std::uint32_t draws, std::uint32_t stride)
			{
				*calls = { buffer, offset, count, countOffset, draws, stride, calls->Calls + 1 };
			};
			Pipeline = MakePipeline(Rhi::Format::RGBA8Unorm);
			Attachment.View = &View;
		}

		void BeginDraw(bool bindIndices = true)
		{
			calls = &Recorded; // The dispatch spies report to the capture being drawn with.
			Commands->Begin();
			Commands->BindGraphicsPipeline(*Pipeline);
			Commands->BeginRendering({ { &Attachment, 1 }, nullptr, { 16, 16 } });
			Commands->SetViewport({ 0, 0, 16, 16 });
			Commands->SetScissor({ 0, 0, 16, 16 });
			if (bindIndices)
			{
				Commands->BindIndexBuffer(Indices, 0, Rhi::IndexType::Uint32);
			}
		}
	};
} // namespace

SWIM_TEST("RHI.Vulkan.IndirectDraw", "IndexedIndirectForwardsArgumentsAndValidatesRanges")
{
	IndirectCapture outside;
	SWIM_REQUIRE(outside.Pipeline);
	outside.Commands->Begin();
	SWIM_CHECK_THROWS(outside.Commands->DrawIndexedIndirect(outside.Arguments, 0, 1), std::logic_error); // Not rendering.

	IndirectCapture capture;
	auto& commands = *capture.Commands;
	IndirectCapture unbound;
	unbound.BeginDraw(false);
	SWIM_CHECK_THROWS(unbound.Commands->DrawIndexedIndirect(unbound.Arguments, 0, 1), std::logic_error); // No index buffer.

	capture.BeginDraw();
	commands.DrawIndexedIndirect(capture.Arguments, 20, 4);
	SWIM_CHECK_EQUAL(capture.Recorded.Calls, 1u);
	SWIM_CHECK_EQUAL(capture.Recorded.Arguments, RhiVulkan::FromNativeHandle<VkBuffer>(51));
	SWIM_CHECK_EQUAL(capture.Recorded.Offset, 20u);
	SWIM_CHECK_EQUAL(capture.Recorded.Draws, 4u);
	SWIM_CHECK_EQUAL(capture.Recorded.Stride, 20u);
	commands.DrawIndexedIndirect(capture.Arguments, 0, 4, 24); // 3 * 24 + 20 = 92 <= 100.
	commands.DrawIndexedIndirect(capture.Arguments, 100, 0);   // Zero draws: validated, nothing recorded.
	SWIM_CHECK_EQUAL(capture.Recorded.Calls, 2u);

	SWIM_CHECK_THROWS(commands.DrawIndexedIndirect(capture.Plain, 0, 1), std::invalid_argument);		 // No Indirect usage.
	SWIM_CHECK_THROWS(commands.DrawIndexedIndirect(capture.Arguments, 2, 1), std::invalid_argument);	 // Unaligned offset.
	SWIM_CHECK_THROWS(commands.DrawIndexedIndirect(capture.Arguments, 0, 2, 16), std::invalid_argument); // Stride below 20.
	SWIM_CHECK_THROWS(commands.DrawIndexedIndirect(capture.Arguments, 0, 2, 22), std::invalid_argument); // Unaligned stride.
	SWIM_CHECK_THROWS(commands.DrawIndexedIndirect(capture.Arguments, 0, 6), std::invalid_argument);	 // 6 * 20 > 100.
	SWIM_CHECK_THROWS(commands.DrawIndexedIndirect(capture.Arguments, 84, 1), std::invalid_argument);	 // 84 + 20 > 100.
	SWIM_CHECK_THROWS(commands.DrawIndexedIndirect(capture.Arguments, 0, 1001), std::invalid_argument);	 // Device limit.
	SWIM_CHECK_EQUAL(capture.Recorded.Calls, 2u);
}

SWIM_TEST("RHI.Vulkan.IndirectDraw", "IndexedIndirectCountReadsTheCountBuffer")
{
	IndirectCapture capture;
	capture.BeginDraw();
	auto& commands = *capture.Commands;
	commands.DrawIndexedIndirectCount(capture.Arguments, 40, capture.Counts, 8, 3);
	SWIM_CHECK_EQUAL(capture.Recorded.Calls, 1u);
	SWIM_CHECK_EQUAL(capture.Recorded.Count, RhiVulkan::FromNativeHandle<VkBuffer>(52));
	SWIM_CHECK_EQUAL(capture.Recorded.CountOffset, 8u);
	SWIM_CHECK_EQUAL(capture.Recorded.Draws, 3u);
	SWIM_CHECK_EQUAL(capture.Recorded.Offset, 40u);
	SWIM_CHECK_THROWS(commands.DrawIndexedIndirectCount(capture.Arguments, 0, capture.Plain, 0, 1), std::invalid_argument);
	SWIM_CHECK_THROWS(commands.DrawIndexedIndirectCount(capture.Arguments, 0, capture.Counts, 2, 1), std::invalid_argument);
	SWIM_CHECK_THROWS(commands.DrawIndexedIndirectCount(capture.Arguments, 0, capture.Counts, 16, 1), std::invalid_argument);
	SWIM_CHECK_THROWS(commands.DrawIndexedIndirectCount(capture.Arguments, 0, capture.Counts, 0, 6), std::invalid_argument); // Max range.
	SWIM_CHECK_EQUAL(capture.Recorded.Calls, 1u);
}
