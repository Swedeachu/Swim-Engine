#include "Tests/Fixtures/VulkanStorageTextureCapture.h"

namespace Swim::Testing
{

	namespace
	{
		VulkanStorageTextureCapture* capture = nullptr;
	}

	VulkanStorageTextureCapture::VulkanStorageTextureCapture()
	{
		capture = this;
		FormatFeatures |= VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
		auto& limits = State->Device.physical_device.properties.limits;
		limits.maxDescriptorSetStorageImages = 64;
		limits.maxPerStageDescriptorStorageImages = 32;
		ImageProperties.maxExtent = { 128, 128, 1 };
		ImageProperties.maxMipLevels = 8;
		ImageProperties.maxArrayLayers = 8;
		ImageProperties.sampleCounts = VK_SAMPLE_COUNT_1_BIT;
		State->Instance->Dispatch.vkGetPhysicalDeviceImageFormatProperties = +[](VkPhysicalDevice, VkFormat format,
			VkImageType, VkImageTiling, VkImageUsageFlags usage, VkImageCreateFlags, VkImageFormatProperties* properties) -> VkResult
		{
			++capture->ImageQueries;
			capture->ImageUsage = usage;
			capture->ImageFormat = format;
			*properties = capture->ImageProperties;
			return capture->ImageResult;
		};
	}

	void VulkanStorageTextureCapture::CreateTable()
	{
		Program = MakeComputeProgram({ { &Schema, 1 }, {} });
		Layout = RhiVulkan::VulkanPipelineLayout::Create(State, { Program.get(), {} });
		Table = RhiVulkan::VulkanDescriptorTable::Create(State, { Layout.get(), 1 });
	}

} // namespace Swim::Testing
