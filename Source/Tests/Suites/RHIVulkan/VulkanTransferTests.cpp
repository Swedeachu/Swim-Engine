#include "Tests/Fixtures/VulkanCommandCapture.h"
#include "Tests/Framework/Test.h"

#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanResourceState.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanTransferUtils.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Resources/VulkanBuffer.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Resources/VulkanTexture.h"

#include <limits>

using namespace Swim;

SWIM_TEST("RHI.Vulkan.Transfer", "ReadbackBarrierIncludesHostVisibilityAndPreservesSameStateWrites")
{
	Testing::VulkanCommandCapture capture;
	Rhi::BufferDesc desc{ 256, Rhi::BufferUsage::TransferDestination | Rhi::BufferUsage::Storage, Rhi::MemoryPreference::GpuToCpu, {} };
	RhiVulkan::VulkanBuffer buffer(capture.State, VK_NULL_HANDLE, nullptr, desc);
	capture.Commands->Begin();
	capture.Commands->Transition(buffer, Rhi::ResourceState::CopyDestination, Rhi::ResourceState::HostRead);
	capture.Commands->Transition(buffer, Rhi::ResourceState::ShaderWrite, Rhi::ResourceState::ShaderWrite);
	SWIM_REQUIRE_EQUAL(capture.Buffers.size(), 2u);
	SWIM_CHECK_EQUAL(capture.Buffers[0].srcAccessMask, VK_ACCESS_2_TRANSFER_WRITE_BIT);
	SWIM_CHECK_EQUAL(capture.Buffers[0].dstStageMask, VK_PIPELINE_STAGE_2_HOST_BIT);
	SWIM_CHECK_EQUAL(capture.Buffers[0].dstAccessMask, VK_ACCESS_2_HOST_READ_BIT);
	SWIM_CHECK_EQUAL(capture.Buffers[0].srcQueueFamilyIndex, VK_QUEUE_FAMILY_IGNORED);
	SWIM_CHECK_EQUAL(capture.Buffers[0].size, VK_WHOLE_SIZE);
	SWIM_CHECK_EQUAL(capture.Buffers[1].srcAccessMask, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
	SWIM_CHECK_THROWS(capture.Commands->Transition(buffer, Rhi::ResourceState::HostWrite, Rhi::ResourceState::CopyDestination), std::invalid_argument);
}

SWIM_TEST("RHI.Vulkan.Transfer", "TextureBarriersResolveRemainingMipsLayersAndCombinedDepthAspects")
{
	Testing::VulkanCommandCapture capture;
	Rhi::TextureDesc desc{};
	desc.Extent = { 64, 64, 1 };
	desc.PixelFormat = Rhi::Format::D24UnormS8Uint;
	desc.Usage = Rhi::TextureUsage::DepthStencilAttachment | Rhi::TextureUsage::Sampled;
	desc.MipLevels = 4;
	desc.ArrayLayers = 3;
	RhiVulkan::VulkanTexture texture(capture.State, VK_NULL_HANDLE, desc);
	capture.Commands->Begin();
	capture.Commands->Transition(texture, Rhi::ResourceState::Undefined, Rhi::ResourceState::DepthStencilWrite, { 1, UINT32_MAX, 1, UINT32_MAX });
	SWIM_REQUIRE_EQUAL(capture.Images.size(), 1u);
	const auto& barrier = capture.Images.front();
	SWIM_CHECK_EQUAL(barrier.oldLayout, VK_IMAGE_LAYOUT_UNDEFINED);
	SWIM_CHECK_EQUAL(barrier.newLayout, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
	SWIM_CHECK_EQUAL(barrier.subresourceRange.levelCount, 3u);
	SWIM_CHECK_EQUAL(barrier.subresourceRange.layerCount, 2u);
	SWIM_CHECK_EQUAL(barrier.subresourceRange.aspectMask, static_cast<VkImageAspectFlags>(VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT));
	SWIM_CHECK_EQUAL(barrier.srcStageMask, VK_PIPELINE_STAGE_2_NONE);
	SWIM_CHECK_THROWS(capture.Commands->Transition(texture, Rhi::ResourceState::Undefined, Rhi::ResourceState::Undefined, {}), std::invalid_argument);
	SWIM_CHECK_THROWS(capture.Commands->Transition(texture, Rhi::ResourceState::Undefined, Rhi::ResourceState::DepthStencilWrite, { 4, 1, 0, 1 }), std::invalid_argument);
	SWIM_CHECK_THROWS(capture.Commands->Transition(texture, Rhi::ResourceState::Undefined, Rhi::ResourceState::DepthStencilWrite, { 0, 0, 0, 1 }), std::invalid_argument);
	const auto read = RhiVulkan::GetTextureState(desc, Rhi::ResourceState::DepthStencilRead | Rhi::ResourceState::ShaderRead);
	SWIM_CHECK_EQUAL(read.Layout, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
	SWIM_CHECK((read.Access & VK_ACCESS_2_SHADER_SAMPLED_READ_BIT) != 0);
	SWIM_CHECK_THROWS(RhiVulkan::GetTextureState(desc, Rhi::ResourceState::CopySource | Rhi::ResourceState::CopyDestination), std::invalid_argument);
}

SWIM_TEST("RHI.Vulkan.Transfer", "CopiesRejectOverflowOverlapAndForeignDeviceBeforeDispatch")
{
	Testing::VulkanCommandCapture capture;
	Rhi::BufferDesc desc{ 256, Rhi::BufferUsage::TransferSource | Rhi::BufferUsage::TransferDestination, Rhi::MemoryPreference::DeviceLocal, {} };
	RhiVulkan::VulkanBuffer buffer(capture.State, VK_NULL_HANDLE, nullptr, desc);
	RhiVulkan::VulkanBuffer foreign(std::make_shared<RhiVulkan::VulkanDeviceState>(), VK_NULL_HANDLE, nullptr, desc);
	SWIM_CHECK_THROWS(capture.Commands->CopyBuffer(buffer, buffer, { 0, 128, 64 }), std::logic_error);
	capture.Commands->Begin();
	capture.Commands->CopyBuffer(buffer, buffer, { 0, 128, 64 });
	SWIM_CHECK_EQUAL(capture.BufferCopy.dstOffset, 128u);
	SWIM_CHECK_THROWS(capture.Commands->CopyBuffer(buffer, buffer, { 0, 32, 64 }), std::invalid_argument);
	SWIM_CHECK_THROWS(capture.Commands->CopyBuffer(buffer, buffer, { UINT64_MAX, 0, 2 }), std::invalid_argument);
	SWIM_CHECK_THROWS(capture.Commands->CopyBuffer(buffer, buffer, { 0, 256, 1 }), std::invalid_argument);
	SWIM_CHECK_THROWS(capture.Commands->CopyBuffer(buffer, buffer, { 0, 128, 0 }), std::invalid_argument);
	SWIM_CHECK_THROWS(capture.Commands->CopyBuffer(buffer, foreign, { 0, 0, 16 }), std::invalid_argument);
	SWIM_CHECK_EQUAL(capture.CopyCount, 1u);
}

SWIM_TEST("RHI.Vulkan.Transfer", "PackedUploadReadbackAndTextureCopyUseExplicitSubresources")
{
	Testing::VulkanCommandCapture capture;
	Rhi::BufferDesc bufferDesc{ 512, Rhi::BufferUsage::TransferSource | Rhi::BufferUsage::TransferDestination, Rhi::MemoryPreference::DeviceLocal, {} };
	RhiVulkan::VulkanBuffer buffer(capture.State, VK_NULL_HANDLE, nullptr, bufferDesc);
	Rhi::TextureDesc desc{};
	desc.Extent = { 16, 16, 1 };
	desc.PixelFormat = Rhi::Format::RGBA8Unorm;
	desc.Usage = Rhi::TextureUsage::TransferSource | Rhi::TextureUsage::TransferDestination;
	desc.MipLevels = 3;
	desc.ArrayLayers = 2;
	RhiVulkan::VulkanTexture source(capture.State, VK_NULL_HANDLE, desc);
	RhiVulkan::VulkanTexture destination(capture.State, VK_NULL_HANDLE, desc);
	Rhi::BufferTextureCopyRegion region{ 16, { 1, 1 }, { 2, 2, 0 }, { 4, 4, 1 } };
	capture.Commands->Begin();
	capture.Commands->CopyBufferToTexture(buffer, source, region);
	capture.Commands->CopyTextureToBuffer(source, buffer, region);
	SWIM_CHECK_EQUAL(capture.BufferImageCopy.bufferOffset, 16u);
	SWIM_CHECK_EQUAL(capture.BufferImageCopy.imageSubresource.mipLevel, 1u);
	SWIM_CHECK_EQUAL(capture.BufferImageCopy.imageSubresource.baseArrayLayer, 1u);
	SWIM_CHECK_EQUAL(capture.BufferImageCopy.bufferRowLength, 0u);
	SWIM_CHECK_EQUAL(capture.BufferImageCopy.imageOffset.x, 2);
	Rhi::TextureCopyRegion imageRegion{ { 1, 1 }, { 1, 0 }, {}, {}, { 8, 8, 1 } };
	capture.Commands->CopyTexture(source, destination, imageRegion);
	SWIM_CHECK_EQUAL(capture.ImageCopy.srcSubresource.baseArrayLayer, 1u);
	SWIM_CHECK_EQUAL(capture.ImageCopy.extent.width, 8u);
	region.TextureOffset.X = 5;
	SWIM_CHECK_THROWS(capture.Commands->CopyBufferToTexture(buffer, source, region), std::invalid_argument);
	region.TextureOffset.X = -1;
	SWIM_CHECK_THROWS(capture.Commands->CopyTextureToBuffer(source, buffer, region), std::invalid_argument);
	region.TextureOffset.X = 0;
	region.BufferOffset = 500;
	SWIM_CHECK_THROWS(capture.Commands->CopyTextureToBuffer(source, buffer, region), std::invalid_argument);
	region.BufferOffset = 2;
	SWIM_CHECK_THROWS(capture.Commands->CopyTextureToBuffer(source, buffer, region), std::invalid_argument);
	SWIM_CHECK_EQUAL(capture.CopyCount, 3u);
}

SWIM_TEST("RHI.Vulkan.Transfer", "UnsupportedFormatsAndByteCountOverflowAreRejected")
{
	Rhi::BufferDesc buffer{ UINT64_MAX, Rhi::BufferUsage::TransferDestination, Rhi::MemoryPreference::DeviceLocal, {} };
	Rhi::TextureDesc texture{};
	texture.Extent = { UINT32_MAX, UINT32_MAX, UINT32_MAX };
	texture.PixelFormat = Rhi::Format::RGBA32Float;
	Rhi::BufferTextureCopyRegion region{};
	region.Extent = texture.Extent;
	SWIM_CHECK_THROWS(RhiVulkan::GetBufferImageCopy(buffer, texture, region), std::invalid_argument);
	SWIM_CHECK_THROWS(RhiVulkan::GetColorTexelBytes(Rhi::Format::BC7Unorm), std::invalid_argument);
	SWIM_CHECK_THROWS(RhiVulkan::GetColorTexelBytes(Rhi::Format::D32Float), std::invalid_argument);
}
