#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Descriptors/VulkanDescriptorTable.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanFormatUtils.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanResourceAccess.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanTransferUtils.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Resources/VulkanBuffer.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Resources/VulkanSampler.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Resources/VulkanTextureView.h"

#include <algorithm>
#include <limits>

namespace Swim::RhiVulkan
{

	void VulkanDescriptorTable::Write(std::span<const Rhi::DescriptorWrite> writes)
	{
		if (sealed.load())
		{
			throw std::logic_error("Recorded descriptor tables are immutable; allocate a replacement and retire the old table after GPU completion");
		}
		if (writes.size() > std::numeric_limits<std::uint32_t>::max())
		{
			throw std::invalid_argument("Too many descriptor writes");
		}
		const auto& bindings = FindDescriptorSchema(*layoutState, space)->Bindings;
		const auto& limits = GetState()->Device.physical_device.properties.limits;
		std::vector<VkWriteDescriptorSet> native(writes.size());
		std::vector<VkDescriptorImageInfo> images(writes.size());
		std::vector<VkDescriptorBufferInfo> buffers(writes.size());
		std::vector<std::size_t> bindingIndices;
		bindingIndices.reserve(writes.size());
		for (std::size_t index = 0; index < writes.size(); ++index)
		{
			const auto& write = writes[index];
			const auto binding = std::lower_bound(bindings.begin(), bindings.end(), write.Binding,
				[](const auto& candidate, std::uint32_t number) { return candidate.Binding < number; });
			if (binding == bindings.end() || binding->Binding != write.Binding || write.ArrayIndex >= binding->Count)
			{
				throw std::invalid_argument("Descriptor write is outside its reflected binding/array bounds");
			}
			bindingIndices.push_back(static_cast<std::size_t>(binding - bindings.begin()));
			auto& target = native[index];
			target.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			target.dstSet = set;
			target.dstBinding = write.Binding;
			target.dstArrayElement = write.ArrayIndex;
			target.descriptorCount = 1;
			target.descriptorType = ToVkDescriptorType(binding->Type);
			if (binding->Type == Rhi::DescriptorType::Sampler)
			{
				if (!write.SamplerResource || write.TextureResource || write.BufferResource || write.BufferOffset || write.BufferRange)
				{
					throw std::invalid_argument("Sampler descriptor requires exactly one sampler resource");
				}
				const auto& sampler = RequireResource<VulkanSampler>(*write.SamplerResource, GetState());
				if (sampler.GetNativeHandle() == 0)
				{
					throw std::invalid_argument("Sampler descriptor cannot be null");
				}
				images[index].sampler = FromNativeHandle<VkSampler>(sampler.GetNativeHandle());
				target.pImageInfo = &images[index];
			}
			else if (binding->Type == Rhi::DescriptorType::SampledTexture)
			{
				if (!write.TextureResource || write.SamplerResource || write.BufferResource || write.BufferOffset || write.BufferRange)
				{
					throw std::invalid_argument("Sampled texture descriptor requires exactly one texture view");
				}
				const auto& view = RequireResource<VulkanTextureView>(*write.TextureResource, GetState());
				const auto& desc = view.GetTexture().GetDesc();
				const auto format = view.GetDesc().PixelFormat;
				if (view.GetNativeHandle() == 0 || !HasTextureUsage(desc.Usage, Rhi::TextureUsage::Sampled) ||
					desc.Samples != Rhi::SampleCount::X1 || view.GetDesc().Dimension != Rhi::TextureViewDimension::Texture2D ||
					Rhi::IsDepthFormat(format) || IsIntegerColorFormat(format))
				{
					throw std::invalid_argument("Sampled descriptors currently require single-sampled floating/normalized 2D color views");
				}
				VkFormatProperties properties{};
				GetState()->Instance->Dispatch.vkGetPhysicalDeviceFormatProperties(GetState()->Device.physical_device.physical_device,
					ToVkFormat(format), &properties);
				constexpr auto required = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
				if ((properties.optimalTilingFeatures & required) != required)
				{
					throw std::invalid_argument("Sampled color format must support both nearest and linear filtering");
				}
				images[index].imageView = FromNativeHandle<VkImageView>(view.GetNativeHandle());
				images[index].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				target.pImageInfo = &images[index];
			}
			else
			{
				if (!write.BufferResource || write.TextureResource || write.SamplerResource)
				{
					throw std::invalid_argument("Buffer descriptor requires exactly one buffer resource");
				}
				const auto& buffer = RequireResource<VulkanBuffer>(*write.BufferResource, GetState());
				const bool uniform = binding->Type == Rhi::DescriptorType::UniformBuffer;
				const auto alignment = uniform ? limits.minUniformBufferOffsetAlignment : limits.minStorageBufferOffsetAlignment;
				const auto maxRange = uniform ? limits.maxUniformBufferRange : limits.maxStorageBufferRange;
				if (buffer.GetNativeHandle() == 0 || !HasBufferUsage(buffer.GetDesc().Usage, uniform ? Rhi::BufferUsage::Uniform : Rhi::BufferUsage::Storage) ||
					write.BufferOffset >= buffer.GetDesc().Size || (alignment != 0 && write.BufferOffset % alignment != 0))
				{
					throw std::invalid_argument("Buffer descriptor usage, offset or alignment is invalid");
				}
				const auto remaining = buffer.GetDesc().Size - write.BufferOffset;
				const auto range = write.BufferRange == 0 ? remaining : write.BufferRange;
				if (range == 0 || range > remaining || range > maxRange)
				{
					throw std::invalid_argument("Buffer descriptor range exceeds resource or device bounds");
				}
				buffers[index] = { FromNativeHandle<VkBuffer>(buffer.GetNativeHandle()), write.BufferOffset, range };
				target.pBufferInfo = &buffers[index];
			}
		}
		// Validate the entire batch before writing any native descriptor or publishing initialization.
		if (!native.empty())
		{
			GetState()->Dispatch.vkUpdateDescriptorSets(GetState()->Device.device, static_cast<std::uint32_t>(native.size()), native.data(), 0, nullptr);
		}
		for (std::size_t index = 0; index < writes.size(); ++index)
		{
			initialized[bindingIndices[index]][writes[index].ArrayIndex] = true;
		}
	}

} // namespace Swim::RhiVulkan
