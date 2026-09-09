#include "Tests/Fixtures/SampledDimensionData.h"
#include "Tests/Fixtures/VulkanComputeCapture.h"
#include "Tests/Framework/Test.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Descriptors/VulkanDescriptorImages.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanTextureViews.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Resources/VulkanTextureView.h"

#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/VulkanDevice.h"
#ifdef SWIM_RHI_SAMPLED_DIMENSIONS_REFLECTION_PATH
#include "Tools/ShaderCompiler/ShaderRhiInterface.h"
#endif

using namespace Swim;

SWIM_TEST("RHI.Vulkan.SampledDimensions", "EveryShapeRequiresMatchingShaderAndValidRanges")
{
	Testing::VulkanDescriptorCapture capture;
	capture.State->Device.physical_device.features.imageCubeArray = VK_TRUE;
	for (std::uint32_t index = 0; index < 7; ++index)
	{
		const auto data = Testing::MakeSampledDimensionData(index);
		Rhi::DescriptorBindingDesc binding{};
		binding.SampledDimension = data.View.Dimension;
		binding.SampledClass = index == 4 || index == 6 ? Rhi::SampledTextureClass::Float : Rhi::SampledTextureClass::Uint;
		RhiVulkan::VulkanTexture texture(capture.State, VK_NULL_HANDLE, data.Texture);
		for (std::uint32_t invalid = 0; invalid < 6; ++invalid)
		{
			auto viewDesc = data.View;
			if (invalid == 1)
			{
				viewDesc.Dimension = index == 5 ? Rhi::TextureViewDimension::Texture2DArray : Rhi::TextureViewDimension::Texture2D;
			}
			if (invalid == 2)
			{
				viewDesc.MipLevelCount = UINT32_MAX;
			}
			if (invalid == 3)
			{
				viewDesc.BaseArrayLayer = UINT32_MAX;
			}
			if (invalid == 4)
			{
				viewDesc.ArrayLayerCount = 0;
			}
			if (invalid == 5)
			{
				viewDesc.PixelFormat = Rhi::Format::R32Sint;
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
}

SWIM_TEST("RHI.Vulkan.SampledDimensions", "CubeArraysAreOptionalAndLayoutsOwnTheDimension")
{
	Testing::VulkanComputeCapture capture;
	Rhi::DescriptorSchemaDesc schema{ 0, { { 0, Rhi::DescriptorType::SampledTexture, 2,
		Rhi::ShaderStageMask::Compute, false, false, Rhi::Format::Undefined,
		Rhi::SampledTextureClass::Float, Rhi::TextureViewDimension::TextureCubeArray } } };
	auto program = capture.MakeComputeProgram({ { &schema, 1 } });
	SWIM_REQUIRE(program);
	schema.Bindings[0].SampledDimension = Rhi::TextureViewDimension::Texture2D;
	SWIM_CHECK(!RhiVulkan::VulkanPipelineLayout::Create(capture.State, { program.get(), {} }));
	SWIM_CHECK(capture.SetBindings.empty());
	capture.State->Device.physical_device.features.imageCubeArray = VK_TRUE;
	auto layout = RhiVulkan::VulkanPipelineLayout::Create(capture.State, { program.get(), {} });
	SWIM_REQUIRE(layout);
	SWIM_CHECK_EQUAL(layout->GetLayoutState()->Interface.DescriptorSchemas[0].Bindings[0].SampledDimension, Rhi::TextureViewDimension::TextureCubeArray);
	SWIM_CHECK_EQUAL(capture.SetBindings[0][0].descriptorCount, 2u);
	SWIM_CHECK_EQUAL(capture.SetBindings[0][0].descriptorType, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
}

SWIM_TEST("RHI.Vulkan.SampledDimensions", "FactoryRulesRejectIncompatibleNativeViews")
{
	using D = Rhi::TextureViewDimension;
	for (std::uint32_t index = 0; index < 7; ++index)
	{
		auto data = Testing::MakeSampledDimensionData(index);
		SWIM_CHECK(RhiVulkan::ValidateVulkanTextureView(data.Texture, data.View, true));
		SWIM_CHECK_EQUAL(RhiVulkan::ValidateVulkanTextureView(data.Texture, data.View, false), index != 6);
		data.View.Dimension = static_cast<D>(255);
		SWIM_CHECK(!RhiVulkan::ValidateVulkanTextureView(data.Texture, data.View, true));
	}
	auto cube = Testing::MakeSampledDimensionData(6);
	cube.View.ArrayLayerCount = 7;
	SWIM_CHECK(!RhiVulkan::ValidateVulkanTextureView(cube.Texture, cube.View, true));
	cube.View.Dimension = D::TextureCube;
	SWIM_CHECK(!RhiVulkan::ValidateVulkanTextureView(cube.Texture, cube.View, true));
	cube.View.ArrayLayerCount = 6;
	SWIM_CHECK(RhiVulkan::ValidateVulkanTextureView(cube.Texture, cube.View, false));
	cube.View.Dimension = D::Texture2DArray;
	SWIM_CHECK(RhiVulkan::ValidateVulkanTextureView(cube.Texture, cube.View, false));
	cube.View.Dimension = D::Texture2D;
	cube.View.ArrayLayerCount = 1;
	SWIM_CHECK(RhiVulkan::ValidateVulkanTextureView(cube.Texture, cube.View, false));
	auto volume = Testing::MakeSampledDimensionData(3);
	volume.View.Dimension = D::Texture2D;
	SWIM_CHECK(!RhiVulkan::ValidateVulkanTextureView(volume.Texture, volume.View, true));
	volume.View.Dimension = D::Texture3D;
	volume.View.BaseArrayLayer = 1;
	SWIM_CHECK(!RhiVulkan::ValidateVulkanTextureView(volume.Texture, volume.View, true));
}

SWIM_TEST("RHI.Vulkan.SampledDimensions", "InvalidDimensionsRejectBeforeNativeLayoutCreation")
{
	Testing::VulkanComputeCapture capture;
	for (const auto type : { Rhi::DescriptorType::SampledTexture, Rhi::DescriptorType::Sampler, Rhi::DescriptorType::UniformBuffer })
	{
		Rhi::DescriptorSchemaDesc schema{ 0, { { 0, type, 1, Rhi::ShaderStageMask::Compute } } };
		schema.Bindings[0].SampledDimension = type == Rhi::DescriptorType::SampledTexture ?
			static_cast<Rhi::TextureViewDimension>(255) : Rhi::TextureViewDimension::Texture3D;
		auto program = capture.MakeComputeProgram({ { &schema, 1 } });
		SWIM_REQUIRE(program);
		SWIM_CHECK(!RhiVulkan::VulkanPipelineLayout::Create(capture.State, { program.get(), {} }));
		SWIM_CHECK(capture.SetBindings.empty());
	}
}

SWIM_TEST("RHI.Vulkan.SampledDimensions", "LayerArrayAndDescriptorArrayDoNotAliasAndBatchIsAtomic")
{
	Testing::VulkanComputeCapture capture;
	Rhi::DescriptorSchemaDesc schema{ 0, { { 0, Rhi::DescriptorType::SampledTexture, 2,
		Rhi::ShaderStageMask::Compute, false, false, Rhi::Format::Undefined,
		Rhi::SampledTextureClass::Uint, Rhi::TextureViewDimension::Texture2DArray } } };
	auto program = capture.MakeComputeProgram({ { &schema, 1 } });
	auto layout = RhiVulkan::VulkanPipelineLayout::Create(capture.State, { program.get(), {} });
	SWIM_REQUIRE(layout);
	auto table = RhiVulkan::VulkanDescriptorTable::Create(capture.State, { layout.get(), 0, 0, {} });
	SWIM_REQUIRE(table);
	auto data = Testing::MakeSampledDimensionData(2);
	data.View.ArrayLayerCount = 1; // A one-layer array view still has an array shader type.
	RhiVulkan::VulkanTexture texture(capture.State, VK_NULL_HANDLE, data.Texture);
	RhiVulkan::VulkanTextureView arrayView(capture.State, texture, RhiVulkan::FromNativeHandle<VkImageView>(1), data.View);
	data.View.Dimension = Rhi::TextureViewDimension::Texture2D;
	RhiVulkan::VulkanTextureView plainView(capture.State, texture, RhiVulkan::FromNativeHandle<VkImageView>(2), data.View);
	std::array<Rhi::DescriptorWrite, 2> writes{};
	writes[0].TextureResource = &arrayView;
	writes[1].ArrayIndex = 1;
	writes[1].TextureResource = &plainView;
	SWIM_CHECK_THROWS(table->Write(writes), std::invalid_argument);
	SWIM_CHECK_EQUAL(capture.Updates, 0u);
	SWIM_CHECK(!table->IsComplete());
	writes[1].TextureResource = &arrayView;
	table->Write(writes);
	SWIM_CHECK(table->IsComplete());
	SWIM_CHECK_EQUAL(capture.Updates, 1u);
}

SWIM_TEST("RHI.Vulkan.SampledDimensions", "DeviceFactoryEmitsNativeShapesAndResolvedSubresources")
{
	Testing::VulkanDescriptorCapture capture;
	static VkImageViewCreateInfo native{};
	static std::uint32_t creates = 0;
	static std::uint32_t destroys = 0;
	creates = destroys = 0;
	capture.State->Dispatch.vkCreateImageView = +[](VkDevice, const VkImageViewCreateInfo* info,
		const VkAllocationCallbacks*, VkImageView* result) -> VkResult
	{
		native = *info;
		*result = RhiVulkan::FromNativeHandle<VkImageView>(++creates);
		return VK_SUCCESS;
	};
	capture.State->Dispatch.vkDestroyImageView = +[](VkDevice, VkImageView, const VkAllocationCallbacks*)
	{
		++destroys;
	};
	RhiVulkan::VulkanDevice device(capture.State, {}, nullptr, nullptr, nullptr);
	const std::array nativeTypes{ VK_IMAGE_VIEW_TYPE_1D, VK_IMAGE_VIEW_TYPE_1D_ARRAY, VK_IMAGE_VIEW_TYPE_2D_ARRAY,
		VK_IMAGE_VIEW_TYPE_3D, VK_IMAGE_VIEW_TYPE_CUBE, VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_VIEW_TYPE_CUBE_ARRAY };
	for (std::uint32_t index = 0; index < 7; ++index)
	{
		const auto data = Testing::MakeSampledDimensionData(index);
		RhiVulkan::VulkanTexture texture(capture.State, RhiVulkan::FromNativeHandle<VkImage>(50), data.Texture);
		if (index == 6)
		{
			SWIM_CHECK(!device.CreateTextureView(texture, data.View));
			SWIM_CHECK_EQUAL(creates, 6u);
			capture.State->Device.physical_device.features.imageCubeArray = VK_TRUE;
		}
		auto viewDesc = data.View;
		viewDesc.PixelFormat = Rhi::Format::Undefined;
		auto view = device.CreateTextureView(texture, viewDesc);
		SWIM_REQUIRE(view);
		SWIM_CHECK_EQUAL(view->GetDesc().PixelFormat, data.Texture.PixelFormat);
		SWIM_CHECK_EQUAL(native.viewType, nativeTypes[index]);
		SWIM_CHECK_EQUAL(native.subresourceRange.baseMipLevel, 1u);
		SWIM_CHECK_EQUAL(native.subresourceRange.levelCount, 1u);
		SWIM_CHECK_EQUAL(native.subresourceRange.baseArrayLayer, data.View.BaseArrayLayer);
		SWIM_CHECK_EQUAL(native.subresourceRange.layerCount, data.View.ArrayLayerCount);
	}
	SWIM_CHECK_EQUAL(creates, 7u);
	SWIM_CHECK_EQUAL(destroys, creates);
}

#ifdef SWIM_RHI_SAMPLED_DIMENSIONS_REFLECTION_PATH
SWIM_TEST("RHI.Vulkan.SampledDimensions", "CompiledVariantsReachOwnedNativeLayouts")
{
	for (bool cubes : { false, true })
	{
		const auto parsed = ShaderCompiler::LoadSlangReflectionJson(cubes ?
			SWIM_RHI_SAMPLED_CUBE_ARRAY_REFLECTION_PATH : SWIM_RHI_SAMPLED_DIMENSIONS_REFLECTION_PATH);
		SWIM_REQUIRE(parsed);
		const auto converted = ShaderCompiler::BuildRhiShaderInterface(parsed.Reflection);
		SWIM_REQUIRE(converted);
		const auto& interface = converted.Interface;
		Testing::VulkanComputeCapture capture;
		capture.State->Device.physical_device.features.imageCubeArray = cubes;
		auto program = capture.MakeComputeProgram({ interface.DescriptorSchemas, interface.PushConstants,
			interface.ComputeThreadGroupSize }, interface.ComputeThreadGroupSize);
		SWIM_REQUIRE(program);
		auto layout = RhiVulkan::VulkanPipelineLayout::Create(capture.State, { program.get(), {} });
		SWIM_REQUIRE(layout);
		for (std::uint32_t index = 0; index < (cubes ? 7u : 6u); ++index)
		{
			SWIM_CHECK_EQUAL(layout->GetLayoutState()->Interface.DescriptorSchemas[0].Bindings[index].SampledDimension,
				Testing::MakeSampledDimensionData(index).View.Dimension);
			SWIM_CHECK_EQUAL(capture.SetBindings[0][index].descriptorType, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
		}
	}
}
#endif
