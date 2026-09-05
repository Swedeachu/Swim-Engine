#include "Tests/Fixtures/VulkanPipelineCapture.h"
#include "Tests/Framework/Test.h"

#include <cstring>

using namespace Swim;

SWIM_TEST("RHI.Vulkan.Pipelines", "ShaderArtifactsOwnNamesAndRejectInvalidStagesAndBytes")
{
	Testing::VulkanPipelineCapture capture;
	const std::array<std::uint32_t, 5> header{ 0x07230203, 0x00010500, 0, 1, 0 };
	std::array<std::byte, 21> unaligned{};
	std::memcpy(unaligned.data() + 1, header.data(), 20);
	std::string name = "vertexMain";
	Rhi::ShaderStageArtifact stage{ Rhi::ShaderStageMask::Vertex, name, { unaligned.data() + 1, 20 } };
	auto program = RhiVulkan::VulkanShaderProgram::Create(capture.State, { { &stage, 1 }, {}, {} });
	SWIM_REQUIRE(program);
	name[0] = 'X';
	SWIM_CHECK_EQUAL(program->GetStages()[0].EntryPoint, std::string("vertexMain"));
	stage.Stage = Rhi::ShaderStageMask::AllGraphics;
	SWIM_CHECK(!RhiVulkan::VulkanShaderProgram::Create(capture.State, { { &stage, 1 }, {}, {} }));
	stage.Stage = Rhi::ShaderStageMask::Vertex;
	stage.Bytecode = { unaligned.data() + 1, 19 };
	SWIM_CHECK(!RhiVulkan::VulkanShaderProgram::Create(capture.State, { { &stage, 1 }, {}, {} }));
	stage.Bytecode = { unaligned.data() + 1, 20 };
	unaligned[1] = std::byte{ 0 };
	SWIM_CHECK(!RhiVulkan::VulkanShaderProgram::Create(capture.State, { { &stage, 1 }, {}, {} }));
	SWIM_CHECK_EQUAL(capture.ModulesCreated, 1u);
	program.reset();
	SWIM_CHECK_EQUAL(capture.ModulesDestroyed, 1u);
}

SWIM_TEST("RHI.Vulkan.Pipelines", "ModuleAndPipelineFailuresReleasePartialNativeObjects")
{
	Testing::VulkanPipelineCapture capture;
	capture.FailModule = 2;
	SWIM_CHECK(!capture.MakeProgram());
	SWIM_CHECK_EQUAL(capture.ModulesDestroyed, 1u);
	capture.FailModule = 0;
	capture.PipelineResult = VK_ERROR_OUT_OF_DEVICE_MEMORY;
	SWIM_CHECK(!capture.MakePipeline());
	SWIM_CHECK_EQUAL(capture.PipelinesDestroyed, 1u);
	SWIM_CHECK_EQUAL(capture.LayoutsDestroyed, 1u);
	SWIM_CHECK_EQUAL(capture.ModulesDestroyed, 3u);
}

SWIM_TEST("RHI.Vulkan.Pipelines", "LayoutRejectsForeignProgramAndUnimplementedPushConstants")
{
	Testing::VulkanPipelineCapture capture;
	auto program = capture.MakeProgram();
	SWIM_REQUIRE(program);
	auto foreign = std::make_shared<RhiVulkan::VulkanDeviceState>();
	SWIM_CHECK(!RhiVulkan::VulkanPipelineLayout::Create(foreign, { program.get(), {} }));
	SWIM_CHECK(!RhiVulkan::VulkanPipelineLayout::Create(capture.State, {}));
	const std::array<std::uint32_t, 5> header{ 0x07230203, 0x00010500, 0, 1, 0 };
	Rhi::ShaderStageArtifact stage{ Rhi::ShaderStageMask::Vertex, "main", std::as_bytes(std::span(header)) };
	Rhi::PushConstantRange range{ 0, 16, Rhi::ShaderStageMask::Vertex };
	Rhi::ShaderProgramDesc desc{};
	desc.Stages = { &stage, 1 };
	desc.Interface.PushConstants = { &range, 1 };
	auto withBindings = RhiVulkan::VulkanShaderProgram::Create(capture.State, desc);
	SWIM_REQUIRE(withBindings);
	range.Size = 32;
	SWIM_CHECK_EQUAL(withBindings->GetInterface().PushConstants[0].Size, 16u);
	SWIM_CHECK(!RhiVulkan::VulkanPipelineLayout::Create(capture.State, { withBindings.get(), {} }));
	SWIM_CHECK_EQUAL(capture.LayoutsCreated, 0u);
}

