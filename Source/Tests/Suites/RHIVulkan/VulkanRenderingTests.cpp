#include "Tests/Fixtures/VulkanCommandCapture.h"
#include "Tests/Framework/Test.h"

#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Resources/VulkanBuffer.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Resources/VulkanTextureView.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/VulkanQueue.h"

#include <array>
#include <limits>

using namespace Swim;

SWIM_TEST("RHI.Vulkan.Rendering", "ClearAttachmentsAndCanonicalViewportAreRecorded")
{
	Testing::VulkanCommandCapture capture;
	Rhi::TextureDesc desc{};
	desc.PixelFormat = Rhi::Format::RGBA8Unorm;
	desc.Usage = Rhi::TextureUsage::ColorAttachment;
	desc.Extent = { 64, 32, 1 };
	RhiVulkan::VulkanTexture texture(capture.State, VK_NULL_HANDLE, desc);
	Rhi::TextureViewDesc viewDesc{};
	viewDesc.PixelFormat = desc.PixelFormat;
	RhiVulkan::VulkanTextureView view(capture.State, texture, VK_NULL_HANDLE, viewDesc);
	Rhi::RenderingAttachmentDesc color{};
	color.View = &view;
	color.Load = Rhi::LoadOp::Clear;
	color.Clear.Value = { 0.25f, 0.5f, 1.0f, 1.0f };
	Rhi::RenderingDesc rendering{ { &color, 1 }, nullptr, { 64, 32 } };
	capture.Commands->Begin();
	capture.Commands->BeginRendering(rendering);
	SWIM_REQUIRE_EQUAL(capture.Colors.size(), 1u);
	SWIM_CHECK_EQUAL(capture.Colors[0].loadOp, VK_ATTACHMENT_LOAD_OP_CLEAR);
	SWIM_CHECK_EQUAL(capture.Colors[0].storeOp, VK_ATTACHMENT_STORE_OP_STORE);
	SWIM_CHECK_NEAR(capture.Colors[0].clearValue.color.float32[1], 0.5f, 0.0f);
	capture.Commands->SetViewport({ 4.0f, 2.0f, 32.0f, 16.0f, 0.0f, 1.0f });
	SWIM_CHECK_NEAR(capture.Viewport.y, 18.0f, 0.0f);
	SWIM_CHECK_NEAR(capture.Viewport.height, -16.0f, 0.0f);
	capture.Commands->SetScissor({ 4, 2, 32, 16 });
	SWIM_CHECK_EQUAL(capture.Scissor.offset.y, 2);
	SWIM_CHECK_THROWS(capture.Commands->BeginRendering(rendering), std::logic_error);
	SWIM_CHECK_THROWS(capture.Commands->End(), std::logic_error);
	SWIM_CHECK_THROWS(capture.Commands->Transition(texture, Rhi::ResourceState::ColorAttachment, Rhi::ResourceState::Present, {}), std::logic_error);
	capture.Commands->EndRendering();
	capture.Commands->End();
	SWIM_CHECK_EQUAL(capture.EndCount, 1u);
	SWIM_CHECK(capture.Commands->IsExecutable());
	SWIM_CHECK_THROWS(capture.Commands->Begin(), std::logic_error);
	++capture.Pool->Generation;
	SWIM_CHECK(!capture.Commands->IsExecutable());
	capture.Commands->Begin();
	capture.Commands->End();
}

SWIM_TEST("RHI.Vulkan.Rendering", "BadAttachmentsAndViewportInputsNeverDispatch")
{
	Testing::VulkanCommandCapture capture;
	capture.Commands->Begin();
	SWIM_CHECK_THROWS(capture.Commands->EndRendering(), std::logic_error);
	SWIM_CHECK_THROWS(capture.Commands->BeginRendering({}), std::invalid_argument);
	Rhi::RenderingAttachmentDesc color{};
	Rhi::RenderingDesc rendering{ { &color, 1 }, nullptr, { 32, 32 } };
	SWIM_CHECK_THROWS(capture.Commands->BeginRendering(rendering), std::invalid_argument);
	Rhi::TextureDesc desc{};
	desc.PixelFormat = Rhi::Format::RGBA8Unorm;
	desc.Usage = Rhi::TextureUsage::ColorAttachment;
	desc.Extent = { 16, 16, 1 };
	RhiVulkan::VulkanTexture texture(capture.State, VK_NULL_HANDLE, desc);
	Rhi::TextureViewDesc viewDesc{};
	viewDesc.PixelFormat = desc.PixelFormat;
	RhiVulkan::VulkanTextureView view(capture.State, texture, VK_NULL_HANDLE, viewDesc);
	color.View = &view;
	SWIM_CHECK_THROWS(capture.Commands->BeginRendering(rendering), std::invalid_argument);
	SWIM_CHECK_THROWS(capture.Commands->SetViewport({ 0, 0, -1, 16 }), std::invalid_argument);
	SWIM_CHECK_THROWS(capture.Commands->SetViewport({ 0, 0, 16, 16, 0, std::numeric_limits<float>::quiet_NaN() }), std::invalid_argument);
	SWIM_CHECK_THROWS(capture.Commands->SetScissor({ -1, 0, 1, 1 }), std::invalid_argument);
	SWIM_CHECK_THROWS(capture.Commands->SetScissor({ 1, 0, UINT32_MAX, 1 }), std::invalid_argument);
	SWIM_CHECK_EQUAL(capture.BeginCount, 0u);
}

