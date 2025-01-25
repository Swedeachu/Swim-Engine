#include "PCH.h"
#include "MaterialPool.h"

namespace Engine
{

	MaterialPool& MaterialPool::GetInstance()
	{
		static MaterialPool instance;
		return instance;
	}

	std::shared_ptr<MaterialDescriptor> MaterialPool::GetMaterialDescriptor(VulkanRenderer& renderer, const std::shared_ptr<Texture2D>& texture)
	{
		std::lock_guard<std::mutex> lock(poolMutex);

		// Check if a descriptor already exists for this texture
		auto it = descriptors.find(texture);
		if (it != descriptors.end())
		{
			return it->second;
		}

		// Create a new MaterialDescriptor
		auto descriptor = std::make_shared<MaterialDescriptor>(renderer, texture);
		descriptors.emplace(texture, descriptor);
		return descriptor;
	}

	void MaterialPool::Flush()
	{
		std::lock_guard<std::mutex> lock(poolMutex);
		descriptors.clear();
	}

}
