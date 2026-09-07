#include "Tests/Fixtures/VulkanPipelineCapture.h"
#include "Tests/Framework/Test.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Resources/VulkanBuffer.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Resources/VulkanTextureView.h"

using namespace Swim;

namespace
{
	struct VertexDrawCapture : Testing::VulkanPipelineCapture
	{
		RhiVulkan::VulkanTexture Target{ State, VK_NULL_HANDLE, { Rhi::TextureDimension::Texture2D, { 16, 16, 1 },
			Rhi::Format::RGBA8Unorm, Rhi::TextureUsage::ColorAttachment } };
		RhiVulkan::VulkanTextureView View{ State, Target, VK_NULL_HANDLE,
			{ Rhi::TextureViewDimension::Texture2D, Rhi::Format::RGBA8Unorm } };
		Rhi::RenderingAttachmentDesc Attachment{};
		RhiVulkan::VulkanBuffer Vertices{ State, RhiVulkan::FromNativeHandle<VkBuffer>(40), nullptr,
			{ 64, Rhi::BufferUsage::Vertex, Rhi::MemoryPreference::DeviceLocal, {} } };
		RhiVulkan::VulkanBuffer Instances{ State, RhiVulkan::FromNativeHandle<VkBuffer>(41), nullptr,
			{ 40, Rhi::BufferUsage::Vertex, Rhi::MemoryPreference::DeviceLocal, {} } };
		RhiVulkan::VulkanBuffer Indices{ State, VK_NULL_HANDLE, nullptr,
			{ 16, Rhi::BufferUsage::Index, Rhi::MemoryPreference::DeviceLocal, {} } };
		std::unique_ptr<RhiVulkan::VulkanGraphicsPipeline> Pipeline;

		VertexDrawCapture()
		{
			const std::array<Rhi::VertexBindingDesc, 2> bindings{{ { 2, 16 }, { 5, 8, Rhi::VertexInputRate::Instance } }};
			const std::array<Rhi::VertexAttributeDesc, 2> attributes{{
				{ 0, 2, Rhi::Format::RGB32Float, 0 }, { 1, 5, Rhi::Format::RG32Float, 0 }
			}};
			Pipeline = MakePipeline(Rhi::Format::RGBA8Unorm, bindings, attributes);
			Attachment.View = &View;
		}

		void BeginDraw()
		{
			Commands->Begin();
			Commands->BindGraphicsPipeline(*Pipeline);
			Commands->BeginRendering({ { &Attachment, 1 }, nullptr, { 16, 16 } });
			Commands->SetViewport({ 0, 0, 16, 16 });
			Commands->SetScissor({ 0, 0, 16, 16 });
		}

		void Bind()
		{
			Commands->BindVertexBuffer(2, Vertices, 4);
			Commands->BindVertexBuffer(5, Instances, 8);
		}
	};
}

SWIM_TEST("RHI.Vulkan.VertexDraw", "BindingsForwardSparseSlotBufferAndOffset")
{
	VertexDrawCapture capture;
	SWIM_REQUIRE(capture.Pipeline);
	SWIM_CHECK_THROWS(capture.Commands->BindVertexBuffer(2, capture.Vertices, 4), std::logic_error);
	capture.Commands->Begin();
	capture.Commands->BindVertexBuffer(2, capture.Vertices, 4);
	SWIM_CHECK_EQUAL(capture.LastVertexSlot, 2u);
	SWIM_CHECK_EQUAL(capture.LastVertexOffset, 4u);
	SWIM_CHECK_EQUAL(capture.LastVertexBuffer, RhiVulkan::FromNativeHandle<VkBuffer>(40));
	SWIM_CHECK_EQUAL(capture.VertexBindCount, 1u);
	capture.Commands->BindGraphicsPipeline(*capture.Pipeline);
	capture.Commands->BeginRendering({ { &capture.Attachment, 1 }, nullptr, { 16, 16 } });
	capture.Commands->SetViewport({ 0, 0, 16, 16 });
	capture.Commands->SetScissor({ 0, 0, 16, 16 });
	SWIM_CHECK_THROWS(capture.Commands->Draw(3, 1, 0, 0), std::invalid_argument);
	capture.Commands->BindVertexBuffer(5, capture.Instances, 8);
	capture.Commands->Draw(3, 2, 1, 2);
	SWIM_CHECK_EQUAL(capture.DrawCount, 1u);
}

