#include "Tests/Fixtures/VulkanStorageTextureCapture.h"
#include "Tests/Framework/Test.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanStorageTexture.h"

using namespace Swim;

SWIM_TEST("RHI.Vulkan.StorageTextureCreation", "ChecksWholeUsageCombinationAndNativeLimitsBeforeAllocation")
{
	Testing::VulkanStorageTextureCapture capture;
	Rhi::TextureDesc desc;
	desc.Extent = { 64, 32, 1 };
	desc.PixelFormat = Rhi::Format::RGBA32Float;
	desc.Usage = Rhi::TextureUsage::Storage | Rhi::TextureUsage::TransferSource | Rhi::TextureUsage::TransferDestination;
	desc.MipLevels = 4;
	desc.ArrayLayers = 2;
	SWIM_CHECK(RhiVulkan::ValidateVulkanStorageTexture(*capture.State, desc));
	SWIM_CHECK_EQUAL(capture.ImageQueries, 1u);
	SWIM_CHECK_EQUAL(capture.ImageFormat, VK_FORMAT_R32G32B32A32_SFLOAT);
	SWIM_CHECK_EQUAL(capture.ImageUsage, static_cast<VkImageUsageFlags>(VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT));
	for (std::uint32_t invalid = 0; invalid < 6; ++invalid)
	{
		auto bad = desc;
		if (invalid == 0)
		{
			bad.Extent.Width = 129;
		}
		if (invalid == 1)
		{
			bad.Extent.Height = 129;
		}
		if (invalid == 2)
		{
			bad.Extent.Depth = 2;
		}
		if (invalid == 3)
		{
			bad.MipLevels = 9;
		}
		if (invalid == 4)
		{
			bad.ArrayLayers = 9;
		}
		if (invalid == 5)
		{
			bad.MipLevels = 8; // Native max allows eight, but a 64x32 image only has seven.
		}
		SWIM_CHECK(!RhiVulkan::ValidateVulkanStorageTexture(*capture.State, bad));
	}
	capture.ImageProperties.sampleCounts = 0;
	SWIM_CHECK(!RhiVulkan::ValidateVulkanStorageTexture(*capture.State, desc));
	capture.ImageProperties.sampleCounts = VK_SAMPLE_COUNT_1_BIT;
	capture.ImageResult = VK_ERROR_FORMAT_NOT_SUPPORTED;
	SWIM_CHECK(!RhiVulkan::ValidateVulkanStorageTexture(*capture.State, desc));
}

SWIM_TEST("RHI.Vulkan.StorageTextureCreation", "FormatsMultisamplingDimensionsAndDeviceLossRejectEarly")
{
	Testing::VulkanStorageTextureCapture capture;
	Rhi::TextureDesc desc;
	desc.Extent = { 32, 32, 1 };
	desc.Usage = Rhi::TextureUsage::Storage;
	for (auto format : { Rhi::Format::Undefined, Rhi::Format::D32Float, Rhi::Format::RGBA8UnormSrgb,
		Rhi::Format::BC7Unorm, Rhi::Format::BGRA8Unorm })
	{
		desc.PixelFormat = format;
		SWIM_CHECK(!RhiVulkan::ValidateVulkanStorageTexture(*capture.State, desc));
	}
	desc.PixelFormat = Rhi::Format::RGBA32Float;
	for (auto dimension : { Rhi::TextureDimension::Texture1D, Rhi::TextureDimension::Texture3D, Rhi::TextureDimension::TextureCube })
	{
		desc.Dimension = dimension;
		SWIM_CHECK(!RhiVulkan::ValidateVulkanStorageTexture(*capture.State, desc));
	}
	desc.Dimension = Rhi::TextureDimension::Texture2D;
	desc.Samples = Rhi::SampleCount::X4;
	SWIM_CHECK(!RhiVulkan::ValidateVulkanStorageTexture(*capture.State, desc));
	SWIM_CHECK_EQUAL(capture.ImageQueries, 0u);
	desc.Samples = Rhi::SampleCount::X1;
	capture.FormatFeatures = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
	SWIM_CHECK(!RhiVulkan::ValidateVulkanStorageTexture(*capture.State, desc));
	SWIM_CHECK_EQUAL(capture.ImageQueries, 0u);
	capture.FormatFeatures = VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
	SWIM_CHECK(RhiVulkan::ValidateVulkanStorageTexture(*capture.State, desc));
	capture.State->Diagnostics->TryRecordLoss("storage creation test", VK_ERROR_DEVICE_LOST);
	SWIM_CHECK_THROWS(RhiVulkan::ValidateVulkanStorageTexture(*capture.State, desc), Rhi::DeviceLostError);
}
