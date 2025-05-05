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

		cpuInstanceData.reserve(MAX_EXPECTED_INSTANCES);
		instanceBatches.reserve(64); // reasonable default for most scenes
	}

	void VulkanIndexDraw::UpdateInstanceBuffer(uint32_t frameIndex)
	{
		// Resize to zero keeps memory and capacity intact
		cpuInstanceData.resize(0);
		instanceBatches.resize(0);

		auto& scene = SwimEngine::GetInstance()->GetSceneSystem()->GetActiveScene();
		auto& registry = scene->GetRegistry();
		auto view = registry.view<Transform, Material>();

		std::unordered_map<std::shared_ptr<Mesh>, MeshInstanceRange> rangeMap;
		rangeMap.reserve(64); // avoid rehashing during population

		uint32_t instanceIndex = 0;

		// === Populate instance data and group by mesh ===
		for (auto entity : view)
		{
			const auto& transform = view.get<Transform>(entity);
			const auto& mat = view.get<Material>(entity).data;
			const auto& mesh = mat->mesh;

			// Build the GPU-side instance data struct
			GpuInstanceData instance{};
			instance.model = transform.GetModelMatrix();
			instance.textureIndex = mat->albedoMap ? mat->albedoMap->GetBindlessIndex() : UINT32_MAX;
			instance.hasTexture = mat->albedoMap ? 1.0f : 0.0f;
			instance.padA = instance.padB = 0.0f;

			cpuInstanceData.push_back(instance);

			// Fast path: track instance range per mesh with single lookup
			auto& range = rangeMap[mesh];
			if (range.count == 0)
			{
				range.firstInstance = instanceIndex;
			}
			range.count++;

			instanceIndex++;
		}

		// === Flatten into a linear batch array for fast drawing ===
		instanceBatches.reserve(rangeMap.size());
		for (auto& [mesh, range] : rangeMap)
		{
			instanceBatches.emplace_back(mesh, range);
		}

		// === Upload CPU instance buffer to GPU ===
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
		VkBuffer instanceBuf = instanceBuffer->GetBuffer(frameIndex);
		VkDeviceSize offsets[] = { 0, 0 };

		// Iterate through batches and draw each mesh with instancing
		for (const auto& [mesh, range] : instanceBatches)
		{
			auto& meshData = GetOrCreateMeshBuffers(mesh);

			VkBuffer vertexBuffers[] = { meshData.vertexBuffer->GetBuffer(), instanceBuf };

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
		instanceBatches.clear();
	}

}
