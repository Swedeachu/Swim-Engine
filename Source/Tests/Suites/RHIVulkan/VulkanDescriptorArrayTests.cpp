#include "Tests/Fixtures/VulkanComputeCapture.h"
#include "Tests/Framework/Test.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Resources/VulkanBuffer.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Resources/VulkanTextureView.h"

#ifdef SWIM_RHI_DESCRIPTOR_ARRAYS_REFLECTION_PATH
#include "Tools/ShaderCompiler/ShaderRhiInterface.h"
#endif

using namespace Swim;

SWIM_TEST("RHI.Vulkan.DescriptorArrays", "BufferElementsRequireCompleteInitializationAndSealTogether")
{
	Testing::VulkanComputeCapture capture;
	Rhi::DescriptorSchemaDesc schema{ 1, { { 9, Rhi::DescriptorType::StorageBuffer, 3, Rhi::ShaderStageMask::Compute } } };
	auto program = capture.MakeComputeProgram({ { &schema, 1 } });
	auto layout = RhiVulkan::VulkanPipelineLayout::Create(capture.State, { program.get(), {} });
	SWIM_REQUIRE(layout);
	auto pipeline = RhiVulkan::VulkanComputePipeline::Create(capture.State, { program.get(), layout.get(), {}, {} });
	auto table = RhiVulkan::VulkanDescriptorTable::Create(capture.State, { layout.get(), 1, 0, {} });
	SWIM_REQUIRE(pipeline && table);
	SWIM_CHECK_EQUAL(capture.SetBindings[1][0].descriptorCount, 3u);
	SWIM_CHECK_EQUAL(capture.PoolSizes[0].descriptorCount, 3u);
	RhiVulkan::VulkanBuffer buffer(capture.State, RhiVulkan::FromNativeHandle<VkBuffer>(1), nullptr,
		{ 256, Rhi::BufferUsage::Storage, Rhi::MemoryPreference::DeviceLocal, {} });
	std::array<Rhi::DescriptorWrite, 3> writes{};
	for (std::uint32_t index = 0; index < writes.size(); ++index)
	{
		writes[index].Binding = 9;
		writes[index].ArrayIndex = index;
		writes[index].BufferResource = &buffer;
		writes[index].BufferOffset = index * 16;
		writes[index].BufferRange = 16;
	}
	capture.Commands->Begin();
	capture.Commands->BindComputePipeline(*pipeline);
	table->Write({ &writes[2], 1 });
	table->Write({ &writes[0], 1 });
	table->Write({ &writes[0], 1 });
	SWIM_CHECK(!table->IsComplete());
	SWIM_CHECK_THROWS(capture.Commands->BindDescriptorTable(1, *table), std::invalid_argument);
	SWIM_CHECK_EQUAL(capture.DescriptorBinds, 0u);
	auto invalid = writes;
	invalid[2].ArrayIndex = 3;
	const auto updates = capture.Updates;
	SWIM_CHECK_THROWS(table->Write(invalid), std::invalid_argument);
	SWIM_CHECK_EQUAL(capture.Updates, updates);
	SWIM_CHECK(!table->IsComplete());
	table->Write({ &writes[1], 1 });
	SWIM_CHECK_EQUAL(capture.Writes[0].dstBinding, 9u);
	SWIM_CHECK_EQUAL(capture.Writes[0].dstArrayElement, 1u);
	SWIM_CHECK_EQUAL(capture.BuffersWritten[0].offset, 16u);
	SWIM_CHECK(table->IsComplete());
	capture.Commands->BindDescriptorTable(1, *table);
	capture.Commands->Dispatch(1, 1, 1);
	SWIM_CHECK_THROWS(table->Write(writes), std::logic_error);
	auto replacement = RhiVulkan::VulkanDescriptorTable::Create(capture.State, { layout.get(), 1, 0, {} });
	SWIM_REQUIRE(replacement);
	replacement->Write(writes);
	capture.Commands->BindDescriptorTable(1, *replacement);
	capture.Commands->Dispatch(1, 1, 1);
	SWIM_CHECK_EQUAL(capture.Dispatches.size(), 2u);
}

