#include "Tests/Fixtures/VulkanComputeCapture.h"
#include "Tests/Framework/Test.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanNativeHandle.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Resources/VulkanTextureView.h"

#ifdef SWIM_RHI_BINDLESS_REFLECTION_PATH
#include "Tools/ShaderCompiler/ShaderRhiInterface.h"
#endif

using namespace Swim;

namespace
{
	constexpr auto AllStages = Rhi::ShaderStageMask::Vertex | Rhi::ShaderStageMask::Fragment | Rhi::ShaderStageMask::Compute;

	// Runtime-sized reflected arrays (Count 0) as the compiled bindless smoke shader produces them.
	Rhi::DescriptorSchemaDesc ReflectedBindless()
	{
		return { 1,
			{ { 0, Rhi::DescriptorType::Sampler, 0, Rhi::ShaderStageMask::Compute },
				{ 1, Rhi::DescriptorType::SampledTexture, 0, Rhi::ShaderStageMask::Compute } } };
	}

	Rhi::DescriptorSchemaDesc SharedBindless(std::uint32_t samplers = 16, std::uint32_t textures = 1000)
	{
		Rhi::DescriptorSchemaDesc space{ 1,
			{ { 0, Rhi::DescriptorType::Sampler, samplers, AllStages }, { 1, Rhi::DescriptorType::SampledTexture, textures, AllStages } } };
		for (auto& binding : space.Bindings)
		{
			binding.PartiallyBound = binding.UpdateAfterBind = true;
		}
		return space;
	}

	void EnableBindless(Testing::VulkanDescriptorCapture& capture)
	{
		capture.State->BindlessDescriptorsEnabled = true;
		auto& indexing = capture.State->DescriptorIndexing;
		indexing.maxDescriptorSetUpdateAfterBindSamplers = indexing.maxPerStageDescriptorUpdateAfterBindSamplers = 4096;
		indexing.maxDescriptorSetUpdateAfterBindSampledImages = indexing.maxPerStageDescriptorUpdateAfterBindSampledImages = 4096;
		indexing.maxDescriptorSetUpdateAfterBindUniformBuffers = indexing.maxPerStageDescriptorUpdateAfterBindUniformBuffers = 64;
		indexing.maxDescriptorSetUpdateAfterBindStorageBuffers = indexing.maxPerStageDescriptorUpdateAfterBindStorageBuffers = 64;
		indexing.maxDescriptorSetUpdateAfterBindStorageImages = indexing.maxPerStageDescriptorUpdateAfterBindStorageImages = 64;
		indexing.maxPerStageUpdateAfterBindResources = 8192;
	}
} // namespace

