#include "Tests/Fixtures/VulkanDescriptorCapture.h"
#include "Tests/Framework/Test.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Resources/VulkanBuffer.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Resources/VulkanTextureView.h"

#include <limits>

using namespace Swim;

SWIM_TEST("RHI.Vulkan.Descriptors", "SparseSpacesPreserveBindingsAndRejectInvalidLayouts")
{
	Testing::VulkanDescriptorCapture capture;
	Rhi::DescriptorSchemaDesc schema{ 2, {
		{ 7, Rhi::DescriptorType::Sampler, 2, Rhi::ShaderStageMask::Fragment },
		{ 3, Rhi::DescriptorType::SampledTexture, 1, Rhi::ShaderStageMask::Fragment }
	} };
	auto program = capture.MakeProgram({ { &schema, 1 }, {} });
	auto layout = RhiVulkan::VulkanPipelineLayout::Create(capture.State, { program.get(), {} });
	SWIM_REQUIRE(layout);
	SWIM_REQUIRE_EQUAL(capture.SetBindings.size(), 3u);
	SWIM_CHECK(capture.SetBindings[0].empty() && capture.SetBindings[1].empty());
	SWIM_CHECK_EQUAL(capture.SetBindings[2][0].binding, 3u);
	SWIM_CHECK_EQUAL(capture.SetBindings[2][1].descriptorCount, 2u);
	SWIM_CHECK_EQUAL(capture.SetBindings[2][1].stageFlags, static_cast<VkShaderStageFlags>(VK_SHADER_STAGE_FRAGMENT_BIT));
	for (std::uint32_t invalid = 0; invalid < 6; ++invalid)
	{
		auto bad = schema;
		if (invalid == 0)
		{
			bad.Bindings[0].Count = 0;
		}
		if (invalid == 1)
		{
			bad.Bindings[0].VariableCount = true;
		}
		if (invalid == 2)
		{
			bad.Bindings[0].PartiallyBound = true;
		}
		if (invalid == 3)
		{
			bad.Bindings[0].Binding = 3;
		}
		if (invalid == 4)
		{
			bad.Space = 8;
		}
		if (invalid == 5)
		{
			bad.Bindings[0].Stages = Rhi::ShaderStageMask::Compute;
		}
		auto invalidProgram = capture.MakeProgram({ { &bad, 1 }, {} });
		SWIM_CHECK(!RhiVulkan::VulkanPipelineLayout::Create(capture.State, { invalidProgram.get(), {} }));
	}
	SWIM_CHECK_EQUAL(capture.SetBindings.size(), 3u);
	capture.State->Device.physical_device.properties.limits.maxPerStageDescriptorSamplers = 1;
	SWIM_CHECK(!RhiVulkan::VulkanPipelineLayout::Create(capture.State, { program.get(), {} }));
}

SWIM_TEST("RHI.Vulkan.Descriptors", "FailedLayoutAndSetAllocationReleaseNativeOwners")
{
	Testing::VulkanDescriptorCapture capture;
	Rhi::DescriptorSchemaDesc schema{ 2, { { 0, Rhi::DescriptorType::Sampler, 1, Rhi::ShaderStageMask::Fragment } } };
	auto program = capture.MakeProgram({ { &schema, 1 }, {} });
	capture.FailSet = 3;
	SWIM_CHECK(!RhiVulkan::VulkanPipelineLayout::Create(capture.State, { program.get(), {} }));
	SWIM_CHECK_EQUAL(capture.SetsDestroyed, 2u);
	capture.FailSet = 0;
	auto layout = RhiVulkan::VulkanPipelineLayout::Create(capture.State, { program.get(), {} });
	SWIM_REQUIRE(layout);
	SWIM_CHECK(!RhiVulkan::VulkanDescriptorTable::Create(capture.State, { layout.get(), 1, 0, {} }));
	SWIM_CHECK(!RhiVulkan::VulkanDescriptorTable::Create(capture.State, { layout.get(), 2, 4, {} }));
	capture.AllocationResult = VK_ERROR_OUT_OF_POOL_MEMORY;
	SWIM_CHECK(!RhiVulkan::VulkanDescriptorTable::Create(capture.State, { layout.get(), 2, 0, {} }));
	SWIM_CHECK_EQUAL(capture.PoolsDestroyed, 1u);
	capture.AllocationResult = VK_SUCCESS;
	auto table = RhiVulkan::VulkanDescriptorTable::Create(capture.State, { layout.get(), 2, 0, {} });
	SWIM_REQUIRE(table);
	SWIM_CHECK_EQUAL(capture.PoolSizes[0].descriptorCount, 1u);
	table.reset();
	SWIM_CHECK_EQUAL(capture.PoolsDestroyed, 2u);
}

