#pragma once

#include "Buffers/VulkanInstanceBuffer.h"
#include "Buffers/VulkanGpuInstanceData.h"
#include "Engine/Systems/Renderer/Core/Meshes/Mesh.h"
#include "Engine/Systems/Renderer/Core/Material/MaterialData.h"

namespace Engine
{

	// Forward declare
	enum class TransformSpace;

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

		void CreateMegaMeshBuffers(VkDeviceSize totalVertexBufferSize, VkDeviceSize totalIndexBufferSize);

		void UploadMeshToMegaBuffer(const std::vector<Vertex>& vertices, const std::vector<uint16_t>& indices, MeshBufferData& meshData);

		void UpdateInstanceBuffer(uint32_t frameIndex);

		void DrawIndexedWorld(uint32_t frameIndex, VkCommandBuffer cmd);

		void DrawIndexedScreenAndDecoratorUI(uint32_t frameIndex, VkCommandBuffer cmd);

		void CleanUp();

		const std::unique_ptr<VulkanInstanceBuffer>& GetInstanceBuffer() const { return instanceBuffer; }

		// CPU is easily the best option right now, so much so that GPU isn't worth trying to fix and get working (yet)
		void SetCulledMode(CullMode mode) { cullMode = mode; }

		// Major performance booster
		void SetUseQueriedFrustumSceneBVH(bool value) { useQueriedFrustumSceneBVH = value; }

		uint32_t GetInstanceCount() const { return static_cast<uint32_t>(cpuInstanceData.size()); }

	private:

		// Helpers for instance buffer update
		void GatherCandidatesBVH(Scene& scene, const Frustum& frustum);
		void GatherCandidatesView(const entt::registry& registry, const TransformSpace space, const Frustum* frustum);
		void AddInstance(const Transform& transform, const std::shared_ptr<MaterialData>& mat, const Frustum* frustum);
		void UploadAndBatchInstances(uint32_t frameIndex);

		void GrowMegaBuffers(VkDeviceSize additionalVertexSize, VkDeviceSize additionalIndexSize);

		bool HasSpaceForMesh(VkDeviceSize vertexSize, VkDeviceSize indexSize) const;

		void DebugWireframeDraw();

		VkDevice device;
		VkPhysicalDevice physicalDevice;

		// Draw data instance buffer per frame
		std::unique_ptr<VulkanInstanceBuffer> instanceBuffer;

		// Draw data instances to feed the buffers per frame
		std::vector<GpuInstanceData> cpuInstanceData;
		std::vector<UIParams> uiParamData;

		struct MeshInstanceRange
		{
			uint32_t firstInstance = 0;
			uint32_t count = 0;
			uint32_t indexCount = 0;
			VkDeviceSize vertexOffsetInMegaBuffer = 0;
			VkDeviceSize indexOffsetInMegaBuffer = 0;
		};

		CullMode cullMode{ CullMode::NONE };

		// The world space meshes we put into a contiguous buffer to batch draw with
		std::unordered_map<uint32_t, MeshInstanceRange> rangeMap;

		std::vector<glm::uvec4> culledVisibleData; // GPU visible output buffer read into CPU
		uint32_t instanceCountCulled = 0;          // Count of instances that passed culling

		// Grouped draw commands for indirect rendering
		struct MeshIndirectDrawBatch
		{
			std::shared_ptr<Mesh> mesh;
			std::vector<VkDrawIndexedIndirectCommand> commands;
		};

		// Command buffers per frame
		std::vector<std::unique_ptr<VulkanBuffer>> indirectCommandBuffers; 
		std::vector<std::unique_ptr<VulkanBuffer>> uiIndirectCommandBuffers; 

		// Mega mesh buffers
		std::unique_ptr<VulkanBuffer> megaVertexBuffer;
		std::unique_ptr<VulkanBuffer> megaIndexBuffer;

		// Track how large the mega buffers are 
		VkDeviceSize megaVertexBufferSize = 0;
		VkDeviceSize megaIndexBufferSize = 0;

		// Tracking offsets
		VkDeviceSize currentVertexBufferOffset = 0;
		VkDeviceSize currentIndexBufferOffset = 0;

		// How big to grow each time we run out
		static constexpr VkDeviceSize MESH_BUFFER_GROWTH_SIZE = 8 * 1024 * 1024; // 8 MB blocks

		bool useQueriedFrustumSceneBVH{ false };

	};

}