SWIM_TEST("RHI.Vulkan.Bindless", "ExplicitSpacesSizeRuntimeArraysAndSetUpdateAfterBindFlags")
{
	Testing::VulkanComputeCapture capture;
	const auto reflected = ReflectedBindless();
	auto program = capture.MakeComputeProgram({ { &reflected, 1 }, {} });
	SWIM_REQUIRE(program);
	const auto shared = SharedBindless();
	// Runtime-sized arrays need a capacity, and bindless spaces need a capable device.
	SWIM_CHECK(!RhiVulkan::VulkanPipelineLayout::Create(capture.State, { program.get(), {} }));
	SWIM_CHECK(!RhiVulkan::VulkanPipelineLayout::Create(capture.State, { program.get(), {}, { &shared, 1 } }));
	EnableBindless(capture);
	capture.SetBindings.clear();
	auto layout = RhiVulkan::VulkanPipelineLayout::Create(capture.State, { program.get(), {}, { &shared, 1 } });
	SWIM_REQUIRE(layout);
	SWIM_REQUIRE_EQUAL(capture.SetBindings.size(), 2u);
	SWIM_CHECK(capture.SetBindings[0].empty());
	SWIM_CHECK_EQUAL(capture.SetFlags[0], 0u);
	SWIM_CHECK(capture.SetBindingFlags[0].empty());
	SWIM_CHECK_EQUAL(capture.SetBindings[1][1].descriptorCount, 1000u);
	// Explicit spaces may be visible to stages the compute program lacks.
	SWIM_CHECK_EQUAL(capture.SetBindings[1][1].stageFlags,
		static_cast<VkShaderStageFlags>(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT));
	SWIM_CHECK_EQUAL(
		capture.SetFlags[1], static_cast<VkDescriptorSetLayoutCreateFlags>(VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT));
	const VkDescriptorBindingFlags bindless = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
		VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT;
	SWIM_REQUIRE_EQUAL(capture.SetBindingFlags[1].size(), 2u);
	SWIM_CHECK_EQUAL(capture.SetBindingFlags[1][0], bindless);
	SWIM_CHECK_EQUAL(capture.SetBindingFlags[1][1], bindless);
	// The layout reports the merged interface; plain limits (128 sampled images) exclude the bindless set.
	const auto& merged = layout->GetInterface().DescriptorSchemas[0].Bindings;
	SWIM_CHECK_EQUAL(merged[1].Count, 1000u);
	SWIM_CHECK(merged[1].UpdateAfterBind && merged[1].PartiallyBound);

	for (std::uint32_t invalid = 0; invalid < 9; ++invalid)
	{
		auto bad = shared;
		std::vector<Rhi::DescriptorSchemaDesc> spaces{ bad };
		auto& binding = spaces[0].Bindings[1];
		switch (invalid)
		{
		case 0:
			binding.UpdateAfterBind = false;
			break; // Runtime arrays need the bindless contract.
		case 1:
			binding.PartiallyBound = false;
			break;
		case 2:
			binding.Type = Rhi::DescriptorType::Sampler;
			break;
		case 3:
			binding.Stages = Rhi::ShaderStageMask::Fragment;
			break; // Must cover the reflected compute stage.
		case 4:
			binding.SampledClass = Rhi::SampledTextureClass::Uint;
			break;
		case 5:
			spaces[0].Bindings.pop_back();
			break;
		case 6:
			binding.Count = 5000;
			break; // Beyond update-after-bind limits.
		case 7:
			binding.VariableCount = true;
			break;
		case 8:
			spaces.push_back(spaces[0]);
			break; // Duplicate explicit space.
		}
		SWIM_CHECK(!RhiVulkan::VulkanPipelineLayout::Create(capture.State, { program.get(), {}, spaces }));
	}

	// Update-after-bind stays limited to samplers and sampled textures.
	auto uniform = shared;
	uniform.Bindings.push_back({ 2, Rhi::DescriptorType::UniformBuffer, 1, Rhi::ShaderStageMask::Compute });
	SWIM_CHECK(RhiVulkan::VulkanPipelineLayout::Create(capture.State, { program.get(), {}, { &uniform, 1 } }));
	uniform.Bindings.back().PartiallyBound = uniform.Bindings.back().UpdateAfterBind = true;
	SWIM_CHECK(!RhiVulkan::VulkanPipelineLayout::Create(capture.State, { program.get(), {}, { &uniform, 1 } }));

	// Fixed reflected arrays keep their exact count inside an explicit space.
	Rhi::DescriptorSchemaDesc fixed{ 1, { { 0, Rhi::DescriptorType::Sampler, 4, Rhi::ShaderStageMask::Compute } } };
	auto fixedProgram = capture.MakeComputeProgram({ { &fixed, 1 }, {} });
	SWIM_CHECK(!RhiVulkan::VulkanPipelineLayout::Create(capture.State, { fixedProgram.get(), {}, { &shared, 1 } }));
	const auto fixedShared = SharedBindless(4);
	SWIM_CHECK(RhiVulkan::VulkanPipelineLayout::Create(capture.State, { fixedProgram.get(), {}, { &fixedShared, 1 } }));
	// A program with no reflected bindings still gets the shared space.
	auto empty = capture.MakeComputeProgram({});
	SWIM_CHECK(RhiVulkan::VulkanPipelineLayout::Create(capture.State, { empty.get(), {}, { &shared, 1 } }));
}

