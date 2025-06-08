#include "PCH.h"
#include "VulkanIndexDraw.h"
#include "Engine/Components/Material.h"
#include "Engine/Components/Transform.h"
#include "Engine/Systems/Renderer/Core/Meshes/MeshPool.h"
#include "Engine/Systems/Renderer/Core/Camera/Frustum.h"
#include "VulkanRenderer.h"

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
	}

	void VulkanIndexDraw::CreateIndirectBuffers(uint32_t maxDrawCalls, uint32_t framesInFlight)
	{
		indirectCommandBuffers.resize(framesInFlight);

		for (uint32_t i = 0; i < framesInFlight; ++i)
		{
			indirectCommandBuffers[i] = std::make_unique<VulkanBuffer>(
				device,
				physicalDevice,
				sizeof(VkDrawIndexedIndirectCommand) * maxDrawCalls,
				VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
			);
		}
	}

	void VulkanIndexDraw::CreateMegaMeshBuffers(VkDeviceSize totalVertexBufferSize, VkDeviceSize totalIndexBufferSize)
	{
		// Free previous buffers if they exist
		if (megaVertexBuffer)
		{
			megaVertexBuffer->Free();
			megaVertexBuffer.reset();
		}

		if (megaIndexBuffer)
		{
			megaIndexBuffer->Free();
			megaIndexBuffer.reset();
		}

		// Store sizes
		megaVertexBufferSize = totalVertexBufferSize;
		megaIndexBufferSize = totalIndexBufferSize;

		// === Create mega vertex buffer ===
		megaVertexBuffer = std::make_unique<VulkanBuffer>(
			device,
			physicalDevice,
			totalVertexBufferSize,
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
		);

		// === Create mega index buffer ===
		megaIndexBuffer = std::make_unique<VulkanBuffer>(
			device,
			physicalDevice,
			totalIndexBufferSize,
			VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
		);
	}

	// This makes 2 temporary index and vertex buffers and then copies them into our mega buffers. Also supports auto growth. This also sets the offsets in mesh data.
	void VulkanIndexDraw::UploadMeshToMegaBuffer(const std::vector<Vertex>& vertices, const std::vector<uint16_t>& indices, MeshBufferData& meshData)
	{
		VkDeviceSize vertexSize = vertices.size() * sizeof(Vertex);
		VkDeviceSize indexSize = indices.size() * sizeof(uint16_t);

		if (!HasSpaceForMesh(vertexSize, indexSize))
		{
			GrowMegaBuffers(vertexSize, indexSize);
		}

		VkDeviceSize vertexOffset = currentVertexBufferOffset;
		VkDeviceSize indexOffset = currentIndexBufferOffset;

		VulkanBuffer stagingVertexBuffer(
			device,
			physicalDevice,
			vertexSize,
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
		);
		stagingVertexBuffer.CopyData(vertices.data(), static_cast<size_t>(vertexSize));

		VulkanBuffer stagingIndexBuffer(
			device,
			physicalDevice,
			indexSize,
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
		);
		stagingIndexBuffer.CopyData(indices.data(), static_cast<size_t>(indexSize));

		SwimEngine::GetInstance()->GetVulkanRenderer()->CopyBuffer(
			stagingVertexBuffer.GetBuffer(),
			megaVertexBuffer->GetBuffer(),
			vertexSize,
			vertexOffset
		);

		SwimEngine::GetInstance()->GetVulkanRenderer()->CopyBuffer(
			stagingIndexBuffer.GetBuffer(),
			megaIndexBuffer->GetBuffer(),
			indexSize,
			indexOffset
		);

		meshData.vertexOffsetInMegaBuffer = vertexOffset;
		meshData.indexOffsetInMegaBuffer = indexOffset;
		meshData.indexCount = static_cast<uint32_t>(indices.size());

		currentVertexBufferOffset += vertexSize;
		currentIndexBufferOffset += indexSize;
	}

	// This actually does CPU culling logic here if that mode is activated
	void VulkanIndexDraw::UpdateInstanceBuffer(uint32_t frameIndex)
	{
		cpuInstanceData.clear(); // was resize(0)
		rangeMap.clear();

		const std::shared_ptr<Scene>& scene = SwimEngine::GetInstance()->GetSceneSystem()->GetActiveScene();
		entt::registry& registry = scene->GetRegistry();

		const Frustum* frustum = nullptr;
		if (cullMode == CullMode::CPU)
		{
			std::shared_ptr<CameraSystem> camera = scene->GetCameraSystem();
			Frustum::SetCameraMatrices(camera->GetViewMatrix(), camera->GetProjectionMatrix());
			frustum = &Frustum::Get();
		}

		// CPU culling can leverage the scenes BVH spacial partitions
		if (cullMode == CullMode::CPU && frustum && useQueriedFrustumSceneBVH)
		{
			GatherCandidatesBVH(*scene, *frustum);

			// We also need to render screen space entities, as BVH only accounts for world space entities since it is for culling.
			// We don't cull screen space entities, just draw them regularly with the view method.
			GatherCandidatesView(registry, TransformSpace::Screen, frustum);
		}
		else
		{
			// Render everything in world and screen space with the registry view
			GatherCandidatesView(registry, TransformSpace::Ambiguous, frustum);
		}

	#ifdef _DEBUG
		DebugWireframeDraw();
	#endif

		// Enforce mesh-contiguity in cpuInstanceData.
		// Indirect batch draw calls obviously need to be contiguous blocks of the same meshes so we sort them together.
		if (!cpuInstanceData.empty())
		{
			std::sort(cpuInstanceData.begin(),
				cpuInstanceData.end(),
				[](const GpuInstanceData& a, const GpuInstanceData& b)
			{
				return a.meshInfoIndex < b.meshInfoIndex;
			});
		}

		MeshPool& pool = MeshPool::GetInstance();

		// Build rangeMap (mesh -> {firstInstance, count}) 
		for (uint32_t i = 0; i < cpuInstanceData.size(); ++i)
		{
			const GpuInstanceData& instance = cpuInstanceData[i];

			MeshInstanceRange& range = rangeMap[instance.meshInfoIndex];
			if (range.count == 0u)
			{
				range.firstInstance = i;  // first element of the now contiguous block
			}

			// Update data for this mesh instance
			range.indexCount = instance.indexCount;
			range.indexOffsetInMegaBuffer = instance.indexOffsetInMegaBuffer;
			range.vertexOffsetInMegaBuffer = instance.vertexOffsetInMegaBuffer;

			range.count++; // increment for every occurrence
		}

		UploadAndBatchInstances(frameIndex);
	}

	// Fires off the frustum query in the scene's bounding volume hierarchy using immediate callback
	void VulkanIndexDraw::GatherCandidatesBVH(Scene& scene, const Frustum& frustum)
	{
		entt::registry& registry = scene.GetRegistry();

		scene.GetSceneBVH()->QueryFrustumCallback(frustum, [&](entt::entity entity)
		{
			AddInstance(registry.get<Transform>(entity), registry.get<Material>(entity).data, nullptr);
		});
	}

	// Passing space as TransformSpace::Ambiguous will just render all entities
	void VulkanIndexDraw::GatherCandidatesView(const entt::registry& registry, const TransformSpace space, const Frustum* frustum)
	{
		auto view = registry.view<Transform, Material>();

		for (auto& entity : view)
		{
			const Transform& tf = view.get<Transform>(entity);
			if (space == TransformSpace::Ambiguous || tf.GetTransformSpace() == space)
			{
				const auto& mat = view.get<Material>(entity).data;
				AddInstance(tf, mat, frustum);
			}
		}
	}

	void VulkanIndexDraw::AddInstance(const Transform& transform, const std::shared_ptr<MaterialData>& mat, const Frustum* frustum)
	{
		const std::shared_ptr<Mesh>& mesh = mat->mesh;

		// Frustum culling if world-space
		if (frustum && transform.GetTransformSpace() == TransformSpace::World)
		{
			const glm::vec3& min = mesh->meshBufferData->aabbMin;
			const glm::vec3& max = mesh->meshBufferData->aabbMax;
			if (!frustum->IsVisibleLazy(min, max, transform.GetModelMatrix()))
			{
				return;
			}
		}

		GpuInstanceData instance{};

		// --- Screen space transform scaling ---
		if (transform.GetTransformSpace() == TransformSpace::Screen)
		{
			/*
			std::shared_ptr<SwimEngine> engine = SwimEngine::GetInstance();
			unsigned int windowWidth = engine->GetWindowWidth();
			unsigned int windowHeight = engine->GetWindowHeight();

			float scaleX = static_cast<float>(windowWidth) / Renderer::VirtualCanvasWidth;
			float scaleY = static_cast<float>(windowHeight) / Renderer::VirtualCanvasHeight;

			// Converts geometry from virtual units -> pixels
			glm::mat4 resolutionScale = glm::scale(glm::mat4(1.0f), glm::vec3(scaleX, scaleY, 1.0f));
			*/

			instance.model = transform.GetModelMatrix(); //* resolutionScale;
			instance.space = 1u;
		}
		else // otherwise regular world space
		{
			instance.model = transform.GetModelMatrix();
			instance.space = 0u;
		}

		instance.aabbMin = glm::vec4(mesh->meshBufferData->aabbMin, 0.0f);
		instance.aabbMax = glm::vec4(mesh->meshBufferData->aabbMax, 0.0f);
		instance.textureIndex = mat->albedoMap ? mat->albedoMap->GetBindlessIndex() : UINT32_MAX;
		instance.hasTexture = mat->albedoMap ? 1.0f : 0.0f;
		instance.meshInfoIndex = mesh->meshBufferData->GetMeshID();
		instance.materialIndex = 0u; // nothing yet
		instance.indexCount = mesh->meshBufferData->indexCount;
		instance.indexOffsetInMegaBuffer = mesh->meshBufferData->indexOffsetInMegaBuffer;
		instance.vertexOffsetInMegaBuffer = mesh->meshBufferData->vertexOffsetInMegaBuffer;

		cpuInstanceData.push_back(instance);
	}

	void VulkanIndexDraw::UploadAndBatchInstances(uint32_t frameIndex)
	{
		// a crash can happen here if you try to draw too much
		void* dst = instanceBuffer->BeginFrame(frameIndex);
		memcpy(dst, cpuInstanceData.data(), sizeof(GpuInstanceData) * cpuInstanceData.size());

		// Build indirect command buffer
		std::vector<VkDrawIndexedIndirectCommand> allCommands;
		allCommands.reserve(rangeMap.size()); // reserve enough space based on unique meshes

		for (const auto& [mesh, range] : rangeMap)
		{
			VkDrawIndexedIndirectCommand cmd{};
			cmd.indexCount = range.indexCount;
			cmd.instanceCount = range.count;
			cmd.firstIndex = static_cast<uint32_t>(range.indexOffsetInMegaBuffer / sizeof(uint16_t));
			cmd.vertexOffset = static_cast<int32_t>(range.vertexOffsetInMegaBuffer / sizeof(Vertex));
			cmd.firstInstance = range.firstInstance;

			allCommands.push_back(cmd);
		}

		VulkanBuffer& indirectBuf = *indirectCommandBuffers[frameIndex];
		indirectBuf.CopyData(allCommands.data(), allCommands.size() * sizeof(VkDrawIndexedIndirectCommand));
	}

	void VulkanIndexDraw::DrawIndexed(uint32_t frameIndex, VkCommandBuffer cmd)
	{
		VkBuffer instanceBuf = instanceBuffer->GetBuffer(frameIndex);
		VkBuffer indirectBuf = indirectCommandBuffers[frameIndex]->GetBuffer();
		VkDeviceSize offsets[] = { 0, 0 };

		// Use the mega mesh buffer to do the scene in one single draw call
		VkBuffer vertexBuffers[] = {
				megaVertexBuffer->GetBuffer(), // All vertex data lives here
				instanceBuf                    // Instance buffer (per-instance data)
		};

		vkCmdBindVertexBuffers(cmd, 0, 2, vertexBuffers, offsets);
		vkCmdBindIndexBuffer(cmd, megaIndexBuffer->GetBuffer(), 0, VK_INDEX_TYPE_UINT16);

		// UploadAndBatchInstances() already flattened all commands into indirect buffer, 
		// so we can call vkCmdDrawIndexedIndirect once over entire buffer.

		size_t totalCommands = rangeMap.size();

		if (totalCommands > 0)
		{
			vkCmdDrawIndexedIndirect(
				cmd,
				indirectBuf,
				0, // offset
				static_cast<uint32_t>(totalCommands),
				sizeof(VkDrawIndexedIndirectCommand)
			);
		}
	}

	void VulkanIndexDraw::DebugWireframeDraw()
	{
		std::shared_ptr<Scene>& scene = SwimEngine::GetInstance()->GetSceneSystem()->GetActiveScene();
		SceneDebugDraw* debugDraw = scene->GetSceneDebugDraw();
		if (!debugDraw || !debugDraw->IsEnabled()) { return; }

		entt::registry& debugRegistry = debugDraw->GetRegistry();
		const Frustum& frustum = Frustum::Get();
		constexpr bool cullWireframe = false;

		auto view = debugRegistry.view<Transform, DebugWireBoxData>();

		for (auto entity : view)
		{
			const Transform& transform = view.get<Transform>(entity);
			const DebugWireBoxData& box = view.get<DebugWireBoxData>(entity);
			const auto& mesh = debugDraw->GetWireframeCubeMesh(box.color);

			if constexpr (cullWireframe)
			{
				const glm::vec3& min = mesh->meshBufferData->aabbMin;
				const glm::vec3& max = mesh->meshBufferData->aabbMax;
				if (!frustum.IsVisibleLazy(min, max, transform.GetModelMatrix())) { continue; }
			}

			GpuInstanceData instance{};
			instance.model = transform.GetModelMatrix();
			instance.aabbMin = glm::vec4(-0.5f, -0.5f, -0.5f, 0.0f);
			instance.aabbMax = glm::vec4(0.5f, 0.5f, 0.5f, 0.0f);
			instance.textureIndex = UINT32_MAX;
			instance.hasTexture = 0.0f;
			instance.meshInfoIndex = mesh->meshBufferData->GetMeshID();
			instance.materialIndex = 0u;
			instance.indexCount = mesh->meshBufferData->indexCount;
			instance.indexOffsetInMegaBuffer = mesh->meshBufferData->indexOffsetInMegaBuffer;
			instance.vertexOffsetInMegaBuffer = mesh->meshBufferData->vertexOffsetInMegaBuffer;

			cpuInstanceData.push_back(instance);
		}
	}

	void VulkanIndexDraw::GrowMegaBuffers(VkDeviceSize additionalVertexSize, VkDeviceSize additionalIndexSize)
	{
		VkDeviceSize newVertexSize = megaVertexBufferSize + std::max(additionalVertexSize, MESH_BUFFER_GROWTH_SIZE);
		VkDeviceSize newIndexSize = megaIndexBufferSize + std::max(additionalIndexSize, MESH_BUFFER_GROWTH_SIZE);

		std::unique_ptr<VulkanBuffer> newVertexBuffer = std::make_unique<VulkanBuffer>(
			device,
			physicalDevice,
			newVertexSize,
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
		);

		std::unique_ptr<VulkanBuffer> newIndexBuffer = std::make_unique<VulkanBuffer>(
			device,
			physicalDevice,
			newIndexSize,
			VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
		);

		if (megaVertexBuffer && currentVertexBufferOffset > 0)
		{
			SwimEngine::GetInstance()->GetVulkanRenderer()->CopyBuffer(
				megaVertexBuffer->GetBuffer(),
				newVertexBuffer->GetBuffer(),
				currentVertexBufferOffset
			);
		}

		if (megaIndexBuffer && currentIndexBufferOffset > 0)
		{
			SwimEngine::GetInstance()->GetVulkanRenderer()->CopyBuffer(
				megaIndexBuffer->GetBuffer(),
				newIndexBuffer->GetBuffer(),
				currentIndexBufferOffset
			);
		}

		megaVertexBuffer = std::move(newVertexBuffer);
		megaIndexBuffer = std::move(newIndexBuffer);

		megaVertexBufferSize = newVertexSize;
		megaIndexBufferSize = newIndexSize;
	}

	bool VulkanIndexDraw::HasSpaceForMesh(VkDeviceSize vertexSize, VkDeviceSize indexSize) const
	{
		if ((currentVertexBufferOffset + vertexSize <= megaVertexBufferSize) &&
			(currentIndexBufferOffset + indexSize <= megaIndexBufferSize))
		{
			return true;
		}
		else
		{
			return false;
		}
	}

	void VulkanIndexDraw::CleanUp()
	{
		if (instanceBuffer)
		{
			instanceBuffer->Cleanup();
			instanceBuffer.reset();
		}

		cpuInstanceData.clear();
		culledVisibleData.clear();
	}

}
