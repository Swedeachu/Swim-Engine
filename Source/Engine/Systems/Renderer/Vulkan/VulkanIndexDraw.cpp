#include "PCH.h"
#include "VulkanIndexDraw.h"
#include "Engine/Components/Material.h"
#include "Engine/Components/Transform.h"
#include "Engine/Systems/Renderer/Core/Meshes/MeshPool.h"
#include "Engine/Systems/Renderer/Core/Camera/Frustum.h"

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

	void VulkanIndexDraw::CreateIndirectBuffers(uint32_t maxDrawCalls, uint32_t framesInFlight)
	{
		indirectCommandBuffers.resize(framesInFlight);
		drawBatchesPerFrame.resize(framesInFlight);

		for (uint32_t i = 0; i < framesInFlight; ++i)
		{
			indirectCommandBuffers[i] = std::make_unique<VulkanBuffer>(
				device,
				physicalDevice,
				sizeof(VkDrawIndexedIndirectCommand) * maxDrawCalls,
				VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
			);

			drawBatchesPerFrame[i].clear();
		}
	}

	// This actually does CPU culling logic here if that mode is activated
	void VulkanIndexDraw::UpdateInstanceBuffer(uint32_t frameIndex)
	{
		cpuInstanceData.clear(); // was resize(0)
		instanceBatches.clear();
		rangeMap.clear();
		drawBatchesPerFrame[frameIndex].clear();

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
		}
		else
		{
			GatherCandidatesView(registry, frustum);
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

		// Build rangeMap (mesh -> {firstInstance, count}) 
		for (uint32_t i = 0; i < cpuInstanceData.size(); ++i)
		{
			const GpuInstanceData& instance = cpuInstanceData[i];
			std::shared_ptr<Mesh> mesh = MeshPool::GetInstance().GetMeshByID(instance.meshInfoIndex);
			if (!mesh) continue;

			MeshInstanceRange& range = rangeMap[mesh];
			if (range.count == 0u)
			{
				range.firstInstance = i;  // first element of the now contiguous block
			}
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

	void VulkanIndexDraw::GatherCandidatesView(const entt::registry& registry, const Frustum* frustum)
	{
		auto view = registry.view<Transform, Material>();

		for (auto& entity : view)
		{
			AddInstance(view.get<Transform>(entity), view.get<Material>(entity).data, frustum);
		}
	}

	void VulkanIndexDraw::AddInstance(const Transform& transform, const std::shared_ptr<MaterialData>& mat, const Frustum* frustum)
	{
		const std::shared_ptr<Mesh>& mesh = mat->mesh;
		if (frustum)
		{
			const glm::vec3& min = mesh->meshBufferData->aabbMin;
			const glm::vec3& max = mesh->meshBufferData->aabbMax;
			if (!frustum->IsVisibleLazy(min, max, transform.GetModelMatrix()))
			{ 
				return; 
			}
		}

		GpuInstanceData instance{};
		instance.model = transform.GetModelMatrix();
		instance.aabbMin = glm::vec4(mesh->meshBufferData->aabbMin, 0.0f);
		instance.aabbMax = glm::vec4(mesh->meshBufferData->aabbMax, 0.0f);
		instance.textureIndex = mat->albedoMap ? mat->albedoMap->GetBindlessIndex() : UINT32_MAX;
		instance.hasTexture = mat->albedoMap ? 1.0f : 0.0f;
		instance.meshInfoIndex = mesh->meshBufferData->GetMeshID();
		instance.materialIndex = 0u;

		cpuInstanceData.push_back(instance);
	}

	void VulkanIndexDraw::UploadAndBatchInstances(uint32_t frameIndex)
	{
		instanceBatches.reserve(rangeMap.size());
		for (auto& [mesh, range] : rangeMap)
		{
			instanceBatches.emplace_back(mesh, range);
		}

		// a crash can happen here if you try to draw too much
		void* dst = instanceBuffer->BeginFrame(frameIndex);
		memcpy(dst, cpuInstanceData.data(), sizeof(GpuInstanceData) * cpuInstanceData.size());

		if (!useIndirectDrawing)
		{
			return;
		}

		std::vector<MeshIndirectDrawBatch>& frameBatches = drawBatchesPerFrame[frameIndex];
		frameBatches.reserve(instanceBatches.size());

		for (const auto& [mesh, range] : instanceBatches)
		{
			MeshBufferData& meshData = GetOrCreateMeshBuffers(mesh);

			VkDrawIndexedIndirectCommand cmd{};
			cmd.indexCount = meshData.indexCount;
			cmd.instanceCount = range.count;
			cmd.firstIndex = 0;
			cmd.vertexOffset = 0;
			cmd.firstInstance = range.firstInstance;

			MeshIndirectDrawBatch batch{};
			batch.mesh = mesh;
			batch.commands.push_back(cmd);

			frameBatches.push_back(std::move(batch));
		}

		std::vector<VkDrawIndexedIndirectCommand> allCommands;
		for (const MeshIndirectDrawBatch& batch : frameBatches)
		{
			allCommands.insert(allCommands.end(), batch.commands.begin(), batch.commands.end());
		}

		VulkanBuffer& indirectBuf = *indirectCommandBuffers[frameIndex];
		indirectBuf.CopyData(allCommands.data(), allCommands.size() * sizeof(VkDrawIndexedIndirectCommand));
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
		std::shared_ptr<MeshBufferData> data = std::make_shared<MeshBufferData>();

		// Generate GPU buffers (Vulkan version requires device + physicalDevice)
		data->GenerateBuffers(mesh->vertices, mesh->indices, device, physicalDevice);

		// Store the newly created MeshBufferData in the Mesh
		mesh->meshBufferData = data;

		return *data;
	}

	void VulkanIndexDraw::DrawIndexed(uint32_t frameIndex, VkCommandBuffer cmd)
	{
		if (useIndirectDrawing)
		{
			VkBuffer instanceBuf = instanceBuffer->GetBuffer(frameIndex);
			VkBuffer indirectBuf = indirectCommandBuffers[frameIndex]->GetBuffer();
			VkDeviceSize offsets[] = { 0, 0 };

			const std::vector<MeshIndirectDrawBatch>& frameBatches = drawBatchesPerFrame[frameIndex];
			uint32_t offset = 0;

			// TODO: mega mesh buffer to truly have this be one draw call
			for (const auto& batch : frameBatches)
			{
				MeshBufferData& meshData = GetOrCreateMeshBuffers(batch.mesh);

				VkBuffer vertexBuffers[] = {
					meshData.vertexBuffer->GetBuffer(),
					instanceBuf
				};

				vkCmdBindVertexBuffers(cmd, 0, 2, vertexBuffers, offsets);
				vkCmdBindIndexBuffer(cmd, meshData.indexBuffer->GetBuffer(), 0, VK_INDEX_TYPE_UINT16);

				vkCmdDrawIndexedIndirect(
					cmd,
					indirectBuf,
					offset,
					static_cast<uint32_t>(batch.commands.size()),
					sizeof(VkDrawIndexedIndirectCommand)
				);

				offset += static_cast<uint32_t>(batch.commands.size()) * sizeof(VkDrawIndexedIndirectCommand);
			}
		}
		else
		{
			VkBuffer instanceBuf = instanceBuffer->GetBuffer(frameIndex);
			VkDeviceSize offsets[] = { 0, 0 };

			// Iterate through batches and draw each mesh with instancing
			for (const auto& [mesh, range] : instanceBatches)
			{
				MeshBufferData& meshData = GetOrCreateMeshBuffers(mesh);

				VkBuffer vertexBuffers[] = { meshData.vertexBuffer->GetBuffer(), instanceBuf };

				vkCmdBindVertexBuffers(cmd, 0, 2, vertexBuffers, offsets);
				vkCmdBindIndexBuffer(cmd, meshData.indexBuffer->GetBuffer(), 0, VK_INDEX_TYPE_UINT16);
				vkCmdDrawIndexed(cmd, meshData.indexCount, range.count, 0, 0, range.firstInstance);
			}
		}
	}

	void VulkanIndexDraw::DebugWireframeDraw()
	{
		std::shared_ptr<Scene>& scene = SwimEngine::GetInstance()->GetSceneSystem()->GetActiveScene();
		SceneDebugDraw* debugDraw = scene->GetSceneDebugDraw();
		if (!debugDraw || !debugDraw->IsEnabled()) { return; }

		entt::registry& debugRegistry = debugDraw->GetRegistry();
		const Frustum& frustum = Frustum::Get();
		constexpr bool cullWireframe = true;

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

			cpuInstanceData.push_back(instance);
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
		instanceBatches.clear();
		culledVisibleData.clear();
	}

}
