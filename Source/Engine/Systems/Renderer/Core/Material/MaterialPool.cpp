#include "PCH.h"
#include "MaterialPool.h"

namespace Engine
{

	MaterialPool& MaterialPool::GetInstance()
	{
		static MaterialPool instance;
		return instance;
	}

	std::shared_ptr<MaterialData> MaterialPool::GetMaterialData(const std::string& name)
	{
		std::lock_guard<std::mutex> lock(poolMutex);

		auto it = materials.find(name);
		if (it != materials.end())
		{
			return it->second;
		}

		return nullptr;
	}

	std::shared_ptr<MaterialData> MaterialPool::RegisterMaterialData(const std::string& name, std::shared_ptr<Mesh> mesh, std::shared_ptr<Texture2D> albedoMap)
	{
		std::lock_guard<std::mutex> lock(poolMutex);

		auto it = materials.find(name);
		if (it != materials.end())
		{
			return it->second;
		}

		auto data = std::make_shared<MaterialData>(mesh, albedoMap);
		materials.emplace(name, data);

		return data;
	}

	void MaterialPool::Flush()
	{
		std::lock_guard<std::mutex> lock(poolMutex);
		materials.clear();
	}

}