SWIM_TEST("RHI.Vulkan.VertexDraw", "InvalidRebindDoesNotReplaceGoodBinding")
{
	VertexDrawCapture capture;
	capture.BeginDraw();
	capture.Bind();
	SWIM_CHECK_THROWS(capture.Commands->BindVertexBuffer(2, capture.Indices, 0), std::invalid_argument);
	SWIM_CHECK_THROWS(capture.Commands->BindVertexBuffer(32, capture.Vertices, 0), std::invalid_argument);
	SWIM_CHECK_THROWS(capture.Commands->BindVertexBuffer(2, capture.Vertices, 64), std::invalid_argument);
	auto foreignState = std::make_shared<RhiVulkan::VulkanDeviceState>();
	RhiVulkan::VulkanBuffer foreign(foreignState, RhiVulkan::FromNativeHandle<VkBuffer>(42), nullptr,
		{ 64, Rhi::BufferUsage::Vertex });
	SWIM_CHECK_THROWS(capture.Commands->BindVertexBuffer(2, foreign, 0), std::invalid_argument);
	RhiVulkan::VulkanBuffer nullNative(capture.State, VK_NULL_HANDLE, nullptr, { 64, Rhi::BufferUsage::Vertex });
	SWIM_CHECK_THROWS(capture.Commands->BindVertexBuffer(2, nullNative, 0), std::invalid_argument);
	SWIM_CHECK_EQUAL(capture.VertexBindCount, 2u);
	capture.Commands->Draw(4, 4, 0, 0);
	SWIM_CHECK_EQUAL(capture.DrawCount, 1u);
}

SWIM_TEST("RHI.Vulkan.VertexDraw", "DirectAndInstanceRangesUseAttributeEndsNotWholeStride")
{
	VertexDrawCapture capture;
	capture.BeginDraw();
	capture.Bind();
	capture.Commands->Draw(4, 4, 0, 0); // Vertex tail ends exactly at byte 64.
	SWIM_CHECK_THROWS(capture.Commands->Draw(4, 1, 1, 0), std::invalid_argument);
	SWIM_CHECK_THROWS(capture.Commands->Draw(1, 4, 0, 1), std::invalid_argument);
	SWIM_CHECK_THROWS(capture.Commands->Draw(UINT32_MAX, 1, UINT32_MAX, 0), std::invalid_argument);
	SWIM_CHECK_THROWS(capture.Commands->Draw(1, UINT32_MAX, 0, UINT32_MAX), std::invalid_argument);
	SWIM_CHECK_EQUAL(capture.DrawCount, 1u);
	capture.Commands->BindVertexBuffer(2, capture.Vertices, 2);
	SWIM_CHECK_THROWS(capture.Commands->Draw(1, 1, 0, 0), std::invalid_argument);
	capture.Commands->BindVertexBuffer(2, capture.Vertices, 60);
	SWIM_CHECK_THROWS(capture.Commands->Draw(1, 1, 0, 0), std::invalid_argument);
	SWIM_CHECK_EQUAL(capture.DrawCount, 1u);
}

SWIM_TEST("RHI.Vulkan.VertexDraw", "IndexedDrawChecksInstanceRangeWithoutReadingIndexContents")
{
	VertexDrawCapture capture;
	capture.BeginDraw();
	capture.Bind();
	capture.Commands->BindIndexBuffer(capture.Indices, 0, Rhi::IndexType::Uint16);
	capture.Commands->DrawIndexed(3, 2, 1, -1, 2);
	SWIM_CHECK_EQUAL(capture.IndexedDrawCount, 1u);
	SWIM_CHECK_EQUAL(capture.IndexedDraw.vertexOffset, -1);
	SWIM_CHECK_THROWS(capture.Commands->DrawIndexed(3, 2, 1, 0, 3), std::invalid_argument);
	SWIM_CHECK_THROWS(capture.Commands->DrawIndexed(9, 1, 0, 0, 0), std::invalid_argument);
	SWIM_CHECK_EQUAL(capture.IndexedDrawCount, 1u);
}

