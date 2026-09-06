#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Resources/VulkanSampler.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanNativeHandle.h"

#include <cmath>
#include <stdexcept>

namespace Swim::RhiVulkan
{

	namespace
	{
		VkSamplerAddressMode AddressMode(Rhi::SamplerAddressMode mode)
		{
			switch (mode)
			{
			case Rhi::SamplerAddressMode::Repeat: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
			case Rhi::SamplerAddressMode::MirroredRepeat: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
			case Rhi::SamplerAddressMode::ClampToEdge: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			case Rhi::SamplerAddressMode::ClampToBorder: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
			default: throw std::invalid_argument("Unknown sampler address mode");
			}
		}
	}

	VulkanSampler::VulkanSampler(std::shared_ptr<VulkanDeviceState> state, const Rhi::SamplerDesc& desc)
		: state(std::move(state)), debugName(desc.DebugName), desc(desc)
	{
		this->desc.DebugName = debugName;
	}

	VulkanSampler::~VulkanSampler()
	{
		RetireLostVulkanDevice(*state);
		if (sampler != VK_NULL_HANDLE)
		{
			state->Dispatch.vkDestroySampler(state->Device.device, sampler, nullptr);
		}
		if (reserved)
		{
			state->SamplerCount.fetch_sub(1);
		}
	}

	std::unique_ptr<VulkanSampler> VulkanSampler::Create(std::shared_ptr<VulkanDeviceState> state, const Rhi::SamplerDesc& desc)
	{
		if (state)
		{
			RequireVulkanDevice(*state);
		}
		const auto& limits = state->Device.physical_device.properties.limits;
		const auto validFilter = [](Rhi::Filter filter) { return filter == Rhi::Filter::Nearest || filter == Rhi::Filter::Linear; };
		if (!validFilter(desc.MinFilter) || !validFilter(desc.MagFilter) || !validFilter(desc.MipFilter) ||
			desc.EnableAnisotropy || desc.EnableComparison || !std::isfinite(desc.MinLod) || !std::isfinite(desc.MaxLod) ||
			!std::isfinite(desc.MipLodBias) || desc.MinLod < 0 || desc.MaxLod < desc.MinLod || std::abs(desc.MipLodBias) > limits.maxSamplerLodBias)
		{
			return nullptr;
		}
		VkSamplerCreateInfo info{};
		info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		info.minFilter = desc.MinFilter == Rhi::Filter::Linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
		info.magFilter = desc.MagFilter == Rhi::Filter::Linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
		info.mipmapMode = desc.MipFilter == Rhi::Filter::Linear ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
		try
		{
			info.addressModeU = AddressMode(desc.AddressU);
			info.addressModeV = AddressMode(desc.AddressV);
			info.addressModeW = AddressMode(desc.AddressW);
		}
		catch (const std::invalid_argument&)
		{
			return nullptr;
		}
		info.minLod = desc.MinLod;
		info.maxLod = desc.MaxLod;
		info.mipLodBias = desc.MipLodBias;
		info.maxAnisotropy = 1.0f;
		info.compareOp = VK_COMPARE_OP_ALWAYS;
		info.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
		auto result = std::make_unique<VulkanSampler>(state, desc);
		auto count = state->SamplerCount.load();
		do
		{
			if (count >= limits.maxSamplerAllocationCount)
			{
				return nullptr;
			}
		} while (!state->SamplerCount.compare_exchange_weak(count, count + 1));
		result->reserved = true;
		const auto createResult = state->Dispatch.vkCreateSampler(state->Device.device, &info, nullptr, &result->sampler);
		if (createResult != VK_SUCCESS)
		{
			result->sampler = VK_NULL_HANDLE;
			CheckVulkanResult(*state, createResult, "vkCreateSampler");
			return nullptr;
		}
		SetVulkanObjectName(*state, VK_OBJECT_TYPE_SAMPLER, ToNativeHandle(result->sampler), desc.DebugName);
		return result;
	}

	std::uintptr_t VulkanSampler::GetNativeHandle() const
	{
		return ToNativeHandle(sampler);
	}

	const Rhi::SamplerDesc& VulkanSampler::GetDesc() const
	{
		return desc;
	}

	const std::shared_ptr<VulkanDeviceState>& VulkanSampler::GetState() const
	{
		return state;
	}

} // namespace Swim::RhiVulkan