SWIM_TEST("RHI.Vulkan.Descriptors", "WritesValidateEntireBatchAndInitializeEveryArrayElement")
{
	Testing::VulkanDescriptorCapture capture;
	Rhi::DescriptorSchemaDesc schema{ 0, { { 4, Rhi::DescriptorType::Sampler, 2, Rhi::ShaderStageMask::Fragment } } };
	auto program = capture.MakeProgram({ { &schema, 1 }, {} });
	auto layout = RhiVulkan::VulkanPipelineLayout::Create(capture.State, { program.get(), {} });
	auto table = RhiVulkan::VulkanDescriptorTable::Create(capture.State, { layout.get(), 0, 0, {} });
	auto sampler = RhiVulkan::VulkanSampler::Create(capture.State, {});
	SWIM_REQUIRE(table && sampler);
	std::array<Rhi::DescriptorWrite, 2> writes{};
	writes[0].Binding = writes[1].Binding = 4;
	writes[0].SamplerResource = writes[1].SamplerResource = sampler.get();
	writes[1].ArrayIndex = 2;
	SWIM_CHECK_THROWS(table->Write(writes), std::invalid_argument);
	SWIM_CHECK_EQUAL(capture.Updates, 0u);
	SWIM_CHECK(!table->IsComplete());
	table->Write({ writes.data(), 1 });
	SWIM_CHECK(!table->IsComplete());
	writes[1].ArrayIndex = 1;
	table->Write({ &writes[1], 1 });
	SWIM_CHECK(table->IsComplete());
	SWIM_CHECK_EQUAL(capture.Writes[0].dstArrayElement, 1u);
	SWIM_CHECK_EQUAL(capture.ImagesWritten[0].sampler, RhiVulkan::FromNativeHandle<VkSampler>(sampler->GetNativeHandle()));
	writes[0].Binding = 5;
	SWIM_CHECK_THROWS(table->Write({ writes.data(), 1 }), std::invalid_argument);
	SWIM_CHECK_EQUAL(capture.Updates, 2u);
}

SWIM_TEST("RHI.Vulkan.Descriptors", "BufferDescriptorsValidateUsageRangeAlignmentAndDevice")
{
	Testing::VulkanDescriptorCapture capture;
	Rhi::DescriptorSchemaDesc schema{ 0, { { 0, Rhi::DescriptorType::UniformBuffer, 1, Rhi::ShaderStageMask::Vertex } } };
	auto program = capture.MakeProgram({ { &schema, 1 }, {} });
	auto layout = RhiVulkan::VulkanPipelineLayout::Create(capture.State, { program.get(), {} });
	auto table = RhiVulkan::VulkanDescriptorTable::Create(capture.State, { layout.get(), 0, 0, {} });
	SWIM_REQUIRE(table);
	RhiVulkan::VulkanBuffer buffer(capture.State, RhiVulkan::FromNativeHandle<VkBuffer>(1), nullptr,
		{ 256, Rhi::BufferUsage::Uniform, Rhi::MemoryPreference::CpuToGpu, {} });
	Rhi::DescriptorWrite write{};
	write.BufferResource = &buffer;
	write.BufferOffset = 16;
	table->Write({ &write, 1 });
	SWIM_CHECK_EQUAL(capture.BuffersWritten[0].range, 240u);
	write.BufferOffset = 1;
	SWIM_CHECK_THROWS(table->Write({ &write, 1 }), std::invalid_argument);
	write.BufferOffset = UINT64_MAX;
	SWIM_CHECK_THROWS(table->Write({ &write, 1 }), std::invalid_argument);
	write.BufferOffset = 16;
	write.BufferRange = 241;
	SWIM_CHECK_THROWS(table->Write({ &write, 1 }), std::invalid_argument);
	auto foreignState = std::make_shared<RhiVulkan::VulkanDeviceState>();
	RhiVulkan::VulkanBuffer foreign(foreignState, RhiVulkan::FromNativeHandle<VkBuffer>(2), nullptr,
		{ 256, Rhi::BufferUsage::Uniform, Rhi::MemoryPreference::CpuToGpu, {} });
	write.BufferResource = &foreign;
	SWIM_CHECK_THROWS(table->Write({ &write, 1 }), std::invalid_argument);
	SWIM_CHECK_EQUAL(capture.Updates, 1u);
}