SWIM_TEST("RHI.Vulkan.Pipelines", "GraphicsStateMapsToDynamicRenderingAndOwnsAttachmentSignature")
{
	Testing::VulkanPipelineCapture capture;
	auto program = capture.MakeProgram();
	auto layout = RhiVulkan::VulkanPipelineLayout::Create(capture.State, { program.get(), {} });
	SWIM_REQUIRE(layout);
	Rhi::Format format = Rhi::Format::RGBA8Unorm;
	Rhi::BlendAttachmentState blend{};
	blend.Enabled = true;
	blend.SourceColor = Rhi::BlendFactor::SourceAlpha;
	blend.DestinationColor = Rhi::BlendFactor::OneMinusSourceAlpha;
	Rhi::GraphicsPipelineDesc desc{};
	desc.Program = program.get();
	desc.Layout = layout.get();
	desc.ColorFormats = { &format, 1 };
	desc.BlendAttachments = { &blend, 1 };
	desc.DepthStencilFormat = Rhi::Format::D24UnormS8Uint;
	desc.DepthStencil.DepthCompare = Rhi::CompareOp::GreaterEqual;
	desc.DepthStencil.StencilTest = true;
	desc.DepthStencil.Front.Pass = Rhi::StencilOp::Replace;
	desc.Raster.Winding = Rhi::FrontFace::Clockwise;
	desc.Topology = Rhi::PrimitiveTopology::TriangleStrip;
	auto pipeline = RhiVulkan::VulkanGraphicsPipeline::Create(capture.State, desc);
	SWIM_REQUIRE(pipeline);
	SWIM_CHECK_EQUAL(capture.PipelineColors[0], VK_FORMAT_R8G8B8A8_UNORM);
	SWIM_CHECK_EQUAL(capture.PipelineDepth, VK_FORMAT_D24_UNORM_S8_UINT);
	SWIM_CHECK_EQUAL(capture.PipelineStencil, VK_FORMAT_D24_UNORM_S8_UINT);
	SWIM_CHECK_EQUAL(capture.Topology, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP);
	SWIM_CHECK_EQUAL(capture.Winding, VK_FRONT_FACE_CLOCKWISE);
	SWIM_CHECK_EQUAL(capture.Blend.srcColorBlendFactor, VK_BLEND_FACTOR_SRC_ALPHA);
	SWIM_CHECK_EQUAL(capture.Blend.dstColorBlendFactor, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA);
	SWIM_CHECK_EQUAL(capture.DepthState.depthCompareOp, VK_COMPARE_OP_GREATER_OR_EQUAL);
	SWIM_CHECK_EQUAL(capture.DepthState.front.passOp, VK_STENCIL_OP_REPLACE);
	SWIM_CHECK((capture.DynamicStates == std::vector<VkDynamicState>{ VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR }));
	format = Rhi::Format::BGRA8Unorm;
	SWIM_CHECK(!pipeline->MatchesRendering({ &format, 1 }, desc.DepthStencilFormat, desc.Samples));
	format = Rhi::Format::RGBA8Unorm;
	SWIM_CHECK(pipeline->MatchesRendering({ &format, 1 }, desc.DepthStencilFormat, desc.Samples));
}

SWIM_TEST("RHI.Vulkan.Pipelines", "InvalidGraphicsDescriptionsNeverCreateNativePipelines")
{
	Testing::VulkanPipelineCapture capture;
	auto program = capture.MakeProgram();
	auto layout = RhiVulkan::VulkanPipelineLayout::Create(capture.State, { program.get(), {} });
	Rhi::Format format = Rhi::Format::RGBA8Unorm;
	Rhi::GraphicsPipelineDesc desc{};
	desc.Program = program.get();
	desc.Layout = layout.get();
	desc.ColorFormats = { &format, 1 };
	SWIM_CHECK(!RhiVulkan::VulkanGraphicsPipeline::Create(capture.State, desc));
	desc.DepthStencil.DepthTest = desc.DepthStencil.DepthWrite = false;
	desc.Raster.Wireframe = true;
	SWIM_CHECK(!RhiVulkan::VulkanGraphicsPipeline::Create(capture.State, desc));
	desc.Raster.Wireframe = false;
	desc.Samples = Rhi::SampleCount::X8;
	SWIM_CHECK(!RhiVulkan::VulkanGraphicsPipeline::Create(capture.State, desc));
	desc.Samples = Rhi::SampleCount::X1;
	desc.Topology = static_cast<Rhi::PrimitiveTopology>(255);
	SWIM_CHECK(!RhiVulkan::VulkanGraphicsPipeline::Create(capture.State, desc));
	desc.Topology = Rhi::PrimitiveTopology::TriangleList;
	capture.FormatFeatures = 0;
	SWIM_CHECK(!RhiVulkan::VulkanGraphicsPipeline::Create(capture.State, desc));
	capture.FormatFeatures = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
	format = Rhi::Format::D32Float;
	SWIM_CHECK(!RhiVulkan::VulkanGraphicsPipeline::Create(capture.State, desc));
	format = Rhi::Format::RGBA8Unorm;
	auto otherProgram = capture.MakeProgram();
	desc.Program = otherProgram.get();
	SWIM_CHECK(!RhiVulkan::VulkanGraphicsPipeline::Create(capture.State, desc));
	SWIM_CHECK_EQUAL(capture.PipelinesCreated, 0u);
}
