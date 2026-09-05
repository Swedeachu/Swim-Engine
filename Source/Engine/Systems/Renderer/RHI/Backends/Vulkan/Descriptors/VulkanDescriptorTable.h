#pragma once

#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Pipelines/VulkanPipelineLayout.h"

#include <atomic>

namespace Swim::RhiVulkan
{

	class VulkanDescriptorTable final : public Rhi::DescriptorTable
	{
	public:
		VulkanDescriptorTable(VulkanPipelineLayout& layout, std::uint32_t space);
		~VulkanDescriptorTable() override;
		static std::unique_ptr<VulkanDescriptorTable> Create(std::shared_ptr<VulkanDeviceState> state, const Rhi::DescriptorTableDesc& desc);
		std::uintptr_t GetNativeHandle() const override;
		Rhi::PipelineLayout& GetLayout() const override;
		std::uint32_t GetSpace() const override;
		void Write(std::span<const Rhi::DescriptorWrite> writes) override;
		const std::shared_ptr<VulkanDeviceState>& GetState() const;
		const std::shared_ptr<VulkanPipelineLayoutState>& GetLayoutState() const;
		bool IsComplete() const;
		void Seal();

	private:
		VulkanPipelineLayout& layout;
		std::shared_ptr<VulkanPipelineLayoutState> layoutState;
		std::uint32_t space;
		VkDescriptorPool pool = VK_NULL_HANDLE;
		VkDescriptorSet set = VK_NULL_HANDLE;
		std::vector<std::vector<bool>> initialized;
		std::atomic<bool> sealed{ false };
	};

} // namespace Swim::RhiVulkan