SWIM_TEST("RHI.Vulkan.Bindless", "TablesStayWritableAfterBindingAndBindAcrossIdenticalSpaces")
{
	Testing::VulkanComputeCapture capture;
	EnableBindless(capture);
	auto shared = SharedBindless(4, 8);
	shared.Bindings.push_back({ 2, Rhi::DescriptorType::Sampler, 1, Rhi::ShaderStageMask::Compute });
	const auto reflected = ReflectedBindless();
	auto first = capture.MakeComputeProgram({ { &reflected, 1 }, {} });
	auto second = capture.MakeComputeProgram({});
	auto firstLayout = RhiVulkan::VulkanPipelineLayout::Create(capture.State, { first.get(), {}, { &shared, 1 } });
	auto secondLayout = RhiVulkan::VulkanPipelineLayout::Create(capture.State, { second.get(), {}, { &shared, 1 } });
	const auto other = SharedBindless(4, 16);
	auto otherLayout = RhiVulkan::VulkanPipelineLayout::Create(capture.State, { second.get(), {}, { &other, 1 } });
	SWIM_REQUIRE(firstLayout && secondLayout && otherLayout);
	auto firstPipeline = RhiVulkan::VulkanComputePipeline::Create(capture.State, { first.get(), firstLayout.get(), {}, {} });
	auto secondPipeline = RhiVulkan::VulkanComputePipeline::Create(capture.State, { second.get(), secondLayout.get(), {}, {} });
	auto otherPipeline = RhiVulkan::VulkanComputePipeline::Create(capture.State, { second.get(), otherLayout.get(), {}, {} });
	SWIM_REQUIRE(firstPipeline && secondPipeline && otherPipeline);

	auto table = RhiVulkan::VulkanDescriptorTable::Create(capture.State, { firstLayout.get(), 1, 0, "Bindless" });
	SWIM_REQUIRE(table);
	SWIM_CHECK_EQUAL(capture.PoolFlags, static_cast<VkDescriptorPoolCreateFlags>(VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT));
	// Partially bound arrays need no writes; the plain sampler at binding 2 does.
	SWIM_CHECK(!table->IsComplete());
	auto sampler = RhiVulkan::VulkanSampler::Create(capture.State, {});
	SWIM_REQUIRE(sampler);
	Rhi::DescriptorWrite plain{};
	plain.Binding = 2;
	plain.SamplerResource = sampler.get();
	table->Write({ &plain, 1 });
	SWIM_CHECK(table->IsComplete());

	auto& commands = *capture.Commands;
	commands.Begin();
	commands.BindComputePipeline(*secondPipeline);
	commands.BindDescriptorTable(1, *table); // Created from another layout with an identical space.
	commands.BindComputePipeline(*otherPipeline);
	SWIM_CHECK_THROWS(commands.BindDescriptorTable(1, *table), std::invalid_argument);
	commands.BindComputePipeline(*firstPipeline);
	commands.BindDescriptorTable(1, *table);
	SWIM_CHECK_EQUAL(capture.DescriptorBinds, 2u);

	// After binding, bindless elements stay writable and the plain binding is frozen.
	Rhi::TextureDesc textureDesc{};
	textureDesc.PixelFormat = Rhi::Format::RGBA8Unorm;
	textureDesc.Usage = Rhi::TextureUsage::Sampled;
	textureDesc.Extent = { 4, 4, 1 };
	RhiVulkan::VulkanTexture texture(capture.State, VK_NULL_HANDLE, textureDesc);
	Rhi::TextureViewDesc viewDesc{};
	viewDesc.PixelFormat = textureDesc.PixelFormat;
	RhiVulkan::VulkanTextureView view(capture.State, texture, RhiVulkan::FromNativeHandle<VkImageView>(7), viewDesc);
	std::array<Rhi::DescriptorWrite, 2> late{};
	late[0].Binding = 1;
	late[0].ArrayIndex = 7;
	late[0].TextureResource = &view;
	late[1].Binding = 0;
	late[1].ArrayIndex = 3;
	late[1].SamplerResource = sampler.get();
	const auto updates = capture.Updates;
	table->Write(late);
	SWIM_CHECK_EQUAL(capture.Updates, updates + 1);
	SWIM_REQUIRE_EQUAL(capture.Writes.size(), 2u);
	SWIM_CHECK_EQUAL(capture.Writes[0].dstArrayElement, 7u);
	SWIM_CHECK_EQUAL(capture.ImagesWritten[0].imageView, RhiVulkan::FromNativeHandle<VkImageView>(7));
	SWIM_CHECK_THROWS(table->Write({ &plain, 1 }), std::logic_error);
	// A mixed batch is rejected whole, before any native write.
	std::array<Rhi::DescriptorWrite, 2> mixed{ late[1], plain };
	SWIM_CHECK_THROWS(table->Write(mixed), std::logic_error);
	SWIM_CHECK_EQUAL(capture.Updates, updates + 1);
	late[0].ArrayIndex = 8;
	SWIM_CHECK_THROWS(table->Write({ late.data(), 1 }), std::invalid_argument);
	commands.End();
}

#ifdef SWIM_RHI_BINDLESS_REFLECTION_PATH
SWIM_TEST("RHI.Vulkan.Bindless", "CompiledUnboundedArraysReflectAsRuntimeSizedBindings")
{
	const auto parsed = ShaderCompiler::LoadSlangReflectionJson(SWIM_RHI_BINDLESS_REFLECTION_PATH);
	SWIM_REQUIRE(parsed);
	const auto converted = ShaderCompiler::BuildRhiShaderInterface(parsed.Reflection);
	SWIM_REQUIRE_MESSAGE(converted, converted.Error);
	const auto& interface = converted.Interface;
	SWIM_REQUIRE_EQUAL(interface.DescriptorSchemas.size(), 2u);
	const auto* space = &interface.DescriptorSchemas[0];
	if (space->Space != 1)
	{
		space = &interface.DescriptorSchemas[1];
	}
	SWIM_REQUIRE_EQUAL(space->Bindings.size(), 2u);
	for (const auto& binding : space->Bindings)
	{
		SWIM_CHECK_EQUAL(binding.Count, 0u);
		SWIM_CHECK_EQUAL(binding.Stages, Rhi::ShaderStageMask::Compute);
	}
	Testing::VulkanComputeCapture capture;
	EnableBindless(capture);
	auto program = capture.MakeComputeProgram(
		{ interface.DescriptorSchemas, interface.PushConstants, interface.ComputeThreadGroupSize }, interface.ComputeThreadGroupSize);
	SWIM_REQUIRE(program);
	SWIM_CHECK(!RhiVulkan::VulkanPipelineLayout::Create(capture.State, { program.get(), {} }));
	const auto shared = SharedBindless(8, 64);
	SWIM_CHECK(RhiVulkan::VulkanPipelineLayout::Create(capture.State, { program.get(), {}, { &shared, 1 } }));
}
#endif
