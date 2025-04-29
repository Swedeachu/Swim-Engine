#include "PCH.h"
#include "DescriptorPool.h"

namespace Engine
{

	DescriptorPool& DescriptorPool::GetInstance()
	{
		static DescriptorPool instance;
		return instance;
	}

	std::shared_ptr<VulkanDescriptor> DescriptorPool::GetDescriptor(VulkanRenderer& vulkanRenderer, const std::shared_ptr<Texture2D>& texture)
	{
		std::lock_guard<std::mutex> lock(poolMutex);

		// Check if a descriptor already exists for this texture
		auto it = descriptors.find(texture);
		if (it != descriptors.end())
		{
			return it->second;
		}

		// Create a new VulkanDescriptor
		auto descriptor = std::make_shared<VulkanDescriptor>(vulkanRenderer, texture);
		descriptors.emplace(texture, descriptor);
		return descriptor;
	}

	void DescriptorPool::Flush()
	{
		std::lock_guard<std::mutex> lock(poolMutex);
		descriptors.clear();
	}

}
