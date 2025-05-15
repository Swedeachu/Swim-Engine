#pragma once

#include "Buffers/VulkanInstanceBuffer.h"
#include "Buffers/VulkanGpuInstanceData.h"
#include "Engine/Systems/Renderer/Core/Meshes/Mesh.h"
#include "Engine/Systems/Renderer/Core/Material/MaterialData.h"

namespace Engine
{

	class VulkanIndexDraw
	{

	public:

		/*
		 CPU: Solid balanced stratedgy and a geniunely good solution for complex scenes with thousands of unique meshes.
		 GPU: Best solution on paper but our implementation is super broken and glitchy for more reasons than one.
		*/
		enum CullMode { NONE, CPU, GPU }; // GPU is not implemented 

		VulkanIndexDraw(VkDevice device, VkPhysicalDevice physicalDevice, const int MAX_EXPECTED_INSTANCES, const int MAX_FRAMES_IN_FLIGHT);

		void CreateIndirectBuffers(uint32_t maxDrawCalls, uint32_t framesInFlight);

		void UpdateInstanceBuffer(uint32_t frameIndex);

		void DrawIndexed(uint32_t frameIndex, VkCommandBuffer cmd);

		void CleanUp();

		const std::unique_ptr<VulkanInstanceBuffer>& GetInstanceBuffer() const { return instanceBuffer; }

		// CPU is easily the best option right now, so much so that GPU isn't worth trying to fix and get working (yet)
		void SetCulledMode(CullMode mode) { cullMode = mode; }

		// No reason to not use this, fundamental GPU optimization for batch draw calls.
		void SetUseIndirectDrawing(bool value) { useIndirectDrawing = value; }

		// it's a great performance boost but MAYBE has edge case issues in SceneBVH logic with over culling.
		// So far seems to work fine actually so I have it enabled in VulkanRenderer::Awake().
		void SetUseQueriedFrustumSceneBVH(bool value) { useQueriedFrustumSceneBVH = value; }

		uint32_t GetInstanceCount() const { return static_cast<uint32_t>(cpuInstanceData.size()); }

	private:

		inline MeshBufferData& GetOrCreateMeshBuffers(const std::shared_ptr<Mesh>& mesh);

		// Helpers for instance buffer update
		void GatherCandidatesBVH(Scene& scene, const Frustum& frustum);
		void GatherCandidatesView(const entt::registry& registry, const Frustum* frustum);
		void AddInstance(const Transform& transform, const std::shared_ptr<MaterialData>& mat, const Frustum* frustum);
		void UploadAndBatchInstances(uint32_t frameIndex);

		void DebugWireframeDraw();

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

		std::unordered_map<std::shared_ptr<Mesh>, MeshInstanceRange> rangeMap;
		std::vector<std::pair<std::shared_ptr<Mesh>, MeshInstanceRange>> instanceBatches;

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

		bool useIndirectDrawing{ false };
		bool useQueriedFrustumSceneBVH{ false };

	};

}
