#pragma once

#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanDeviceState.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/VulkanAdapter.h"
#include "Engine/Systems/Renderer/RHI/RhiContracts.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace Swim::RhiVulkan
{

		class VulkanGraphicsSystem final : public Rhi::GraphicsSystem
		{
		public:
			VulkanGraphicsSystem(
				std::shared_ptr<VulkanInstanceState> instance,
				std::vector<std::unique_ptr<VulkanAdapter>> adapters)
				: instance(std::move(instance)), adapters(std::move(adapters))
			{
			}

			std::uint32_t GetAdapterCount() const override
			{
				return static_cast<std::uint32_t>(adapters.size());
			}

			Rhi::Adapter& GetAdapter(std::uint32_t adapterIndex) override
			{
				return *adapters.at(adapterIndex);
			}

		private:
			std::shared_ptr<VulkanInstanceState> instance;
			std::vector<std::unique_ptr<VulkanAdapter>> adapters;
		};

} // namespace Swim::RhiVulkan
