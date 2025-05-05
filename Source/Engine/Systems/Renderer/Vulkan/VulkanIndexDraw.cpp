#include "PCH.h"
#include "VulkanIndexDraw.h"
#include "Engine/Components/Material.h"
#include "Engine/Components/Transform.h"

namespace Engine
{

	VulkanIndexDraw::VulkanIndexDraw(VkDevice device, VkPhysicalDevice physicalDevice, const int MAX_EXPECTED_INSTANCES, const int MAX_FRAMES_IN_FLIGHT)
		: device(device), physicalDevice(physicalDevice)
	{
		instanceBuffer = std::make_unique<Engine::VulkanInstanceBuffer>(
			device,
			physicalDevice,
			sizeof(GpuInstanceData),
			MAX_EXPECTED_INSTANCES,
			MAX_FRAMES_IN_FLIGHT
		);
	}

	void VulkanIndexDraw::UpdateInstanceBuffer(uint32_t frameIndex)
	{
		cpuInstanceData.clear();
		meshToInstanceOffsets.clear(); // Reset per frame

		auto& scene = SwimEngine::GetInstance()->GetSceneSystem()->GetActiveScene();
		auto& registry = scene->GetRegistry();
		auto view = registry.view<Transform, Material>();

		uint32_t instanceIndex = 0;
		for (auto entity : view)
		{
			const auto& transform = view.get<Transform>(entity);
			const auto& mat = view.get<Material>(entity).data;
			const auto& mesh = mat->mesh;

			GpuInstanceData instance{};
			instance.model = transform.GetModelMatrix();
			instance.textureIndex = mat->albedoMap ? mat->albedoMap->GetBindlessIndex() : UINT32_MAX;
			instance.hasTexture = mat->albedoMap ? 1.0f : 0.0f;
			instance.padA = instance.padB = 0.0f;

			cpuInstanceData.push_back(instance);

			// First time seeing this mesh, store starting index
			if (meshToInstanceOffsets.find(mesh) == meshToInstanceOffsets.end())
			{
				meshToInstanceOffsets[mesh].firstInstance = instanceIndex;
			}

			// Always increment count
			meshToInstanceOffsets[mesh].count++;

			instanceIndex++;
		}

		// Upload all instance data to GPU for current frame
		void* dst = instanceBuffer->BeginFrame(frameIndex);
		memcpy(dst, cpuInstanceData.data(), sizeof(GpuInstanceData) * cpuInstanceData.size());
	}

	inline MeshBufferData& VulkanIndexDraw::GetOrCreateMeshBuffers(const std::shared_ptr<Mesh>& mesh)
	{
		// Check if the Mesh already has its MeshBufferData initialized, it pretty much always should due to MeshPool::RegisterMesh()
		if (mesh->meshBufferData)
		{
			return *mesh->meshBufferData;
		}
		// Below is back up code:

		// Create a new MeshBufferData instance
		auto data = std::make_shared<MeshBufferData>();

		// Generate GPU buffers (Vulkan version requires device + physicalDevice)
		data->GenerateBuffers(mesh->vertices, mesh->indices, device, physicalDevice);

		// Store the newly created MeshBufferData in the Mesh
		mesh->meshBufferData = data;

		return *data;
	}

	void VulkanIndexDraw::DrawIndexed(uint32_t frameIndex, VkCommandBuffer cmd)
	{
		// Bind instance buffer at binding 1
		VkBuffer instanceBuf = instanceBuffer->GetBuffer(frameIndex);
		VkDeviceSize instanceOffset = 0;

		// Group entities by mesh to minimize pipeline bindings
		std::unordered_map<std::shared_ptr<Mesh>, std::vector<uint32_t>> meshToInstanceIndices;

		auto& scene = SwimEngine::GetInstance()->GetSceneSystem()->GetActiveScene();
		auto& registry = scene->GetRegistry();
		auto view = registry.view<Transform, Material>();

		uint32_t instanceIndex = 0;
		for (auto entity : view)
		{
			const auto& mat = view.get<Material>(entity).data;
			meshToInstanceIndices[mat->mesh].push_back(instanceIndex++);
		}

		// For each unique mesh, bind once and draw all its instances
		for (const auto& [meshPtr, range] : meshToInstanceOffsets)
		{
			auto& meshData = GetOrCreateMeshBuffers(meshPtr);

			VkBuffer vertexBuffers[] = { meshData.vertexBuffer->GetBuffer(), instanceBuf };
			VkDeviceSize offsets[] = { 0, 0 };

			vkCmdBindVertexBuffers(cmd, 0, 2, vertexBuffers, offsets);
			vkCmdBindIndexBuffer(cmd, meshData.indexBuffer->GetBuffer(), 0, VK_INDEX_TYPE_UINT16);

			vkCmdDrawIndexed(cmd, meshData.indexCount, range.count, 0, 0, range.firstInstance);
		}
	}

	void VulkanIndexDraw::CleanUp()
	{
		if (instanceBuffer)
		{
			instanceBuffer->Cleanup();
			instanceBuffer.reset();
		}

		// I guess empty this stuff out too
		cpuInstanceData.clear();
		meshToInstanceOffsets.clear();
	}

}