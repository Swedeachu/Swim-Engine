#include "Tests/Fixtures/VulkanStorageTextureCapture.h"
#include "Tests/Framework/Test.h"

using namespace Swim;

namespace
{
	Rhi::TextureDesc TextureDesc()
	{
		Rhi::TextureDesc desc;
		desc.Extent = { 32, 16, 1 };
		desc.PixelFormat = Rhi::Format::RGBA32Float;
		desc.Usage = Rhi::TextureUsage::Storage | Rhi::TextureUsage::Sampled;
		desc.MipLevels = 3;
		desc.ArrayLayers = 2;
		return desc;
	}

	Rhi::TextureViewDesc ViewDesc()
	{
		return { Rhi::TextureViewDimension::Texture2D, Rhi::Format::RGBA32Float, 1, 1, 1, 1, {} };
	}
}

SWIM_TEST("RHI.Vulkan.StorageTexture", "NativeLayoutPoolFormatOwnershipAndDescriptorLimits")
{
	Testing::VulkanStorageTextureCapture capture;
	capture.CreateTable();
	SWIM_REQUIRE(capture.Table);
	SWIM_CHECK_EQUAL(capture.SetBindings[1][0].descriptorType, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
	SWIM_CHECK_EQUAL(capture.SetBindings[1][0].stageFlags, static_cast<VkShaderStageFlags>(VK_SHADER_STAGE_COMPUTE_BIT));
	SWIM_CHECK_EQUAL(capture.PoolSizes[0].type, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
	capture.Schema.Bindings[0].StorageTextureFormat = Rhi::Format::R32Uint;
	SWIM_CHECK_EQUAL(capture.Program->GetInterface().DescriptorSchemas[0].Bindings[0].StorageTextureFormat, Rhi::Format::RGBA32Float);
	const auto create = [&]
	{
		auto program = capture.MakeComputeProgram({ { &capture.Schema, 1 }, {} });
		return RhiVulkan::VulkanPipelineLayout::Create(capture.State, { program.get(), {} }) != nullptr;
	};
	capture.Schema.Bindings[0].Count = 32;
	SWIM_CHECK(create());
	capture.Schema.Bindings[0].Count = 33;
	SWIM_CHECK(!create());
	capture.Schema.Bindings[0].Count = 2;
	capture.State->Device.physical_device.properties.limits.maxDescriptorSetStorageImages = 1;
	SWIM_CHECK(!create());
	capture.State->Device.physical_device.properties.limits.maxDescriptorSetStorageImages = 64;
	capture.State->Device.physical_device.properties.limits.maxPerStageResources = 2;
	SWIM_CHECK(create());
	capture.Schema.Bindings.push_back({ 3, Rhi::DescriptorType::ReadOnlyStorageBuffer, 1, Rhi::ShaderStageMask::Compute });
	SWIM_CHECK(!create());
}

SWIM_TEST("RHI.Vulkan.StorageTexture", "LayoutRejectsMissingUnsupportedFormatsAndGraphicsWrites")
{
	Testing::VulkanStorageTextureCapture capture;
	for (auto format : { Rhi::Format::Undefined, Rhi::Format::BGRA8Unorm, Rhi::Format::RGBA8UnormSrgb,
		Rhi::Format::D32Float, Rhi::Format::BC7Unorm, Rhi::Format::RG16Float })
	{
		capture.Schema.Bindings[0].StorageTextureFormat = format;
		auto program = capture.MakeComputeProgram({ { &capture.Schema, 1 }, {} });
		SWIM_CHECK(!RhiVulkan::VulkanPipelineLayout::Create(capture.State, { program.get(), {} }));
	}
	capture.Schema.Bindings[0].StorageTextureFormat = Rhi::Format::RGBA32Float;
	capture.Schema.Bindings[0].Stages = Rhi::ShaderStageMask::Fragment;
	auto graphics = capture.MakeProgram({ { &capture.Schema, 1 }, {} });
	SWIM_CHECK(!RhiVulkan::VulkanPipelineLayout::Create(capture.State, { graphics.get(), {} }));
	capture.Schema.Bindings[0].Stages = Rhi::ShaderStageMask::Compute;
	capture.Schema.Bindings[0].Type = Rhi::DescriptorType::SampledTexture;
	auto program = capture.MakeComputeProgram({ { &capture.Schema, 1 }, {} });
	SWIM_CHECK(!RhiVulkan::VulkanPipelineLayout::Create(capture.State, { program.get(), {} }));
}

SWIM_TEST("RHI.Vulkan.StorageTexture", "WritesUseGeneralLayoutWithoutSamplerOrFilteringAndSealAfterBinding")
{
	Testing::VulkanStorageTextureCapture capture;
	capture.CreateTable();
	SWIM_REQUIRE(capture.Table);
	RhiVulkan::VulkanTexture texture(capture.State, RhiVulkan::FromNativeHandle<VkImage>(1), TextureDesc());
	RhiVulkan::VulkanTextureView view(capture.State, texture, RhiVulkan::FromNativeHandle<VkImageView>(2), ViewDesc());
	Rhi::DescriptorWrite write{};
	write.Binding = 7;
	write.TextureResource = &view;
	capture.FormatFeatures = VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
	capture.Table->Write({ &write, 1 });
	SWIM_REQUIRE(capture.Table->IsComplete());
	SWIM_REQUIRE_EQUAL(capture.ImagesWritten.size(), 1u);
	SWIM_CHECK_EQUAL(capture.ImagesWritten[0].imageLayout, VK_IMAGE_LAYOUT_GENERAL);
	SWIM_CHECK_EQUAL(capture.ImagesWritten[0].imageView, RhiVulkan::FromNativeHandle<VkImageView>(2));
	SWIM_CHECK(capture.ImagesWritten[0].sampler == VK_NULL_HANDLE);
	SWIM_CHECK_EQUAL(capture.Writes[0].descriptorType, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
	auto pipeline = RhiVulkan::VulkanComputePipeline::Create(capture.State, { capture.Program.get(), capture.Layout.get(), {}, {} });
	SWIM_REQUIRE(pipeline);
	capture.Commands->Begin();
	capture.Commands->BindComputePipeline(*pipeline);
	capture.Commands->BindDescriptorTable(1, *capture.Table);
	capture.Commands->Dispatch(1, 1, 1);
	SWIM_CHECK_EQUAL(capture.Dispatches.size(), 1u);
	SWIM_CHECK_THROWS(capture.Table->Write({ &write, 1 }), std::logic_error);
}

SWIM_TEST("RHI.Vulkan.StorageTexture", "BadViewsFormatsUsagesAndFeaturesNeverPublishWrites")
{
	Testing::VulkanStorageTextureCapture capture;
	capture.CreateTable();
	SWIM_REQUIRE(capture.Table);
	for (std::uint32_t invalid = 0; invalid < 11; ++invalid)
	{
		auto desc = TextureDesc();
		auto viewDesc = ViewDesc();
		if (invalid == 0)
		{
			desc.Usage = Rhi::TextureUsage::Sampled;
		}
		if (invalid == 1)
		{
			desc.Samples = Rhi::SampleCount::X2;
		}
		if (invalid == 2)
		{
			viewDesc.Dimension = Rhi::TextureViewDimension::Texture2DArray;
		}
		if (invalid == 3)
		{
			viewDesc.MipLevelCount = 2;
		}
		if (invalid == 4)
		{
			viewDesc.ArrayLayerCount = 2;
		}
		if (invalid == 5)
		{
			viewDesc.PixelFormat = Rhi::Format::RGBA32Uint;
		}
		if (invalid == 6)
		{
			viewDesc.BaseMipLevel = desc.MipLevels;
		}
		if (invalid == 7)
		{
			viewDesc.BaseArrayLayer = desc.ArrayLayers;
		}
		if (invalid == 8)
		{
			desc.PixelFormat = Rhi::Format::R32Float;
		}
		if (invalid == 9)
		{
			desc.Dimension = Rhi::TextureDimension::Texture3D;
		}
		RhiVulkan::VulkanTexture texture(capture.State, VK_NULL_HANDLE, desc);
		RhiVulkan::VulkanTextureView view(capture.State, texture, RhiVulkan::FromNativeHandle<VkImageView>(invalid == 10 ? 0 : 2), viewDesc);
		Rhi::DescriptorWrite write{};
		write.Binding = 7;
		write.TextureResource = &view;
		SWIM_CHECK_THROWS(capture.Table->Write({ &write, 1 }), std::invalid_argument);
	}
	RhiVulkan::VulkanTexture texture(capture.State, VK_NULL_HANDLE, TextureDesc());
	RhiVulkan::VulkanTextureView view(capture.State, texture, RhiVulkan::FromNativeHandle<VkImageView>(2), ViewDesc());
	Rhi::DescriptorWrite write{};
	write.Binding = 7;
	write.TextureResource = &view;
	capture.FormatFeatures = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
	SWIM_CHECK_THROWS(capture.Table->Write({ &write, 1 }), std::invalid_argument);
	SWIM_CHECK(!capture.Table->IsComplete());
	SWIM_CHECK_EQUAL(capture.Updates, 0u);
}

SWIM_TEST("RHI.Vulkan.StorageTexture", "MixedBatchForeignResourceAndDeviceLossAreAtomic")
{
	Testing::VulkanStorageTextureCapture capture;
	capture.CreateTable();
	SWIM_REQUIRE(capture.Table);
	RhiVulkan::VulkanTexture texture(capture.State, VK_NULL_HANDLE, TextureDesc());
	RhiVulkan::VulkanTextureView view(capture.State, texture, RhiVulkan::FromNativeHandle<VkImageView>(2), ViewDesc());
	std::array<Rhi::DescriptorWrite, 2> writes{};
	for (auto& write : writes)
	{
		write.Binding = 7;
		write.TextureResource = &view;
	}
	writes[1].BufferOffset = 4;
	SWIM_CHECK_THROWS(capture.Table->Write(writes), std::invalid_argument);
	SWIM_CHECK_EQUAL(capture.Updates, 0u);
	SWIM_CHECK(!capture.Table->IsComplete());
	auto foreign = std::make_shared<RhiVulkan::VulkanDeviceState>();
	RhiVulkan::VulkanTextureView foreignView(foreign, texture, RhiVulkan::FromNativeHandle<VkImageView>(3), ViewDesc());
	writes[0].TextureResource = &foreignView;
	SWIM_CHECK_THROWS(capture.Table->Write(std::span(writes).first(1)), std::invalid_argument);
	writes[0].TextureResource = &view;
	capture.State->Diagnostics->TryRecordLoss("storage descriptors test", VK_ERROR_DEVICE_LOST);
	SWIM_CHECK_THROWS(capture.Table->Write(std::span(writes).first(1)), Rhi::DeviceLostError);
	SWIM_CHECK_EQUAL(capture.Updates, 0u);
}