SWIM_TEST("RHI.Vulkan.Descriptors", "SampledViewsRequireCorrectUsageAndFilterableColorFormats")
{
	Testing::VulkanDescriptorCapture capture;
	Rhi::DescriptorSchemaDesc schema{ 0, { { 0, Rhi::DescriptorType::SampledTexture, 1, Rhi::ShaderStageMask::Fragment } } };
	auto program = capture.MakeProgram({ { &schema, 1 }, {} });
	auto layout = RhiVulkan::VulkanPipelineLayout::Create(capture.State, { program.get(), {} });
	auto table = RhiVulkan::VulkanDescriptorTable::Create(capture.State, { layout.get(), 0, 0, {} });
	SWIM_REQUIRE(table);
	Rhi::TextureDesc desc{};
	desc.PixelFormat = Rhi::Format::RGBA8Unorm;
	desc.Usage = Rhi::TextureUsage::Sampled;
	RhiVulkan::VulkanTexture texture(capture.State, VK_NULL_HANDLE, desc);
	Rhi::TextureViewDesc viewDesc{};
	viewDesc.PixelFormat = desc.PixelFormat;
	RhiVulkan::VulkanTextureView view(capture.State, texture, RhiVulkan::FromNativeHandle<VkImageView>(1), viewDesc);
	Rhi::DescriptorWrite write{};
	write.TextureResource = &view;
	table->Write({ &write, 1 });
	SWIM_CHECK_EQUAL(capture.ImagesWritten[0].imageLayout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	capture.FormatFeatures = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
	SWIM_CHECK_THROWS(table->Write({ &write, 1 }), std::invalid_argument);
	write.TextureResource = nullptr;
	SWIM_CHECK_THROWS(table->Write({ &write, 1 }), std::invalid_argument);
	SWIM_CHECK_EQUAL(capture.Updates, 1u);
}

SWIM_TEST("RHI.Vulkan.Samplers", "SamplerStateAndAllocationBudgetAreValidatedAndReleased")
{
	Testing::VulkanDescriptorCapture capture;
	capture.State->Device.physical_device.properties.limits.maxSamplerAllocationCount = 1;
	Rhi::SamplerDesc desc{};
	desc.MinFilter = desc.MagFilter = desc.MipFilter = Rhi::Filter::Nearest;
	desc.AddressU = Rhi::SamplerAddressMode::ClampToEdge;
	desc.MaxLod = 3;
	auto sampler = RhiVulkan::VulkanSampler::Create(capture.State, desc);
	SWIM_REQUIRE(sampler);
	SWIM_CHECK_EQUAL(capture.SamplerInfo.minFilter, VK_FILTER_NEAREST);
	SWIM_CHECK_EQUAL(capture.SamplerInfo.mipmapMode, VK_SAMPLER_MIPMAP_MODE_NEAREST);
	SWIM_CHECK_EQUAL(capture.SamplerInfo.addressModeU, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
	SWIM_CHECK(!RhiVulkan::VulkanSampler::Create(capture.State, desc));
	sampler.reset();
	SWIM_CHECK_EQUAL(capture.SamplersDestroyed, 1u);
	capture.SamplerResult = VK_ERROR_OUT_OF_HOST_MEMORY;
	SWIM_CHECK(!RhiVulkan::VulkanSampler::Create(capture.State, desc));
	SWIM_CHECK_EQUAL(capture.State->SamplerCount.load(), 0u);
	capture.SamplerResult = VK_SUCCESS;
	desc.MinLod = std::numeric_limits<float>::quiet_NaN();
	SWIM_CHECK(!RhiVulkan::VulkanSampler::Create(capture.State, desc));
	desc.MinLod = 4;
	SWIM_CHECK(!RhiVulkan::VulkanSampler::Create(capture.State, desc));
	desc.MinLod = 0;
	desc.EnableAnisotropy = true;
	SWIM_CHECK(!RhiVulkan::VulkanSampler::Create(capture.State, desc));
	SWIM_CHECK_EQUAL(capture.SamplersCreated, 2u);
}
