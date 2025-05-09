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

		void CreateCullOutputBuffers(uint32_t maxInstances);

		void UpdateInstanceBuffer(uint32_t frameIndex);

		void DrawIndexed(uint32_t frameIndex, VkCommandBuffer cmd);

		void DrawCulledIndexed(uint32_t frameIndex, VkCommandBuffer cmd); 

		void ReadbackCulledInstanceData();

		void ZeroDrawCount();

		void CleanUp();

		const std::unique_ptr<VulkanInstanceBuffer>& GetInstanceBuffer() const { return instanceBuffer; }

		VulkanBuffer* GetVisibleModelBuffer() const { return visibleModelBuffer.get(); }
		VulkanBuffer* GetVisibleDataBuffer() const { return visibleDataBuffer.get(); }
		VulkanBuffer* GetDrawCountBuffer() const { return drawCountBuffer.get(); }
		VulkanBuffer* GetInstanceMetaBuffer() const { return instanceMetaBuffer.get(); }

		void SetUseCulledDraw(bool enabled) { useCulledDraw = enabled; }

		uint32_t GetInstanceCount() const { return static_cast<uint32_t>(cpuInstanceData.size()); }

	private:

		inline MeshBufferData& GetOrCreateMeshBuffers(const std::shared_ptr<Mesh>& mesh);

		VkDevice device;
		VkPhysicalDevice physicalDevice;

		std::unique_ptr<VulkanInstanceBuffer> instanceBuffer;

		std::vector<GpuInstanceData> cpuInstanceData;

		struct MeshInstanceRange
		{
			uint32_t firstInstance = 0;
			uint32_t count = 0;
		};

		std::vector<std::pair<std::shared_ptr<Mesh>, MeshInstanceRange>> instanceBatches;

		// Buffers for visibility culling (compute shader output)
		std::unique_ptr<VulkanBuffer> visibleModelBuffer;
		std::unique_ptr<VulkanBuffer> visibleDataBuffer;
		std::unique_ptr<VulkanBuffer> drawCountBuffer;
		std::unique_ptr<VulkanBuffer> instanceMetaBuffer;

		std::vector<glm::uvec4> culledVisibleData; // GPU visible output buffer read into CPU
		uint32_t instanceCountCulled = 0;          // Count of instances that passed culling

		bool useCulledDraw{ false };

	};

}
