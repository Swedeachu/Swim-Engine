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
		enum CullMode { NONE, CPU, GPU };

		VulkanIndexDraw(VkDevice device, VkPhysicalDevice physicalDevice, const int MAX_EXPECTED_INSTANCES, const int MAX_FRAMES_IN_FLIGHT);

		void CreateIndirectBuffers(uint32_t maxDrawCalls, uint32_t framesInFlight);

		void CreateMegaMeshBuffers(VkDeviceSize totalVertexBufferSize, VkDeviceSize totalIndexBufferSize);

		void UploadMeshToMegaBuffer(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices, MeshBufferData& meshData);

		void UpdateInstanceBuffer(uint32_t frameIndex);

		// Must call this outside the render pass.
		// If not in GPU mode, this is a no-op.
		void RecordGpuCulling(uint32_t frameIndex, VkCommandBuffer cmd);

		void DrawIndexedWorldMeshes(uint32_t frameIndex, VkCommandBuffer cmd);

		void DrawIndexedScreenSpaceAndDecoratedMeshes(uint32_t frameIndex, VkCommandBuffer cmd);

		void DrawIndexedMsdfText(uint32_t frameIndex, VkCommandBuffer cmd, TransformSpace space);

		void CleanUp();

		const std::unique_ptr<VulkanInstanceBuffer>& GetInstanceBuffer() const { return instanceBuffer; }

		void SetCulledMode(CullMode mode) { cullMode = mode; }
		CullMode GetCulledMode() const { return cullMode; }

		// Major performance booster for CPU side 
		void SetUseQueriedFrustumSceneBVH(bool value) { useQueriedFrustumSceneBVH = value; }

		uint32_t GetInstanceCount() const { return static_cast<uint32_t>(cpuInstanceData.size()); }

	private:

		// Helpers for instance buffer update
		void GatherCandidatesBVH(Scene& scene, const Frustum& frustum);
		void GatherCandidatesView(const entt::registry& registry, const TransformSpace space, const Frustum* frustum);
		void AddInstance(const entt::registry& registry, const Transform& transform, const std::shared_ptr<MaterialData>& mat, const Frustum* frustum);
		void UploadAndBatchInstances(uint32_t frameIndex);

		void GrowMegaBuffers(VkDeviceSize additionalVertexSize, VkDeviceSize additionalIndexSize);

		bool HasSpaceForMesh(VkDeviceSize vertexSize, VkDeviceSize indexSize) const;

		void EnsureIndirectCapacity
		(
			std::unique_ptr<VulkanBuffer>& buf,
			size_t commandCount
		);

		void DrawDecoratorsAndScreenSpaceEntitiesInRegistry
		(
			entt::registry& registry,
			const CameraUBO& cameraUBO,
			const glm::mat4& worldView,
			unsigned int windowWidth,
			unsigned int windowHeight,
			const Frustum& frustum,
			uint32_t& instanceCount,
			std::vector<VkDrawIndexedIndirectCommand>& drawCommands,
			bool cull
		);

		// Ensure the static glyph quad exists in mega buffers
		void EnsureGlyphQuadUploaded();

		// Build MSDF instances for the given space (World or Screen)
		void BuildMsdfInstancesForSpace(
			entt::registry& registry,
			TransformSpace space,
			const CameraUBO& cameraUBO,
			const glm::mat4& view,
			const glm::mat4& proj,
			unsigned int windowWidth,
			unsigned int windowHeight,
			std::vector<MsdfTextGpuInstanceData>& outInstances
		);

		// upload + draw for a batch of glyphs
		void UploadAndDrawMsdfBatch(
			uint32_t frameIndex,
			VkCommandBuffer cmd,
			const std::vector<MsdfTextGpuInstanceData>& instances,
			std::unique_ptr<VulkanBuffer>& outIndirectBuf
		);

		// GPU compute culling path (writes indirect commands + draw count).
		void DispatchGpuCulling(uint32_t frameIndex, VkCommandBuffer cmd);

		// Helper for binding the standard world mesh graphics pipeline + descriptor sets
		void BindWorldMeshPipeline(uint32_t frameIndex, VkCommandBuffer cmd);

		VkDevice device;
		VkPhysicalDevice physicalDevice;

		// Draw data instance buffer per frame
		std::unique_ptr<VulkanInstanceBuffer> instanceBuffer;

		// Draw data instances to feed the buffers per frame
		std::vector<GpuInstanceData> cpuInstanceData;
		std::vector<MeshDecoratorGpuInstanceData> meshDecoratorInstanceData;
		std::vector<MsdfTextGpuInstanceData> msdfInstancesData;

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

		// One static quad to render all glyphs with
		bool hasUploadedGlyphQuad = false;
		MeshBufferData glyphQuadMesh = {};

		// Command buffers per frame
		std::vector<std::unique_ptr<VulkanBuffer>> indirectCommandBuffers;
		std::vector<std::unique_ptr<VulkanBuffer>> meshDecoratorIndirectCommandBuffers;
		std::vector<std::unique_ptr<VulkanBuffer>> msdfIndirectCommandBuffers;

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

		bool useQueriedFrustumSceneBVH{ true };

		// Compute shader tuning
		static constexpr uint32_t GPU_CULL_LOCAL_SIZE = 256;

	};

}
