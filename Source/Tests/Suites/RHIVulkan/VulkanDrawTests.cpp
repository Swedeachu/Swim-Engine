#include "Tests/Fixtures/VulkanPipelineCapture.h"
#include "Tests/Framework/Test.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Resources/VulkanBuffer.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Resources/VulkanTextureView.h"

using namespace Swim;

namespace
{

	struct DrawTarget
	{
		explicit DrawTarget(Testing::VulkanPipelineCapture& capture)
			: Texture(capture.State, VK_NULL_HANDLE, { Rhi::TextureDimension::Texture2D, { 16, 16, 1 },
				Rhi::Format::RGBA8Unorm, Rhi::TextureUsage::ColorAttachment, 1, 1, Rhi::SampleCount::X1, {} }),
			  View(capture.State, Texture, VK_NULL_HANDLE, { Rhi::TextureViewDimension::Texture2D, Rhi::Format::RGBA8Unorm, 0, 1, 0, 1, {} })
		{
			Attachment.View = &View;
		}

		RhiVulkan::VulkanTexture Texture;
		RhiVulkan::VulkanTextureView View;
		Rhi::RenderingAttachmentDesc Attachment{};
	};

} // namespace

SWIM_TEST("RHI.Vulkan.Draw", "DrawRequiresScopePipelineAndDynamicStateAndResetsAfterPoolReuse")
{
	Testing::VulkanPipelineCapture capture;
	auto pipeline = capture.MakePipeline();
	SWIM_REQUIRE(pipeline);
	DrawTarget target(capture);
	auto& commands = *capture.Commands;
	SWIM_CHECK_THROWS(commands.Draw(3, 1, 0, 0), std::logic_error);
	commands.Begin();
	commands.BindGraphicsPipeline(*pipeline);
	SWIM_CHECK_THROWS(commands.Draw(3, 1, 0, 0), std::logic_error);
	commands.BeginRendering({ { &target.Attachment, 1 }, nullptr, { 16, 16 } });
	SWIM_CHECK_THROWS(commands.Draw(3, 1, 0, 0), std::logic_error);
	commands.SetViewport({ 0, 0, 16, 16 });
	SWIM_CHECK_THROWS(commands.Draw(3, 1, 0, 0), std::logic_error);
	commands.SetScissor({ 0, 0, 16, 16 });
	commands.Draw(3, 2, 4, 5);
	SWIM_CHECK_EQUAL(capture.DrawCount, 1u);
	SWIM_CHECK_EQUAL(capture.Draw.vertexCount, 3u);
	SWIM_CHECK_EQUAL(capture.Draw.instanceCount, 2u);
	SWIM_CHECK_EQUAL(capture.Draw.firstVertex, 4u);
	SWIM_CHECK_EQUAL(capture.Draw.firstInstance, 5u);
	commands.EndRendering();
	commands.End();
	++capture.Pool->Generation;
	commands.Begin();
	commands.BeginRendering({ { &target.Attachment, 1 }, nullptr, { 16, 16 } });
	SWIM_CHECK_THROWS(commands.Draw(3, 1, 0, 0), std::logic_error);
	SWIM_CHECK_EQUAL(capture.DrawCount, 1u);
	commands.EndRendering();
	commands.End();
}

SWIM_TEST("RHI.Vulkan.Draw", "AttachmentMismatchAndForeignPipelinesNeverDraw")
{
	Testing::VulkanPipelineCapture capture;
	auto wrongFormat = capture.MakePipeline(Rhi::Format::BGRA8Unorm);
	SWIM_REQUIRE(wrongFormat);
	DrawTarget target(capture);
	auto& commands = *capture.Commands;
	commands.Begin();
	commands.BindGraphicsPipeline(*wrongFormat);
	commands.SetViewport({ 0, 0, 16, 16 });
	commands.SetScissor({ 0, 0, 16, 16 });
	commands.BeginRendering({ { &target.Attachment, 1 }, nullptr, { 16, 16 } });
	SWIM_CHECK_THROWS(commands.Draw(3, 1, 0, 0), std::invalid_argument);
	auto foreignState = std::make_shared<RhiVulkan::VulkanDeviceState>();
	RhiVulkan::VulkanGraphicsPipeline foreign(foreignState, {});
	SWIM_CHECK_THROWS(commands.BindGraphicsPipeline(foreign), std::invalid_argument);
	SWIM_CHECK_EQUAL(capture.BindCount, 1u);
	SWIM_CHECK_EQUAL(capture.DrawCount, 0u);
	commands.EndRendering();
	commands.End();
}

SWIM_TEST("RHI.Vulkan.Draw", "IndexedDrawChecksUsageAlignmentAndRemainingBufferRange")
{
	Testing::VulkanPipelineCapture capture;
	auto pipeline = capture.MakePipeline();
	SWIM_REQUIRE(pipeline);
	DrawTarget target(capture);
	RhiVulkan::VulkanBuffer indices(capture.State, VK_NULL_HANDLE, nullptr, { 16, Rhi::BufferUsage::Index, Rhi::MemoryPreference::DeviceLocal, {} });
	RhiVulkan::VulkanBuffer wrongUsage(capture.State, VK_NULL_HANDLE, nullptr, { 16, Rhi::BufferUsage::Vertex, Rhi::MemoryPreference::DeviceLocal, {} });
	auto& commands = *capture.Commands;
	commands.Begin();
	SWIM_CHECK_THROWS(commands.BindIndexBuffer(wrongUsage, 0, Rhi::IndexType::Uint16), std::invalid_argument);
	SWIM_CHECK_THROWS(commands.BindIndexBuffer(indices, 1, Rhi::IndexType::Uint16), std::invalid_argument);
	SWIM_CHECK_THROWS(commands.BindIndexBuffer(indices, 16, Rhi::IndexType::Uint16), std::invalid_argument);
	commands.BindGraphicsPipeline(*pipeline);
	commands.SetViewport({ 0, 0, 16, 16 });
	commands.SetScissor({ 0, 0, 16, 16 });
	commands.BeginRendering({ { &target.Attachment, 1 }, nullptr, { 16, 16 } });
	SWIM_CHECK_THROWS(commands.DrawIndexed(3, 1, 0, 0, 0), std::invalid_argument);
	commands.BindIndexBuffer(indices, 4, Rhi::IndexType::Uint16);
	SWIM_CHECK_THROWS(commands.DrawIndexed(3, 1, 4, 0, 0), std::invalid_argument);
	SWIM_CHECK_THROWS(commands.DrawIndexed(UINT32_MAX, 1, UINT32_MAX, 0, 0), std::invalid_argument);
	commands.DrawIndexed(3, 2, 3, -2, 7);
	SWIM_CHECK_EQUAL(capture.IndexedDrawCount, 1u);
	SWIM_CHECK_EQUAL(capture.IndexedDraw.firstIndex, 3u);
	SWIM_CHECK_EQUAL(capture.IndexedDraw.vertexOffset, -2);
	SWIM_CHECK_EQUAL(capture.IndexedDraw.firstInstance, 7u);
	commands.EndRendering();
	commands.End();
}
