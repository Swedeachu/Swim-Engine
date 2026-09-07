#include "Tests/Fixtures/VulkanDescriptorCapture.h"
#include "Tests/Framework/Test.h"

using namespace Swim;

SWIM_TEST("RHI.Vulkan.PushConstantLayout", "RangesOwnReflectionAndPreserveStageSpecificOverlap")
{
	Testing::VulkanPipelineCapture capture;
	std::array<Rhi::PushConstantRange, 2> ranges{{
		{ 8, 24, Rhi::ShaderStageMask::Fragment }, { 0, 16, Rhi::ShaderStageMask::Vertex }
	}};
	auto program = capture.MakeProgram({ {}, ranges });
	SWIM_REQUIRE(program);
	ranges[0].Size = 4;
	auto layout = RhiVulkan::VulkanPipelineLayout::Create(capture.State, { program.get(), {} });
	SWIM_REQUIRE(layout);
	SWIM_REQUIRE_EQUAL(capture.PushConstantRanges.size(), 2u);
	SWIM_CHECK_EQUAL(capture.PushConstantRanges[0].stageFlags, VK_SHADER_STAGE_VERTEX_BIT);
	SWIM_CHECK_EQUAL(capture.PushConstantRanges[0].offset, 0u);
	SWIM_CHECK_EQUAL(capture.PushConstantRanges[0].size, 16u);
	SWIM_CHECK_EQUAL(capture.PushConstantRanges[1].stageFlags, VK_SHADER_STAGE_FRAGMENT_BIT);
	SWIM_CHECK_EQUAL(capture.PushConstantRanges[1].offset, 8u);
	SWIM_CHECK_EQUAL(capture.PushConstantRanges[1].size, 24u);
	SWIM_CHECK_EQUAL(layout->GetInterface().PushConstants[0].Size, 24u);
}

SWIM_TEST("RHI.Vulkan.PushConstantLayout", "InvalidRangesRejectBeforeNativeCreation")
{
	Testing::VulkanPipelineCapture capture;
	const std::array<Rhi::PushConstantRange, 9> invalid{{
		{ 0, 0, Rhi::ShaderStageMask::Vertex }, { 2, 4, Rhi::ShaderStageMask::Vertex },
		{ 0, 6, Rhi::ShaderStageMask::Vertex }, { 128, 4, Rhi::ShaderStageMask::Vertex },
		{ 124, 8, Rhi::ShaderStageMask::Vertex }, { UINT32_MAX - 3, 8, Rhi::ShaderStageMask::Vertex },
		{ 0, 4, Rhi::ShaderStageMask::None }, { 0, 4, Rhi::ShaderStageMask::Compute },
		{ 0, 4, static_cast<Rhi::ShaderStageMask>(0x80000000u) }
	}};
	for (const auto& range : invalid)
	{
		auto program = capture.MakeProgram({ {}, { &range, 1 } });
		SWIM_REQUIRE(program);
		SWIM_CHECK(!RhiVulkan::VulkanPipelineLayout::Create(capture.State, { program.get(), {} }));
	}
	SWIM_CHECK_EQUAL(capture.LayoutsCreated, 0u);
	const Rhi::PushConstantRange valid{ 124, 4, Rhi::ShaderStageMask::Vertex | Rhi::ShaderStageMask::Fragment };
	auto program = capture.MakeProgram({ {}, { &valid, 1 } });
	SWIM_CHECK(RhiVulkan::VulkanPipelineLayout::Create(capture.State, { program.get(), {} }));
	SWIM_CHECK_EQUAL(capture.LayoutsCreated, 1u);
}

SWIM_TEST("RHI.Vulkan.PushConstantLayout", "RepeatedAndAbsentStagesRejectEvenForDisjointRanges")
{
	Testing::VulkanPipelineCapture capture;
	std::array<Rhi::PushConstantRange, 2> ranges{{
		{ 0, 8, Rhi::ShaderStageMask::Vertex }, { 8, 8, Rhi::ShaderStageMask::Vertex }
	}};
	auto program = capture.MakeProgram({ {}, ranges });
	SWIM_CHECK(!RhiVulkan::VulkanPipelineLayout::Create(capture.State, { program.get(), {} }));
	ranges[1].Stages = Rhi::ShaderStageMask::Vertex | Rhi::ShaderStageMask::Fragment;
	program = capture.MakeProgram({ {}, ranges });
	SWIM_CHECK(!RhiVulkan::VulkanPipelineLayout::Create(capture.State, { program.get(), {} }));
	const std::array<std::uint32_t, 5> header{ 0x07230203, 0x00010500, 0, 1, 0 };
	const Rhi::ShaderStageArtifact vertex{ Rhi::ShaderStageMask::Vertex, "main", std::as_bytes(std::span(header)) };
	const Rhi::PushConstantRange fragment{ 0, 4, Rhi::ShaderStageMask::Fragment };
	program = RhiVulkan::VulkanShaderProgram::Create(capture.State, { { &vertex, 1 }, { {}, { &fragment, 1 } }, {} });
	SWIM_REQUIRE(program);
	SWIM_CHECK(!RhiVulkan::VulkanPipelineLayout::Create(capture.State, { program.get(), {} }));
	SWIM_CHECK_EQUAL(capture.LayoutsCreated, 0u);
}

SWIM_TEST("RHI.Vulkan.PushConstantLayout", "DescriptorLayoutsCoexistAndReleaseOnNativeFailure")
{
	Testing::VulkanDescriptorCapture capture;
	const Rhi::DescriptorSchemaDesc schema{ 1, { { 3, Rhi::DescriptorType::UniformBuffer, 1, Rhi::ShaderStageMask::Vertex } } };
	const Rhi::PushConstantRange range{ 0, 16, Rhi::ShaderStageMask::Vertex };
	auto program = capture.MakeProgram({ { &schema, 1 }, { &range, 1 } });
	capture.LayoutResult = VK_ERROR_OUT_OF_HOST_MEMORY;
	SWIM_CHECK(!RhiVulkan::VulkanPipelineLayout::Create(capture.State, { program.get(), {} }));
	SWIM_CHECK_EQUAL(capture.SetsDestroyed, 2u);
	SWIM_CHECK_EQUAL(capture.LayoutsDestroyed, 0u);
	SWIM_REQUIRE_EQUAL(capture.PushConstantRanges.size(), 1u);
	SWIM_CHECK_EQUAL(capture.PushConstantRanges[0].size, 16u);
	capture.LayoutResult = VK_SUCCESS;
	auto layout = RhiVulkan::VulkanPipelineLayout::Create(capture.State, { program.get(), {} });
	SWIM_REQUIRE(layout);
	SWIM_CHECK_EQUAL(layout->GetLayoutState()->Sets.size(), 2u);
	layout.reset();
	SWIM_CHECK_EQUAL(capture.SetsDestroyed, 4u);
	SWIM_CHECK_EQUAL(capture.LayoutsDestroyed, 1u);
}
