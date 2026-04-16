#pragma once

#include "Buffers/VulkanInstanceBuffer.h"
#include "Buffers/VulkanGpuInstanceData.h"
#include "Engine/Systems/Renderer/Core/Meshes/Mesh.h"
#include "Engine/Systems/Renderer/Core/Material/MaterialData.h"
#include "Library/EnTT/entt.hpp"

#include <deque>

namespace Engine
{

	// Forward declare
	enum class TransformSpace;
	class Scene;
	class Transform;
	struct Frustum;

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

		void UploadMeshToMegaBuffer(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices, MeshBufferData& meshData);

		void UpdateInstanceBuffer(uint32_t frameIndex);

		void DrawIndexedWorldMeshes(uint32_t frameIndex, VkCommandBuffer cmd);

		void DrawIndexedScreenSpaceAndDecoratedMeshes(uint32_t frameIndex, VkCommandBuffer cmd);

		void DrawIndexedMsdfText(uint32_t frameIndex, VkCommandBuffer cmd, TransformSpace space);

		void CleanUp();

		const std::unique_ptr<VulkanInstanceBuffer>& GetInstanceBuffer() const { return instanceBuffer; }

		// CPU is easily the best option right now, so much so that GPU isn't worth trying to fix and get working (yet)
		void SetCulledMode(CullMode mode) { cullMode = mode; }

		// Major performance booster
		void SetUseQueriedFrustumSceneBVH(bool value) { useQueriedFrustumSceneBVH = value; }

		uint32_t GetInstanceCount() const { return static_cast<uint32_t>(cpuInstanceData.size()); }

	private:

		// Helpers for instance buffer update
		void BuildFullScenePacket(Scene& scene);
		bool CanUseFullScenePacket(const Scene& scene, const Frustum* frustum) const;
		bool UpdateFullScenePacketDirtyEntities(Scene& scene);
		bool PatchFullScenePacketFrame(uint32_t frameIndex);
		void UploadFullScenePacket(uint32_t frameIndex, Scene& scene);
		void GatherCandidatesBVH(Scene& scene, const Frustum& frustum);
		void GatherCandidatesView(entt::registry& registry, const TransformSpace space, const Frustum* frustum);
		void AddInstance(entt::registry& registry, entt::entity entity, const Transform& transform, const std::shared_ptr<MaterialData>& mat, const Frustum* frustum);
		void UploadAndBatchInstances(uint32_t frameIndex);
		bool CanReuseCachedWorldPacket(const Scene& scene, const Frustum* frustum) const;
		void UploadCachedWorldPacketToFrame(uint32_t frameIndex);

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

		VkDevice device;
		VkPhysicalDevice physicalDevice;

		// Draw data instance buffer per frame
		std::unique_ptr<VulkanInstanceBuffer> instanceBuffer;

		// Draw data instances to feed the buffers per frame
		std::vector<GpuInstanceData> cpuInstanceData;
		std::vector<MeshDecoratorGpuInstanceData> meshDecoratorInstanceData;
		std::vector<MsdfTextGpuInstanceData> msdfInstancesData;

		struct MeshBucket
		{
			uint32_t indexCount = 0;
			VkDeviceSize vertexOffsetInMegaBuffer = 0;
			VkDeviceSize indexOffsetInMegaBuffer = 0;
			std::vector<GpuInstanceData> instances;
		};

		struct FullSceneRenderable
		{
			entt::entity entity{ entt::null };
			std::shared_ptr<MaterialData> material;
			GpuInstanceData baseInstance{};
		};

		CullMode cullMode{ CullMode::NONE };

		struct CachedWorldPacketState
		{
			const Scene* scene = nullptr;
			uint64_t frustumRevision = 0;
			uint64_t renderablesRevision = 0;
			uint64_t packetVersion = 0;
			CullMode cullMode = CullMode::NONE;
			bool usedSceneBVH = false;
			bool valid = false;
		};

		// The world space meshes we put into contiguous buckets to avoid resorting every frame
		std::unordered_map<uint32_t, MeshBucket> meshBuckets;
		std::vector<uint32_t> activeMeshBucketKeys;
		std::vector<VkDrawIndexedIndirectCommand> worldDrawCommands;
		CachedWorldPacketState cachedWorldPacketState;
		std::vector<uint64_t> uploadedWorldPacketVersions;
		size_t cachedWorldInstanceCount = 0;

		std::vector<glm::uvec4> culledVisibleData; // GPU visible output buffer read into CPU
		uint32_t instanceCountCulled = 0;          // Count of instances that passed culling

		struct FullSceneDirtyHistoryEntry
		{
			uint64_t version = 0;
			std::vector<std::pair<uint32_t, uint32_t>> ranges;
		};

		std::vector<FullSceneRenderable> fullSceneRenderables;
		std::vector<VkDrawIndexedIndirectCommand> fullSceneDrawCommands;
		std::unordered_map<entt::entity, std::vector<uint32_t>> fullSceneEntityToInstanceIndices;
		std::deque<FullSceneDirtyHistoryEntry> fullSceneDirtyHistory;
		std::vector<uint64_t> uploadedFullScenePacketVersions;
		std::vector<uint64_t> uploadedFullSceneCommandVersions;
		const Scene* fullScenePacketScene = nullptr;
		uint64_t fullScenePacketRenderablesRevision = 0;
		uint64_t fullScenePacketVersion = 0;
		bool fullScenePacketValid = false;

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

	};

}
