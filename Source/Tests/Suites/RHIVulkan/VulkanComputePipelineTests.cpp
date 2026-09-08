#include "Tests/Fixtures/VulkanComputeCapture.h"
#include "Tests/Framework/Test.h"

using namespace Swim;

SWIM_TEST("RHI.Vulkan.ComputePipeline", "NativeStageEntryCacheAndLayoutOwnership")
{
	Testing::VulkanComputeCapture capture;
	auto program = capture.MakeComputeProgram();
	SWIM_REQUIRE(program);
	auto layout = RhiVulkan::VulkanPipelineLayout::Create(capture.State, { program.get(), {} });
	SWIM_REQUIRE(layout);
	auto pipeline = RhiVulkan::VulkanComputePipeline::Create(capture.State, { program.get(), layout.get(), "computeMain", {} });
	SWIM_REQUIRE(pipeline);
	SWIM_CHECK_EQUAL(capture.ComputeStage, VK_SHADER_STAGE_COMPUTE_BIT);
	SWIM_CHECK_EQUAL(capture.ComputeEntry, "computeMain");
	SWIM_CHECK_EQUAL(capture.ComputeModule, program->GetStages()[0].Module);
	SWIM_CHECK_EQUAL(capture.ComputeLayout, layout->GetLayoutState()->Layout);
	SWIM_CHECK(capture.LastPipelineCache != VK_NULL_HANDLE);
	SWIM_CHECK_EQUAL(capture.CachesCreated, 1u);
	layout.reset();
	program.reset();
	SWIM_CHECK_EQUAL(capture.LayoutsDestroyed, 0u);
	SWIM_CHECK_EQUAL(capture.ModulesDestroyed, 1u);
	pipeline.reset();
	SWIM_CHECK_EQUAL(capture.LayoutsDestroyed, 1u);
	SWIM_CHECK_EQUAL(capture.PipelinesDestroyed, 1u);
	auto again = capture.MakeComputePipeline();
	SWIM_REQUIRE(again);
	SWIM_CHECK_EQUAL(capture.CachesCreated, 1u);
}

SWIM_TEST("RHI.Vulkan.ComputePipeline", "RejectsInvalidProgramsLayoutsAndEntryNames")
{
	Testing::VulkanComputeCapture capture;
	auto program = capture.MakeComputeProgram();
	auto other = capture.MakeComputeProgram();
	auto graphics = capture.MakeProgram();
	auto layout = RhiVulkan::VulkanPipelineLayout::Create(capture.State, { program.get(), {} });
	SWIM_REQUIRE(program && other && graphics && layout);
	SWIM_CHECK(!RhiVulkan::VulkanComputePipeline::Create({}, {}));
	SWIM_CHECK(!RhiVulkan::VulkanComputePipeline::Create(capture.State, {}));
	SWIM_CHECK(!RhiVulkan::VulkanComputePipeline::Create(capture.State, { other.get(), layout.get(), {}, {} }));
	SWIM_CHECK(!RhiVulkan::VulkanComputePipeline::Create(capture.State, { graphics.get(), layout.get(), {}, {} }));
	SWIM_CHECK(!RhiVulkan::VulkanComputePipeline::Create(capture.State, { program.get(), layout.get(), "missing", {} }));
	auto foreignState = std::make_shared<RhiVulkan::VulkanDeviceState>();
	SWIM_CHECK(!RhiVulkan::VulkanComputePipeline::Create(foreignState, { program.get(), layout.get(), {}, {} }));
	SWIM_CHECK_EQUAL(capture.PipelinesCreated, 0u);
}

SWIM_TEST("RHI.Vulkan.ComputePipeline", "LocalSizeBoundsAndProductCannotOverflow")
{
	Testing::VulkanComputeCapture capture;
	const auto create = [&](std::array<std::uint32_t, 3> size)
	{
		auto program = capture.MakeComputeProgram({}, size);
		auto layout = RhiVulkan::VulkanPipelineLayout::Create(capture.State, { program.get(), {} });
		return RhiVulkan::VulkanComputePipeline::Create(capture.State, { program.get(), layout.get(), {}, {} });
	};
	for (std::size_t axis = 0; axis < 3; ++axis)
	{
		std::array<std::uint32_t, 3> size{ 1, 1, 1 };
		size[axis] = 0;
		SWIM_CHECK(!create(size));
		size[axis] = 65;
		SWIM_CHECK(!create(size));
	}
	SWIM_CHECK(create({ 64, 2, 1 }) != nullptr);
	SWIM_CHECK(!create({ 64, 2, 2 }));
	auto& limits = capture.State->Device.physical_device.properties.limits;
	limits.maxComputeWorkGroupInvocations = UINT32_MAX;
	for (auto& size : limits.maxComputeWorkGroupSize)
	{
		size = UINT32_MAX;
	}
	SWIM_CHECK(!create({ 65536, 65536, 1 }));
	SWIM_CHECK(!create({ UINT32_MAX, UINT32_MAX, UINT32_MAX }));
	SWIM_CHECK(create({ UINT32_MAX, 1, 1 }) != nullptr);
}