SWIM_TEST("RHI.Vulkan.DescriptorArrays", "StorageImageElementMismatchRejectsWholeBatch")
{
	Testing::VulkanComputeCapture capture;
	capture.FormatFeatures |= VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
	auto& limits = capture.State->Device.physical_device.properties.limits;
	limits.maxDescriptorSetStorageImages = limits.maxPerStageDescriptorStorageImages = 8;
	Rhi::DescriptorSchemaDesc schema{ 1, { { 7, Rhi::DescriptorType::StorageTexture, 2,
		Rhi::ShaderStageMask::Compute, false, false, Rhi::Format::R32Uint } } };
	auto program = capture.MakeComputeProgram({ { &schema, 1 } });
	auto layout = RhiVulkan::VulkanPipelineLayout::Create(capture.State, { program.get(), {} });
	SWIM_REQUIRE(layout);
	auto table = RhiVulkan::VulkanDescriptorTable::Create(capture.State, { layout.get(), 1, 0, {} });
	SWIM_REQUIRE(table);
	Rhi::TextureDesc desc{};
	desc.PixelFormat = Rhi::Format::R32Uint;
	desc.Usage = Rhi::TextureUsage::Storage;
	RhiVulkan::VulkanTexture texture(capture.State, VK_NULL_HANDLE, desc);
	Rhi::TextureViewDesc viewDesc{};
	viewDesc.PixelFormat = desc.PixelFormat;
	RhiVulkan::VulkanTextureView view(capture.State, texture, RhiVulkan::FromNativeHandle<VkImageView>(1), viewDesc);
	viewDesc.PixelFormat = Rhi::Format::R32Sint;
	RhiVulkan::VulkanTextureView invalid(capture.State, texture, RhiVulkan::FromNativeHandle<VkImageView>(2), viewDesc);
	std::array<Rhi::DescriptorWrite, 2> writes{};
	writes[0].Binding = writes[1].Binding = 7;
	writes[0].ArrayIndex = 1;
	writes[0].TextureResource = &view;
	writes[1].TextureResource = &invalid;
	SWIM_CHECK_THROWS(table->Write(writes), std::invalid_argument);
	SWIM_CHECK_EQUAL(capture.Updates, 0u);
	SWIM_CHECK(!table->IsComplete());
	writes[1].TextureResource = &view;
	table->Write(writes);
	SWIM_CHECK(table->IsComplete());
	SWIM_REQUIRE_EQUAL(capture.ImagesWritten.size(), 2u);
	SWIM_CHECK_EQUAL(capture.Writes[0].dstArrayElement, 1u);
	SWIM_CHECK_EQUAL(capture.Writes[1].dstArrayElement, 0u);
	SWIM_CHECK_EQUAL(capture.ImagesWritten[1].imageLayout, VK_IMAGE_LAYOUT_GENERAL);
	SWIM_CHECK_EQUAL(capture.PoolSizes[0].descriptorCount, 2u);
}

SWIM_TEST("RHI.Vulkan.DescriptorArrays", "LimitsCountElementsAcrossBindingsBeforeNativeAllocation")
{
	Testing::VulkanComputeCapture capture;
	auto& limits = capture.State->Device.physical_device.properties.limits;
	Rhi::DescriptorSchemaDesc schema{ 0, {
		{ 2, Rhi::DescriptorType::ReadOnlyStorageBuffer, 2, Rhi::ShaderStageMask::Compute },
		{ 3, Rhi::DescriptorType::StorageBuffer, 2, Rhi::ShaderStageMask::Compute }
	} };
	limits.maxPerStageDescriptorStorageBuffers = 3;
	auto program = capture.MakeComputeProgram({ { &schema, 1 } });
	SWIM_CHECK(!RhiVulkan::VulkanPipelineLayout::Create(capture.State, { program.get(), {} }));
	SWIM_CHECK(capture.SetBindings.empty());
	limits.maxPerStageDescriptorStorageBuffers = 4;
	limits.maxDescriptorSetStorageBuffers = 3;
	SWIM_CHECK(!RhiVulkan::VulkanPipelineLayout::Create(capture.State, { program.get(), {} }));
	limits.maxDescriptorSetStorageBuffers = 4;
	limits.maxPerStageResources = 3;
	SWIM_CHECK(!RhiVulkan::VulkanPipelineLayout::Create(capture.State, { program.get(), {} }));
	limits.maxPerStageResources = 4;
	SWIM_CHECK(RhiVulkan::VulkanPipelineLayout::Create(capture.State, { program.get(), {} }));
	schema.Bindings[0].Count = UINT32_MAX;
	program = capture.MakeComputeProgram({ { &schema, 1 } });
	SWIM_CHECK(!RhiVulkan::VulkanPipelineLayout::Create(capture.State, { program.get(), {} }));
}

#ifdef SWIM_RHI_DESCRIPTOR_ARRAYS_REFLECTION_PATH
SWIM_TEST("RHI.Vulkan.DescriptorArrays", "CompiledReflectionCreatesExactNativeArrayLayoutsAndPools")
{
	const auto parsed = ShaderCompiler::LoadSlangReflectionJson(SWIM_RHI_DESCRIPTOR_ARRAYS_REFLECTION_PATH);
	SWIM_REQUIRE(parsed);
	const auto converted = ShaderCompiler::BuildRhiShaderInterface(parsed.Reflection);
	SWIM_REQUIRE_MESSAGE(converted, converted.Error);
	Testing::VulkanComputeCapture capture;
	auto& limits = capture.State->Device.physical_device.properties.limits;
	limits.maxDescriptorSetStorageImages = limits.maxPerStageDescriptorStorageImages = 8;
	const auto& interface = converted.Interface;
	auto program = capture.MakeComputeProgram({ interface.DescriptorSchemas, interface.PushConstants, interface.ComputeThreadGroupSize }, interface.ComputeThreadGroupSize);
	SWIM_REQUIRE(program);
	auto layout = RhiVulkan::VulkanPipelineLayout::Create(capture.State, { program.get(), {} });
	SWIM_REQUIRE(layout);
	SWIM_REQUIRE_EQUAL(capture.SetBindings.size(), 2u);
	std::uint32_t total = 0;
	for (const auto& schema : interface.DescriptorSchemas)
	{
		auto table = RhiVulkan::VulkanDescriptorTable::Create(capture.State, { layout.get(), schema.Space, 0, {} });
		SWIM_REQUIRE(table);
		SWIM_CHECK(!table->IsComplete());
		for (const auto& binding : capture.SetBindings[schema.Space])
		{
			SWIM_CHECK_EQUAL(binding.descriptorCount, 2u);
			SWIM_CHECK_EQUAL(binding.stageFlags, static_cast<VkShaderStageFlags>(VK_SHADER_STAGE_COMPUTE_BIT));
		}
		for (const auto& pool : capture.PoolSizes)
		{
			total += pool.descriptorCount;
		}
	}
	SWIM_CHECK_EQUAL(total, 12u);
}
#endif
