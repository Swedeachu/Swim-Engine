#include "Tests/Fixtures/VulkanDescriptorCapture.h"
#include "Tests/Framework/Test.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Resources/VulkanTextureView.h"

using namespace Swim;

SWIM_TEST("RHI.Vulkan.Descriptors", "DrawRequiresCompleteTablesAndRecordedTablesCannotBeMutated")
{
	Testing::VulkanDescriptorCapture capture;
	Rhi::DescriptorSchemaDesc schema{ 2, { { 0, Rhi::DescriptorType::Sampler, 1, Rhi::ShaderStageMask::Fragment } } };
	auto program = capture.MakeProgram({ { &schema, 1 }, {} });
	auto layout = RhiVulkan::VulkanPipelineLayout::Create(capture.State, { program.get(), {} });
	SWIM_REQUIRE(layout);
	const Rhi::Format format = Rhi::Format::RGBA8Unorm;
	Rhi::GraphicsPipelineDesc desc{};
	desc.Program = program.get();
	desc.Layout = layout.get();
	desc.ColorFormats = { &format, 1 };
	desc.DepthStencil.DepthTest = desc.DepthStencil.DepthWrite = false;
	auto pipeline = RhiVulkan::VulkanGraphicsPipeline::Create(capture.State, desc);
	auto table = RhiVulkan::VulkanDescriptorTable::Create(capture.State, { layout.get(), 2, 0, {} });
	auto sampler = RhiVulkan::VulkanSampler::Create(capture.State, {});
	SWIM_REQUIRE(pipeline && table && sampler);
	Rhi::TextureDesc targetDesc{};
	targetDesc.PixelFormat = format;
	targetDesc.Usage = Rhi::TextureUsage::ColorAttachment;
	targetDesc.Extent = { 16, 16, 1 };
	RhiVulkan::VulkanTexture target(capture.State, VK_NULL_HANDLE, targetDesc);
	Rhi::TextureViewDesc viewDesc{};
	viewDesc.PixelFormat = format;
	RhiVulkan::VulkanTextureView view(capture.State, target, VK_NULL_HANDLE, viewDesc);
	Rhi::RenderingAttachmentDesc attachment{};
	attachment.View = &view;
	auto& commands = *capture.Commands;
	commands.Begin();
	SWIM_CHECK_THROWS(commands.BindDescriptorTable(2, *table), std::logic_error);
	commands.BindGraphicsPipeline(*pipeline);
	SWIM_CHECK_THROWS(commands.BindDescriptorTable(2, *table), std::invalid_argument);
	Rhi::DescriptorWrite write{};
	write.SamplerResource = sampler.get();
	table->Write({ &write, 1 });
	SWIM_CHECK_THROWS(commands.BindDescriptorTable(0, *table), std::invalid_argument);
	commands.BeginRendering({ { &attachment, 1 }, nullptr, { 16, 16 } });
	commands.SetViewport({ 0, 0, 16, 16 });
	commands.SetScissor({ 0, 0, 16, 16 });
	SWIM_CHECK_THROWS(commands.Draw(3, 1, 0, 0), std::logic_error);
	commands.BindDescriptorTable(2, *table);
	commands.Draw(3, 1, 0, 0);
	SWIM_CHECK_EQUAL(capture.DrawCount, 1u);
	SWIM_CHECK_EQUAL(capture.BoundSpace, 2u);
	SWIM_CHECK_THROWS(table->Write({ &write, 1 }), std::logic_error);
	commands.BindGraphicsPipeline(*pipeline);
	SWIM_CHECK_THROWS(commands.Draw(3, 1, 0, 0), std::logic_error);
	commands.BindDescriptorTable(2, *table);
	commands.Draw(3, 1, 0, 0);
	commands.EndRendering();
	commands.End();
	SWIM_CHECK_EQUAL(capture.DrawCount, 2u);
	// Pipeline retains native layout objects after public creation objects go away.
	table.reset();
	layout.reset();
	program.reset();
	SWIM_CHECK_EQUAL(capture.LayoutsDestroyed, 0u);
	pipeline.reset();
	SWIM_CHECK_EQUAL(capture.LayoutsDestroyed, 1u);
	SWIM_CHECK_EQUAL(capture.SetsDestroyed, 3u);
}
