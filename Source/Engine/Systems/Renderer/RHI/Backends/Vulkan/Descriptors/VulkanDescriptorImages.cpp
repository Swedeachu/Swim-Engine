#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Descriptors/VulkanDescriptorImages.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanFormatUtils.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanDepthSampling.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanResourceAccess.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanResourceState.h"
#include "Engine/Systems/Renderer/RHI/RhiSampledTexture.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanTextureViews.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Resources/VulkanTextureView.h"

namespace Swim::RhiVulkan
{

	VkDescriptorImageInfo BuildVulkanImageDescriptor(const std::shared_ptr<VulkanDeviceState>& state,
		const Rhi::DescriptorBindingDesc& binding, const Rhi::DescriptorWrite& write)
	{
		if (!write.TextureResource || write.SamplerResource || write.BufferResource || write.BufferOffset || write.BufferRange)
		{
			throw std::invalid_argument("Image descriptor requires exactly one texture view");
		}
		const auto& view = RequireResource<VulkanTextureView>(*write.TextureResource, state);
		RequireResource<VulkanTexture>(view.GetTexture(), state);
		const auto& desc = view.GetTexture().GetDesc();
		const auto& viewDesc = view.GetDesc();
		const auto format = viewDesc.PixelFormat;
		const bool depth = Rhi::IsDepthFormat(format);
		const auto aspect = GetVulkanTextureViewAspect(format, viewDesc.Aspect);
		VkFormatFeatureFlags required = 0;
		VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
		if (binding.Type == Rhi::DescriptorType::StorageTexture)
		{
			if (view.GetNativeHandle() == 0 || !HasTextureUsage(desc.Usage, Rhi::TextureUsage::Storage) ||
				desc.Samples != Rhi::SampleCount::X1 || desc.Dimension != Rhi::TextureDimension::Texture2D ||
				viewDesc.Dimension != Rhi::TextureViewDimension::Texture2D || viewDesc.MipLevelCount != 1 || viewDesc.ArrayLayerCount != 1 ||
				aspect != VK_IMAGE_ASPECT_COLOR_BIT || !Rhi::IsStorageTextureFormat(format) || format != binding.StorageTextureFormat || format != desc.PixelFormat ||
				viewDesc.BaseMipLevel >= desc.MipLevels || viewDesc.BaseArrayLayer >= desc.ArrayLayers)
			{
				throw std::invalid_argument("Storage descriptors require a matching typed, single-sampled 2D color view of one mip and layer");
			}
			required = VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
			layout = VK_IMAGE_LAYOUT_GENERAL;
		}
		else if (binding.Type == Rhi::DescriptorType::SampledTexture)
		{
			if (view.GetNativeHandle() == 0 || !HasTextureUsage(desc.Usage, Rhi::TextureUsage::Sampled) ||
				desc.Samples != Rhi::SampleCount::X1 || viewDesc.Dimension != binding.SampledDimension ||
				!ValidateVulkanTextureView(desc, viewDesc, state->Device.physical_device.features.imageCubeArray != VK_FALSE) ||
				(depth ? (aspect != VK_IMAGE_ASPECT_DEPTH_BIT || binding.SampledClass != Rhi::SampledTextureClass::Float) :
					(Rhi::GetSampledTextureClass(format) == Rhi::SampledTextureClass::Undefined || Rhi::GetSampledTextureClass(format) != binding.SampledClass)))
			{
				throw std::invalid_argument("Sampled descriptors require a single-sampled color or depth-only view matching the shader dimension and numeric class");
			}
			required = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
			// Preserve the floating/normalized filtering contract. Integer texel loads
			// do not use a sampler and must not require linear filtering support.
			if (!depth && binding.SampledClass == Rhi::SampledTextureClass::Float)
			{
				required |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
			}
			layout = GetTextureState(desc, Rhi::ResourceState::ShaderRead).Layout;
		}
		else
		{
			throw std::invalid_argument("Expected a sampled or storage texture descriptor");
		}
		if (depth)
		{
			// The depth contract supports raw reads and comparison. Do not infer
			// linear filtering from this; nearest depth sampling needs no linear bit.
			if (!SupportsVulkanDepthSampling(*state, format))
			{
				throw std::invalid_argument("Depth image format lacks sampled comparison support");
			}
			return { VK_NULL_HANDLE, FromNativeHandle<VkImageView>(view.GetNativeHandle()), layout };
		}
		VkFormatProperties properties{};
		state->Instance->Dispatch.vkGetPhysicalDeviceFormatProperties(state->Device.physical_device.physical_device,
			ToVkFormat(format), &properties);
		if ((properties.optimalTilingFeatures & required) != required)
		{
			throw std::invalid_argument("Image format lacks the descriptor's required format features");
		}
		return { VK_NULL_HANDLE, FromNativeHandle<VkImageView>(view.GetNativeHandle()), layout };
	}

} // namespace Swim::RhiVulkan
