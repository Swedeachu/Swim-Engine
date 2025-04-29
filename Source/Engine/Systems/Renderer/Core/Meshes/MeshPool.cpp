#include "PCH.h"
#include "MeshPool.h"
#include "Engine/SwimEngine.h"
#include "Engine/Systems/Renderer/Vulkan/VulkanRenderer.h"

namespace Engine
{

	MeshPool& MeshPool::GetInstance()
	{
		static MeshPool instance;
		return instance;
	}

	std::shared_ptr<Mesh> MeshPool::RegisterMesh(const std::string& name, const std::vector<Vertex>& vertices, const std::vector<uint16_t>& indices)
	{
		std::lock_guard<std::mutex> lock(poolMutex);

		// Check if the mesh already exists
		auto it = meshes.find(name);
		if (it != meshes.end())
		{
			return it->second; // Return existing mesh
		}

		// Create the mesh and its buffer data
		auto mesh = std::make_shared<Mesh>(vertices, indices);
		mesh->meshBufferData = std::make_shared<MeshBufferData>();

		if constexpr (SwimEngine::CONTEXT == SwimEngine::RenderContext::Vulkan)
		{
			// Lazily cache the Vulkan device + physical device
			if (!vulkanDevicesCached)
			{
				std::shared_ptr<SwimEngine>& engine = SwimEngine::GetInstanceRef();
				std::shared_ptr<VulkanRenderer> renderer = engine->GetVulkanRenderer();
				cachedDevice = renderer->GetDevice();
				cachedPhysicalDevice = renderer->GetPhysicalDevice();
				vulkanDevicesCached = true;
			}

			mesh->meshBufferData->GenerateBuffers(vertices, indices, cachedDevice, cachedPhysicalDevice);
		}
		else if constexpr (SwimEngine::CONTEXT == SwimEngine::RenderContext::OpenGL)
		{
			mesh->meshBufferData->GenerateBuffers(vertices, indices); // No Vulkan args
		}

		meshes.emplace(name, mesh);
		return mesh;
	}

	std::shared_ptr<Mesh> MeshPool::GetMesh(const std::string& name) const
	{
		std::lock_guard<std::mutex> lock(poolMutex);

		auto it = meshes.find(name);
		if (it != meshes.end())
		{
			return it->second;
		}

		return nullptr; // Mesh not found
	}

	bool MeshPool::RemoveMesh(const std::string& name)
	{
		std::lock_guard<std::mutex> lock(poolMutex);
		return meshes.erase(name) > 0;
	}

	void MeshPool::Flush()
	{
		std::lock_guard<std::mutex> lock(poolMutex);

		for (auto& [name, mesh] : meshes)
		{
			if (mesh && mesh->meshBufferData)
			{
				mesh->meshBufferData->Free();
				mesh->meshBufferData.reset();
			}
		}

		// Clear all meshes from the pool too
		meshes.clear();
	}

}