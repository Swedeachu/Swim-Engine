#pragma once

#include "Buffers/VulkanInstanceBuffer.h"
#include "Buffers/VulkanGpuInstanceData.h"
#include "Engine/Systems/Renderer/Core/Meshes/Mesh.h"

namespace Engine
{

	class VulkanIndexDraw
	{

	public:

		VulkanIndexDraw(VkDevice device, VkPhysicalDevice physicalDevice, const int MAX_EXPECTED_INSTANCES, const int MAX_FRAMES_IN_FLIGHT);

		void UpdateInstanceBuffer(uint32_t frameIndex);

		void DrawIndexed(uint32_t frameIndex, VkCommandBuffer cmd);

		void CleanUp();

		const std::unique_ptr<VulkanInstanceBuffer>& GetInstanceBuffer() const { return instanceBuffer; }

	private:

		inline MeshBufferData& GetOrCreateMeshBuffers(const std::shared_ptr<Mesh>& mesh);

		VkDevice device;
		VkPhysicalDevice physicalDevice;

		// Instance buffer manager
		std::unique_ptr<VulkanInstanceBuffer> instanceBuffer;
		// Stores GPU instance data to upload
		std::vector<GpuInstanceData> cpuInstanceData;

		struct MeshInstanceRange
		{
			uint32_t firstInstance = 0;
			uint32_t count = 0;
		};

		// Sorted per-frame instance draw batches
		std::vector<std::pair<std::shared_ptr<Mesh>, MeshInstanceRange>> instanceBatches;

	};

}