SWIM_TEST("RHI.Vulkan.Rendering", "DepthStencilUsesBothAttachmentAspects")
{
	Testing::VulkanCommandCapture capture;
	Rhi::TextureDesc desc{};
	desc.PixelFormat = Rhi::Format::D24UnormS8Uint;
	desc.Usage = Rhi::TextureUsage::DepthStencilAttachment;
	desc.Extent = { 32, 32, 1 };
	RhiVulkan::VulkanTexture texture(capture.State, VK_NULL_HANDLE, desc);
	Rhi::TextureViewDesc viewDesc{};
	viewDesc.PixelFormat = desc.PixelFormat;
	RhiVulkan::VulkanTextureView view(capture.State, texture, VK_NULL_HANDLE, viewDesc);
	Rhi::DepthStencilAttachmentDesc depth{};
	depth.View = &view;
	depth.DepthLoad = Rhi::LoadOp::Clear;
	depth.ClearDepth = 0.25f;
	depth.ClearStencil = 7;
	capture.Commands->Begin();
	capture.Commands->BeginRendering({ {}, &depth, { 32, 32 } });
	SWIM_CHECK(capture.HasDepth && capture.HasStencil);
	SWIM_CHECK_EQUAL(capture.Depth.clearValue.depthStencil.stencil, 7u);
	SWIM_CHECK_NEAR(capture.Depth.clearValue.depthStencil.depth, 0.25f, 0.0f);
	capture.Commands->EndRendering();
	depth.ClearDepth = -1.0f;
	SWIM_CHECK_THROWS(capture.Commands->BeginRendering({ {}, &depth, { 32, 32 } }), std::invalid_argument);
}

SWIM_TEST("RHI.Vulkan.Rendering", "QueueRejectsUnfinishedDuplicateAndAlreadySubmittedCommands")
{
	Testing::VulkanCommandCapture capture;
	RhiVulkan::VulkanQueue queue(capture.State, Rhi::QueueType::Graphics, VK_NULL_HANDLE, 0, std::make_shared<std::mutex>());
	std::array<Rhi::CommandList*, 1> lists{ capture.Commands.get() };
	Rhi::SubmitDesc submit{};
	submit.CommandLists = lists;
	SWIM_CHECK_THROWS(queue.Submit(submit), std::invalid_argument);
	capture.Commands->Begin();
	SWIM_CHECK_THROWS(queue.Submit(submit), std::invalid_argument);
	capture.Commands->End();
	std::array<Rhi::CommandList*, 2> duplicate{ capture.Commands.get(), capture.Commands.get() };
	submit.CommandLists = duplicate;
	SWIM_CHECK_THROWS(queue.Submit(submit), std::invalid_argument);
	submit.CommandLists = lists;
	capture.SubmitResult = VK_ERROR_OUT_OF_HOST_MEMORY;
	SWIM_CHECK_THROWS(queue.Submit(submit), std::runtime_error);
	SWIM_CHECK(capture.Commands->IsExecutable());
	capture.SubmitResult = VK_SUCCESS;
	queue.Submit(submit);
	SWIM_CHECK(!capture.Commands->IsExecutable());
	SWIM_CHECK_THROWS(queue.Submit(submit), std::invalid_argument);
	SWIM_CHECK_EQUAL(capture.SubmitCount, 2u);
}

SWIM_TEST("RHI.Vulkan.Rendering", "CpuBufferAccessRejectsWrongMemoryAndOverflowBeforeAllocatorAccess")
{
	Testing::VulkanCommandCapture capture;
	Rhi::BufferDesc desc{ 16, Rhi::BufferUsage::TransferSource, Rhi::MemoryPreference::CpuToGpu, {} };
	RhiVulkan::VulkanBuffer upload(capture.State, VK_NULL_HANDLE, nullptr, desc);
	std::array<std::byte, 4> bytes{};
	SWIM_CHECK_THROWS(upload.Read(0, bytes), std::invalid_argument);
	SWIM_CHECK_THROWS(upload.Write(UINT64_MAX, bytes), std::invalid_argument);
	SWIM_CHECK_THROWS(upload.Write(15, bytes), std::invalid_argument);
	upload.Write(16, {});
	desc.Memory = Rhi::MemoryPreference::GpuToCpu;
	RhiVulkan::VulkanBuffer readback(capture.State, VK_NULL_HANDLE, nullptr, desc);
	SWIM_CHECK_THROWS(readback.Write(0, bytes), std::invalid_argument);
	SWIM_CHECK_THROWS(readback.Read(16, bytes), std::invalid_argument);
	readback.Read(16, {});
}
