#include "Tests/Fixtures/VulkanComputeCapture.h"
#include "Tests/Framework/Test.h"
#include "Engine/Systems/Renderer/RHI/RhiSampledTexture.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Descriptors/VulkanDescriptorImages.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Resources/VulkanTextureView.h"

#ifdef SWIM_RHI_SAMPLED_INTEGER_REFLECTION_PATH
#include "Tools/ShaderCompiler/ShaderRhiInterface.h"
#endif

using namespace Swim;

SWIM_TEST("RHI.Vulkan.SampledTextures", "SignednessWidthAndFilteringAreIndependent")
{
	Testing::VulkanDescriptorCapture capture;
	Rhi::DescriptorBindingDesc binding{};
	binding.SampledClass = Rhi::SampledTextureClass::Uint;
	struct Case
	{
		Rhi::Format Format;
		Rhi::SampledTextureClass Class;
	};
	const std::array cases{
		Case{ Rhi::Format::R8Uint, Rhi::SampledTextureClass::Uint },
		Case{ Rhi::Format::RG16Uint, Rhi::SampledTextureClass::Uint },
		Case{ Rhi::Format::R32Uint, Rhi::SampledTextureClass::Uint },
		Case{ Rhi::Format::RGBA32Uint, Rhi::SampledTextureClass::Uint },
		Case{ Rhi::Format::RGB10A2Uint, Rhi::SampledTextureClass::Uint },
		Case{ Rhi::Format::R8Sint, Rhi::SampledTextureClass::Sint },
		Case{ Rhi::Format::RG16Sint, Rhi::SampledTextureClass::Sint },
		Case{ Rhi::Format::R32Sint, Rhi::SampledTextureClass::Sint },
		Case{ Rhi::Format::RGBA32Sint, Rhi::SampledTextureClass::Sint }
	};
	for (const auto& item : cases)
	{
		const auto format = item.Format;
		SWIM_CHECK_EQUAL(Rhi::GetSampledTextureClass(format), item.Class);
		Rhi::TextureDesc desc{};
		desc.PixelFormat = format;
		desc.Usage = Rhi::TextureUsage::Sampled;
		RhiVulkan::VulkanTexture texture(capture.State, VK_NULL_HANDLE, desc);
		Rhi::TextureViewDesc viewDesc{};
		viewDesc.PixelFormat = format;
		RhiVulkan::VulkanTextureView view(capture.State, texture, RhiVulkan::FromNativeHandle<VkImageView>(1), viewDesc);
		Rhi::DescriptorWrite write{};
		write.TextureResource = &view;
		binding.SampledClass = item.Class;
		capture.FormatFeatures = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
		const auto image = RhiVulkan::BuildVulkanImageDescriptor(capture.State, binding, write);
		SWIM_CHECK_EQUAL(image.imageLayout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		SWIM_CHECK(image.sampler == VK_NULL_HANDLE);
		binding.SampledClass = binding.SampledClass == Rhi::SampledTextureClass::Uint ? Rhi::SampledTextureClass::Sint : Rhi::SampledTextureClass::Uint;
		SWIM_CHECK_THROWS(RhiVulkan::BuildVulkanImageDescriptor(capture.State, binding, write), std::invalid_argument);
		binding.SampledClass = Rhi::SampledTextureClass::Float;
		SWIM_CHECK_THROWS(RhiVulkan::BuildVulkanImageDescriptor(capture.State, binding, write), std::invalid_argument);
		binding.SampledClass = item.Class;
		capture.FormatFeatures = 0;
		SWIM_CHECK_THROWS(RhiVulkan::BuildVulkanImageDescriptor(capture.State, binding, write), std::invalid_argument);
	}
	for (const auto format : { Rhi::Format::RGBA8Unorm, Rhi::Format::RGBA8Snorm, Rhi::Format::RGBA8UnormSrgb,
		Rhi::Format::R16Float, Rhi::Format::R32Float, Rhi::Format::BC6HSfloat, Rhi::Format::BC7Unorm })
	{
		SWIM_CHECK_EQUAL(Rhi::GetSampledTextureClass(format), Rhi::SampledTextureClass::Float);
	}
	for (const auto format : { Rhi::Format::Undefined, Rhi::Format::D16Unorm, Rhi::Format::D24UnormS8Uint,
		Rhi::Format::D32Float, Rhi::Format::D32FloatS8Uint, static_cast<Rhi::Format>(UINT16_MAX) })
	{
		SWIM_CHECK_EQUAL(Rhi::GetSampledTextureClass(format), Rhi::SampledTextureClass::Undefined);
	}
}

SWIM_TEST("RHI.Vulkan.SampledTextures", "TypedArrayBatchRejectsMismatchWithoutPublishingAndSeals")
{
	Testing::VulkanComputeCapture capture;
	capture.FormatFeatures = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
	Rhi::DescriptorSchemaDesc schema{ 0, { { 2, Rhi::DescriptorType::SampledTexture, 2,
		Rhi::ShaderStageMask::Compute, false, false, Rhi::Format::Undefined, Rhi::SampledTextureClass::Uint } } };
	auto program = capture.MakeComputeProgram({ { &schema, 1 } });
	SWIM_REQUIRE(program);
	// Caller-owned reflection can go away or change without changing the program/layout.
	schema.Bindings[0].SampledClass = Rhi::SampledTextureClass::Sint;
	auto layout = RhiVulkan::VulkanPipelineLayout::Create(capture.State, { program.get(), {} });
	SWIM_REQUIRE(layout);
	auto pipeline = RhiVulkan::VulkanComputePipeline::Create(capture.State, { program.get(), layout.get(), {}, {} });
	auto table = RhiVulkan::VulkanDescriptorTable::Create(capture.State, { layout.get(), 0, 0, {} });
	SWIM_REQUIRE(pipeline && table);
	Rhi::TextureDesc desc{};
	desc.PixelFormat = Rhi::Format::R32Uint;
	desc.Usage = Rhi::TextureUsage::Sampled;
	RhiVulkan::VulkanTexture texture(capture.State, VK_NULL_HANDLE, desc);
	Rhi::TextureViewDesc viewDesc{};
	viewDesc.PixelFormat = desc.PixelFormat;
	RhiVulkan::VulkanTextureView view(capture.State, texture, RhiVulkan::FromNativeHandle<VkImageView>(1), viewDesc);
	desc.PixelFormat = viewDesc.PixelFormat = Rhi::Format::R32Sint;
	RhiVulkan::VulkanTexture signedTexture(capture.State, VK_NULL_HANDLE, desc);
	RhiVulkan::VulkanTextureView signedView(capture.State, signedTexture, RhiVulkan::FromNativeHandle<VkImageView>(2), viewDesc);
	std::array<Rhi::DescriptorWrite, 2> writes{};
	writes[0].Binding = writes[1].Binding = 2;
	writes[0].ArrayIndex = 1;
	writes[0].TextureResource = &view;
	writes[1].TextureResource = &signedView;
	SWIM_CHECK_THROWS(table->Write(writes), std::invalid_argument);
	SWIM_CHECK_EQUAL(capture.Updates, 0u);
	SWIM_CHECK(!table->IsComplete());
	writes[1].TextureResource = &view;
	table->Write(writes);
	SWIM_CHECK(table->IsComplete());
	SWIM_CHECK_EQUAL(capture.PoolSizes[0].type, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
	SWIM_CHECK_EQUAL(capture.PoolSizes[0].descriptorCount, 2u);
	SWIM_CHECK_EQUAL(capture.Writes[0].dstArrayElement, 1u);
	capture.Commands->Begin();
	capture.Commands->BindComputePipeline(*pipeline);
	capture.Commands->BindDescriptorTable(0, *table);
	capture.Commands->Dispatch(1, 1, 1);
	SWIM_CHECK_THROWS(table->Write(writes), std::logic_error);
}

SWIM_TEST("RHI.Vulkan.SampledTextures", "InvalidClassContractsRejectBeforeNativeLayouts")
{
	Testing::VulkanDescriptorCapture capture;
	for (const auto type : { Rhi::DescriptorType::SampledTexture, Rhi::DescriptorType::UniformBuffer, Rhi::DescriptorType::Sampler })
	{
		for (const auto numeric : { Rhi::SampledTextureClass::Undefined, static_cast<Rhi::SampledTextureClass>(255), Rhi::SampledTextureClass::Uint })
		{
			if (type == Rhi::DescriptorType::SampledTexture && numeric == Rhi::SampledTextureClass::Uint)
			{
				continue;
			}
			Rhi::DescriptorSchemaDesc schema{ 0, { { 0, type, 1, Rhi::ShaderStageMask::Fragment,
				false, false, Rhi::Format::Undefined, numeric } } };
			auto program = capture.MakeProgram({ { &schema, 1 } });
			SWIM_REQUIRE(program);
			SWIM_CHECK(!RhiVulkan::VulkanPipelineLayout::Create(capture.State, { program.get(), {} }));
			SWIM_CHECK(capture.SetBindings.empty());
		}
	}
}

SWIM_TEST("RHI.Vulkan.SampledTextures", "ViewsSelectValidMipAndLayerRanges")
{
	Testing::VulkanDescriptorCapture capture;
	capture.FormatFeatures = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
	Rhi::DescriptorBindingDesc binding{};
	binding.SampledClass = Rhi::SampledTextureClass::Sint;
	Rhi::TextureDesc desc{};
	desc.PixelFormat = Rhi::Format::R32Sint;
	desc.Extent = { 8, 8, 1 };
	desc.MipLevels = 4;
	desc.ArrayLayers = 2;
	desc.Usage = Rhi::TextureUsage::Sampled;
	RhiVulkan::VulkanTexture texture(capture.State, VK_NULL_HANDLE, desc);
	for (std::uint32_t invalid = 0; invalid < 7; ++invalid)
	{
		Rhi::TextureViewDesc viewDesc{};
		viewDesc.PixelFormat = desc.PixelFormat;
		viewDesc.BaseMipLevel = 1;
		viewDesc.MipLevelCount = 3;
		viewDesc.BaseArrayLayer = 1;
		if (invalid == 1)
		{
			viewDesc.MipLevelCount = 0;
		}
		if (invalid == 2)
		{
			viewDesc.MipLevelCount = UINT32_MAX;
		}
		if (invalid == 3)
		{
			viewDesc.BaseMipLevel = UINT32_MAX;
		}
		if (invalid == 4)
		{
			viewDesc.BaseArrayLayer = 2;
		}
		if (invalid == 5)
		{
			viewDesc.ArrayLayerCount = 2;
		}
		if (invalid == 6)
		{
			viewDesc.Dimension = Rhi::TextureViewDimension::Texture2DArray;
		}
		RhiVulkan::VulkanTextureView view(capture.State, texture, RhiVulkan::FromNativeHandle<VkImageView>(1), viewDesc);
		Rhi::DescriptorWrite write{};
		write.TextureResource = &view;
		if (invalid == 0)
		{
			SWIM_CHECK_EQUAL(RhiVulkan::BuildVulkanImageDescriptor(capture.State, binding, write).imageLayout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		}
		else
		{
			SWIM_CHECK_THROWS(RhiVulkan::BuildVulkanImageDescriptor(capture.State, binding, write), std::invalid_argument);
		}
	}
}

#ifdef SWIM_RHI_SAMPLED_INTEGER_REFLECTION_PATH
SWIM_TEST("RHI.Vulkan.SampledTextures", "CompiledInterfaceRetainsNumericClassesInNativeLayout")
{
	const auto parsed = ShaderCompiler::LoadSlangReflectionJson(SWIM_RHI_SAMPLED_INTEGER_REFLECTION_PATH);
	SWIM_REQUIRE(parsed);
	const auto converted = ShaderCompiler::BuildRhiShaderInterface(parsed.Reflection);
	SWIM_REQUIRE(converted);
	Testing::VulkanComputeCapture capture;
	const auto& interface = converted.Interface;
	auto program = capture.MakeComputeProgram({ interface.DescriptorSchemas, interface.PushConstants, interface.ComputeThreadGroupSize }, interface.ComputeThreadGroupSize);
	SWIM_REQUIRE(program);
	auto layout = RhiVulkan::VulkanPipelineLayout::Create(capture.State, { program.get(), {} });
	SWIM_REQUIRE(layout);
	const auto state = layout->GetLayoutState();
	SWIM_CHECK_EQUAL(state->Interface.DescriptorSchemas[0].Bindings[0].SampledClass, Rhi::SampledTextureClass::Uint);
	SWIM_CHECK_EQUAL(state->Interface.DescriptorSchemas[1].Bindings[1].SampledClass, Rhi::SampledTextureClass::Sint);
	SWIM_CHECK_EQUAL(capture.SetBindings[0][0].descriptorType, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
	SWIM_CHECK_EQUAL(capture.SetBindings[0][0].descriptorCount, 2u);
}
#endif