SWIM_TEST("RHI.Vulkan.ComputePipeline", "MixedStageProgramsRejectAndComputeReflectionIsOwned")
{
	Testing::VulkanComputeCapture capture;
	std::array<std::uint32_t, 3> local{ 8, 4, 1 };
	auto program = capture.MakeComputeProgram({}, local);
	SWIM_REQUIRE(program);
	local[0] = 1;
	SWIM_CHECK_EQUAL(program->GetInterface().ComputeThreadGroupSize[0], 8u);
	const std::array<std::uint32_t, 5> header{ 0x07230203, 0x00010500, 0, 1, 0 };
	std::array<Rhi::ShaderStageArtifact, 2> stages{{
		{ Rhi::ShaderStageMask::Vertex, "vertexMain", std::as_bytes(std::span(header)) },
		{ Rhi::ShaderStageMask::Compute, "computeMain", std::as_bytes(std::span(header)) }
	}};
	SWIM_CHECK(!RhiVulkan::VulkanShaderProgram::Create(capture.State, { stages, {}, {} }));
	stages[0] = stages[1];
	SWIM_CHECK(!RhiVulkan::VulkanShaderProgram::Create(capture.State, { stages, {}, {} }));
	SWIM_CHECK_EQUAL(capture.ModulesCreated - capture.ModulesDestroyed, 1u);
}

SWIM_TEST("RHI.Vulkan.ComputePipeline", "NativeFailureReleasesPartialPipelineAndLostDeviceStopsCreation")
{
	Testing::VulkanComputeCapture capture;
	capture.PipelineResult = VK_ERROR_OUT_OF_HOST_MEMORY;
	SWIM_CHECK(!capture.MakeComputePipeline());
	SWIM_CHECK_EQUAL(capture.PipelinesCreated, 1u);
	SWIM_CHECK_EQUAL(capture.PipelinesDestroyed, 1u);
	capture.PipelineResult = VK_ERROR_DEVICE_LOST;
	SWIM_CHECK_THROWS(capture.MakeComputePipeline(), Rhi::DeviceLostError);
	SWIM_CHECK_EQUAL(capture.PipelinesCreated, 2u);
	SWIM_CHECK_EQUAL(capture.PipelinesDestroyed, 2u);
	SWIM_CHECK_THROWS(capture.MakeComputePipeline(), Rhi::DeviceLostError);
	SWIM_CHECK_EQUAL(capture.PipelinesCreated, 2u);
}

SWIM_TEST("RHI.Vulkan.ComputePipeline", "StorageVisibilityAndComputeDescriptorLimitsAreValidated")
{
	Testing::VulkanComputeCapture capture;
	Rhi::DescriptorSchemaDesc schema{ 1, { { 7, Rhi::DescriptorType::StorageBuffer, 1, Rhi::ShaderStageMask::Compute } } };
	const auto create = [&]
	{
		auto program = capture.MakeComputeProgram({ { &schema, 1 }, {} });
		return RhiVulkan::VulkanPipelineLayout::Create(capture.State, { program.get(), {} }) != nullptr;
	};
	SWIM_CHECK(create());
	SWIM_CHECK_EQUAL(capture.SetBindings.back()[0].descriptorType, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
	SWIM_CHECK_EQUAL(capture.SetBindings.back()[0].stageFlags, VK_SHADER_STAGE_COMPUTE_BIT);
	schema.Bindings[0].Stages = Rhi::ShaderStageMask::Fragment;
	SWIM_CHECK(!create());
	auto graphics = capture.MakeProgram({ { &schema, 1 }, {} });
	SWIM_CHECK(!RhiVulkan::VulkanPipelineLayout::Create(capture.State, { graphics.get(), {} }));
	schema.Bindings[0].Stages = Rhi::ShaderStageMask::Compute;
	schema.Bindings[0].Count = 32;
	SWIM_CHECK(create());
	schema.Bindings.push_back({ 3, Rhi::DescriptorType::ReadOnlyStorageBuffer, 1, Rhi::ShaderStageMask::Compute });
	SWIM_CHECK(!create());
	schema.Bindings[0].Count = 31;
	SWIM_CHECK(create());
	capture.State->Device.physical_device.properties.limits.maxDescriptorSetStorageBuffers = 31;
	SWIM_CHECK(!create());
}