SWIM_TEST("RHI.Vulkan.VertexDraw", "PipelineSwitchAndPoolReuseHaveDifferentBindingLifetimes")
{
	VertexDrawCapture capture;
	capture.BeginDraw();
	capture.Bind();
	auto generated = capture.MakePipeline();
	capture.Commands->BindGraphicsPipeline(*generated);
	capture.Commands->Draw(999, 1, 999, 0);
	capture.Commands->BindGraphicsPipeline(*capture.Pipeline);
	capture.Commands->Draw(4, 4, 0, 0);
	capture.Commands->EndRendering();
	capture.Commands->BeginRendering({ { &capture.Attachment, 1 }, nullptr, { 16, 16 } });
	capture.Commands->Draw(4, 4, 0, 0);
	capture.Commands->EndRendering();
	capture.Commands->End();
	++capture.Pool->Generation;
	capture.BeginDraw();
	SWIM_CHECK_THROWS(capture.Commands->Draw(4, 4, 0, 0), std::invalid_argument);
	capture.Bind();
	capture.Commands->Draw(4, 4, 0, 0);
	SWIM_CHECK_EQUAL(capture.DrawCount, 4u);
}

SWIM_TEST("RHI.Vulkan.VertexDraw", "ZeroStrideAndEmptyDrawsDoNotOverflowFetchArithmetic")
{
	VertexDrawCapture capture;
	const Rhi::VertexBindingDesc binding{ 2, 0 };
	const Rhi::VertexAttributeDesc attribute{ 0, 2, Rhi::Format::RGBA32Float, 0 };
	auto constant = capture.MakePipeline(Rhi::Format::RGBA8Unorm, { &binding, 1 }, { &attribute, 1 });
	SWIM_REQUIRE(constant);
	capture.BeginDraw();
	capture.Bind();
	capture.Commands->Draw(0, UINT32_MAX, UINT32_MAX, UINT32_MAX);
	capture.Commands->Draw(UINT32_MAX, 0, UINT32_MAX, UINT32_MAX);
	capture.Commands->BindGraphicsPipeline(*constant);
	capture.Commands->Draw(UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX);
	SWIM_CHECK_EQUAL(capture.DrawCount, 3u);
}

SWIM_TEST("RHI.Vulkan.VertexDraw", "WrongQueueStaleGenerationAndDeviceLossStopNativeCalls")
{
	VertexDrawCapture capture;
	capture.BeginDraw();
	capture.Pool->FamilyIndex = 1;
	SWIM_CHECK_THROWS(capture.Commands->BindVertexBuffer(2, capture.Vertices, 4), std::logic_error);
	capture.Pool->FamilyIndex = 0;
	++capture.Pool->Generation;
	SWIM_CHECK_THROWS(capture.Commands->BindVertexBuffer(2, capture.Vertices, 4), std::logic_error);
	capture.BeginDraw();
	capture.State->Diagnostics->TryRecordLoss("vertex test", VK_ERROR_DEVICE_LOST);
	SWIM_CHECK_THROWS(capture.Commands->BindVertexBuffer(2, capture.Vertices, 4), Rhi::DeviceLostError);
	SWIM_CHECK_THROWS(capture.Commands->Draw(3, 1, 0, 0), Rhi::DeviceLostError);
	SWIM_CHECK_THROWS(capture.MakePipeline(), Rhi::DeviceLostError);
	SWIM_CHECK_EQUAL(capture.VertexBindCount, 0u);
	SWIM_CHECK_EQUAL(capture.DrawCount, 0u);
}

SWIM_TEST("RHI.Vulkan.VertexDraw", "ExtremeStridesAnd64BitBufferSizesCannotWrapDrawBounds")
{
	VertexDrawCapture capture;
	capture.State->Device.physical_device.properties.limits.maxVertexInputBindingStride = UINT32_MAX;
	const Rhi::VertexBindingDesc binding{ 2, UINT32_MAX - 3 };
	const Rhi::VertexAttributeDesc attribute{ 0, 2, Rhi::Format::R32Float, 0 };
	auto pipeline = capture.MakePipeline(Rhi::Format::RGBA8Unorm, { &binding, 1 }, { &attribute, 1 });
	SWIM_REQUIRE(pipeline);
	RhiVulkan::VulkanBuffer huge(capture.State, RhiVulkan::FromNativeHandle<VkBuffer>(60), nullptr,
		{ UINT64_MAX, Rhi::BufferUsage::Vertex });
	capture.BeginDraw();
	capture.Commands->BindGraphicsPipeline(*pipeline);
	capture.Commands->BindVertexBuffer(2, huge, 0);
	capture.Commands->Draw(1, 1, UINT32_MAX, 0);
	SWIM_CHECK_THROWS(capture.Commands->Draw(UINT32_MAX, 1, UINT32_MAX, 0), std::invalid_argument);
	SWIM_CHECK_EQUAL(capture.DrawCount, 1u);
}
