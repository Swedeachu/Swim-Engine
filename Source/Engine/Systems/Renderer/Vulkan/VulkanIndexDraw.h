#pragma once

#include "Buffers/VulkanInstanceBuffer.h"
#include "Buffers/VulkanGpuInstanceData.h"
#include "Engine/Systems/Renderer/Core/Meshes/Mesh.h"

namespace Engine
{

	class VulkanIndexDraw
	{

	public:

		/*
		 NONE: Fastest for batch drawing in simple scenes with thousands of the same meshes.
		 CPU: Solid balanced stratedgy and a geniunely good solution for complex scenes with thousands of unique meshes.
		 GPU: Best solution on paper but our implementation is super broken and glitchy for more reasons than one.

		 CPU_DYNAMIC: 
		  Scene analysis to see when doing the culling calculation is worth it or not for the current frame.
			This could literally save 1000+ fps each frame even if we are using CPU or GPU culling only.
			I think a good way to do this is by analysing the active meshes in the scene and seeing how many of them are different and the amount of them.
			For example, a scene with 10K unique meshes being used must be culled, but a scene with 10K shared meshes can just be batched instantly.
		*/
		enum CullMode { NONE, CPU, GPU };

		VulkanIndexDraw(VkDevice device, VkPhysicalDevice physicalDevice, const int MAX_EXPECTED_INSTANCES, const int MAX_FRAMES_IN_FLIGHT);

		void CreateIndirectBuffers(uint32_t maxDrawCalls, uint32_t framesInFlight);

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

		void SetCulledMode(CullMode mode) { cullMode = mode; }
		void SetUseIndirectDrawing(bool value) { useIndirectDrawing = value; }

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

		CullMode cullMode{ CullMode::NONE };

		std::vector<std::pair<std::shared_ptr<Mesh>, MeshInstanceRange>> instanceBatches;

		// Buffers for visibility culling (compute shader output)
		std::unique_ptr<VulkanBuffer> visibleModelBuffer;
		std::unique_ptr<VulkanBuffer> visibleDataBuffer;
		std::unique_ptr<VulkanBuffer> drawCountBuffer;
		std::unique_ptr<VulkanBuffer> instanceMetaBuffer;

		std::vector<glm::uvec4> culledVisibleData; // GPU visible output buffer read into CPU
		uint32_t instanceCountCulled = 0;          // Count of instances that passed culling

		// Grouped draw commands for indirect rendering
		struct MeshIndirectDrawBatch
		{
			std::shared_ptr<Mesh> mesh;
			std::vector<VkDrawIndexedIndirectCommand> commands;
		};

		std::vector<std::unique_ptr<VulkanBuffer>> indirectCommandBuffers; // [frameCount]
		std::vector<std::vector<MeshIndirectDrawBatch>> drawBatchesPerFrame; // [frameCount][meshBatch]

		uint32_t currentDrawCount{ 0 };
		bool useIndirectDrawing{ false };

	};

}
