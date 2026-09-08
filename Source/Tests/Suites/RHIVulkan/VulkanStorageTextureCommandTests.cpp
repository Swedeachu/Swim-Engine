#include "Tests/Fixtures/VulkanStorageTextureCapture.h"
#include "Tests/Framework/Test.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Resources/VulkanBuffer.h"

using namespace Swim;

SWIM_TEST("RHI.Vulkan.StorageTextureCommand", "DedicatedComputeTransitionsSubresourcesAndCopiesStorageResults")
{
	Testing::VulkanStorageTextureCapture capture;
	capture.Pool->FamilyIndex = 1;
	Rhi::TextureDesc desc;
	desc.Extent = { 32, 16, 1 };
	desc.PixelFormat = Rhi::Format::R32Uint;
	desc.Usage = Rhi::TextureUsage::Storage | Rhi::TextureUsage::Sampled | Rhi::TextureUsage::TransferSource | Rhi::TextureUsage::TransferDestination;
	desc.MipLevels = 3;
	desc.ArrayLayers = 2;
	RhiVulkan::VulkanTexture texture(capture.State, VK_NULL_HANDLE, desc);
	RhiVulkan::VulkanTexture other(capture.State, VK_NULL_HANDLE, desc);
	RhiVulkan::VulkanBuffer buffer(capture.State, VK_NULL_HANDLE, nullptr,
		{ 1024, Rhi::BufferUsage::TransferSource | Rhi::BufferUsage::TransferDestination });
	const Rhi::TextureSubresourceRange range{ 1, 1, 1, 1 };
	const auto rw = Rhi::ResourceState::ShaderRead | Rhi::ResourceState::ShaderWrite;
	capture.Commands->Begin();
	capture.Commands->Transition(texture, Rhi::ResourceState::Undefined, Rhi::ResourceState::CopyDestination, range);
	Rhi::BufferTextureCopyRegion region{};
	region.BufferOffset = 16;
	region.Subresource = { 1, 1 };
	region.Extent = { 16, 8, 1 };
	capture.Commands->CopyBufferToTexture(buffer, texture, region);
	capture.Commands->Transition(texture, Rhi::ResourceState::CopyDestination, rw, range);
	capture.Commands->Transition(texture, rw, rw, range);
	capture.Commands->Transition(texture, rw, Rhi::ResourceState::CopySource, range);
	capture.Commands->CopyTextureToBuffer(texture, buffer, region);
	Rhi::TextureCopyRegion copy{};
	copy.Source = copy.Destination = { 1, 1 };
	copy.Extent = { 16, 8, 1 };
	capture.Commands->CopyTexture(texture, other, copy);
	SWIM_REQUIRE_EQUAL(capture.Images.size(), 4u);
	const auto& barrier = capture.Images[2];
	SWIM_CHECK_EQUAL(barrier.oldLayout, VK_IMAGE_LAYOUT_GENERAL);
	SWIM_CHECK_EQUAL(barrier.newLayout, VK_IMAGE_LAYOUT_GENERAL);
	SWIM_CHECK((barrier.srcAccessMask & VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT) != 0);
	SWIM_CHECK((barrier.dstAccessMask & VK_ACCESS_2_SHADER_STORAGE_READ_BIT) != 0);
	SWIM_CHECK_EQUAL(barrier.srcStageMask, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);
	SWIM_CHECK_EQUAL(barrier.srcQueueFamilyIndex, VK_QUEUE_FAMILY_IGNORED);
	SWIM_CHECK_EQUAL(barrier.dstQueueFamilyIndex, VK_QUEUE_FAMILY_IGNORED);
	SWIM_CHECK_EQUAL(barrier.subresourceRange.baseMipLevel, 1u);
	SWIM_CHECK_EQUAL(barrier.subresourceRange.baseArrayLayer, 1u);
	SWIM_CHECK_EQUAL(barrier.subresourceRange.levelCount, 1u);
	SWIM_CHECK_EQUAL(barrier.subresourceRange.layerCount, 1u);
	SWIM_CHECK_EQUAL(capture.CopyCount, 3u);
	SWIM_CHECK_EQUAL(capture.BufferImageCopy.bufferOffset, 16u);
	capture.Commands->Transition(texture, Rhi::ResourceState::CopySource, Rhi::ResourceState::ShaderRead, range);
	SWIM_CHECK_EQUAL(capture.Images.back().newLayout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

SWIM_TEST("RHI.Vulkan.StorageTextureCommand", "RejectsUnsupportedStatesFamiliesAndCopyBoundsBeforeRecording")
{
	Testing::VulkanStorageTextureCapture capture;
	Rhi::TextureDesc desc;
	desc.Extent = { 16, 16, 1 };
	desc.PixelFormat = Rhi::Format::RGBA32Float;
	desc.Usage = Rhi::TextureUsage::Storage | Rhi::TextureUsage::TransferSource | Rhi::TextureUsage::ColorAttachment;
	RhiVulkan::VulkanTexture texture(capture.State, VK_NULL_HANDLE, desc);
	RhiVulkan::VulkanBuffer buffer(capture.State, VK_NULL_HANDLE, nullptr, { 128, Rhi::BufferUsage::TransferDestination });
	capture.Pool->FamilyIndex = 1;
	capture.Commands->Begin();
	for (auto state : { Rhi::ResourceState::ColorAttachment, Rhi::ResourceState::DepthStencilRead,
		Rhi::ResourceState::DepthStencilWrite, Rhi::ResourceState::Present })
	{
		SWIM_CHECK_THROWS(capture.Commands->Transition(texture, state, Rhi::ResourceState::ShaderWrite, {}), std::invalid_argument);
		SWIM_CHECK_THROWS(capture.Commands->Transition(texture, Rhi::ResourceState::ShaderWrite, state, {}), std::invalid_argument);
	}
	Rhi::BufferTextureCopyRegion region{};
	region.Extent = { 16, 16, 1 };
	SWIM_CHECK_THROWS(capture.Commands->CopyTextureToBuffer(texture, buffer, region), std::invalid_argument);
	for (auto family : { 2u, 99u })
	{
		capture.Pool->FamilyIndex = family;
		SWIM_CHECK_THROWS(capture.Commands->Transition(texture, Rhi::ResourceState::Undefined, Rhi::ResourceState::ShaderWrite, {}), std::logic_error);
		SWIM_CHECK_THROWS(capture.Commands->CopyTextureToBuffer(texture, buffer, region), std::logic_error);
	}
	SWIM_CHECK(capture.Images.empty());
	SWIM_CHECK_EQUAL(capture.CopyCount, 0u);
	capture.Pool->FamilyIndex = 1;
	capture.State->Diagnostics->TryRecordLoss("storage image commands test", VK_ERROR_DEVICE_LOST);
	SWIM_CHECK_THROWS(capture.Commands->Transition(texture, Rhi::ResourceState::Undefined, Rhi::ResourceState::ShaderWrite, {}), Rhi::DeviceLostError);
	SWIM_CHECK(capture.Images.empty());
}
