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

	void VulkanIndexDraw::CreateCullOutputBuffers(uint32_t maxInstances)
	{
		constexpr VkBufferUsageFlags storageAndVertex =
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
			VK_BUFFER_USAGE_TRANSFER_DST_BIT;

		VkDeviceSize matrixSize = sizeof(glm::mat4) * maxInstances;
		VkDeviceSize instanceDataSize = sizeof(uint32_t) * 4 * maxInstances; // match uint4 layout
		VkDeviceSize drawCountSize = sizeof(uint32_t);

		visibleModelBuffer = std::make_unique<VulkanBuffer>(
			device, physicalDevice, matrixSize,
			storageAndVertex,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
		);

		drawCountBuffer = std::make_unique<VulkanBuffer>(
			device, physicalDevice, drawCountSize,
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
			VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | // critical for raw access
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
			VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
		);

		visibleDataBuffer = std::make_unique<VulkanBuffer>(
			device, physicalDevice, instanceDataSize,
			storageAndVertex,
			// Same here to allow CPU readback
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
		);

		instanceMetaBuffer = std::make_unique<VulkanBuffer>(
			device, physicalDevice,
			sizeof(uint32_t),
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
		);
	}

	// This actually does CPU culling logic here if that mode is activated
	void VulkanIndexDraw::UpdateInstanceBuffer(uint32_t frameIndex)
	{
		// Clear CPU-side collections but preserve capacity
		cpuInstanceData.resize(0); // resize because object reuse is expected
		instanceBatches.clear(); 
		drawBatchesPerFrame[frameIndex].clear(); // Reset indirect batch data for this frame

		auto& scene = SwimEngine::GetInstance()->GetSceneSystem()->GetActiveScene();
		auto& registry = scene->GetRegistry();
		auto view = registry.view<Transform, Material>();

		std::unordered_map<std::shared_ptr<Mesh>, MeshInstanceRange> rangeMap;
		rangeMap.reserve(64); // avoid rehashing

		uint32_t instanceIndex = 0;

		// Frustum pointer (set if CPU culling is enabled)
		const Frustum* frustum = nullptr;
		if (cullMode == CullMode::CPU)
		{
			auto camera = scene->GetCameraSystem();
			Frustum::SetCameraMatrices(camera->GetViewMatrix(), camera->GetProjectionMatrix());
			frustum = &Frustum::Get();
		}

		// === Populate instance data and group by mesh ===
		for (auto entity : view)
		{
			const auto& transform = view.get<Transform>(entity);
			const auto& mat = view.get<Material>(entity).data;
			const auto& mesh = mat->mesh;

			// CPU culling check
			if (frustum != nullptr)
			{
				const glm::vec3& min = mesh->meshBufferData->aabbMin;
				const glm::vec3& max = mesh->meshBufferData->aabbMax;

				if (!frustum->IsVisibleLazy(min, max, transform.GetModelMatrix()))
				{
					continue;
				}

				/* the is visible lazy method is actually on par and simpler than using a cached frustum component (for now)
				if (!registry.all_of<FrustumCullCache>(entity))
				{
					continue; // Skip if missing cache — optional debug log
				}

				auto& cache = registry.get<FrustumCullCache>(entity);
				cache.Update(min, max, transform.GetModelMatrix());

				if (!frustum->IsVisibleCached(cache))
				{
					continue; // Skip this instance
				}
				*/
			}

			// Build the GPU-side instance data struct
			GpuInstanceData instance{};
			instance.model = transform.GetModelMatrix();
			instance.aabbMin = glm::vec4(mesh->meshBufferData->aabbMin, 0.0f);
			instance.aabbMax = glm::vec4(mesh->meshBufferData->aabbMax, 0.0f);
			instance.textureIndex = mat->albedoMap ? mat->albedoMap->GetBindlessIndex() : UINT32_MAX;
			instance.hasTexture = mat->albedoMap ? 1.0f : 0.0f;
			instance.meshIndex = mesh->meshBufferData->GetMeshID();
			instance.pad = 0u;

			cpuInstanceData.push_back(instance);

			auto& range = rangeMap[mesh];
			if (range.count == 0)
			{
				range.firstInstance = instanceIndex;
			}
			range.count++;
			instanceIndex++;
		}

		// === Flatten into linear draw batch for fast draw ===
		instanceBatches.reserve(rangeMap.size());
		for (auto& [mesh, range] : rangeMap)
		{
			instanceBatches.emplace_back(mesh, range);
		}

		// === Upload per-instance data to GPU for rendering ===
		void* dst = instanceBuffer->BeginFrame(frameIndex);
		memcpy(dst, cpuInstanceData.data(), sizeof(GpuInstanceData) * cpuInstanceData.size());

		// Indirect drawing stuff now
		if (useIndirectDrawing)
		{
			std::vector<MeshIndirectDrawBatch>& frameBatches = drawBatchesPerFrame[frameIndex];
			frameBatches.reserve(instanceBatches.size());

			for (const auto& [mesh, range] : instanceBatches)
			{
				auto& meshData = GetOrCreateMeshBuffers(mesh);

				VkDrawIndexedIndirectCommand cmd{};
				cmd.indexCount = meshData.indexCount;
				cmd.instanceCount = range.count;
				cmd.firstIndex = 0;
				cmd.vertexOffset = 0;
				cmd.firstInstance = range.firstInstance;

				// Each mesh gets its own indirect batch
				MeshIndirectDrawBatch batch{};
				batch.mesh = mesh;
				batch.commands.push_back(cmd);

				frameBatches.push_back(std::move(batch));
			}

			// Flatten all commands to the buffer for safety/debug (optional if not used directly)
			std::vector<VkDrawIndexedIndirectCommand> allCommands;
			for (const auto& batch : frameBatches)
			{
				allCommands.insert(allCommands.end(), batch.commands.begin(), batch.commands.end());
			}

			VulkanBuffer& indirectBuf = *indirectCommandBuffers[frameIndex];
			indirectBuf.CopyData(allCommands.data(), allCommands.size() * sizeof(VkDrawIndexedIndirectCommand));
		}

		// Below is probably way outdated by now since we are now doing indirect indexed drawing, 
		// which in that case if we were doing GPU compute culling we would have to write to an SSBO for the commands to use.
		// Once we get GPU culling we would have to refactor this now very messy class entirely.

		// === Upload instance count for compute shader (only used for GPU culling) ===
		if (cullMode != CullMode::GPU)
		{
			return;
		}

		InstanceMeta meta{};
		meta.instanceCount = static_cast<uint32_t>(cpuInstanceData.size());

		void* mappedPtr = instanceMetaBuffer->GetMappedPointer();
		if (mappedPtr == nullptr)
		{
			throw std::runtime_error("Instance meta buffer is not mapped.");
		}

		memcpy(mappedPtr, &meta, sizeof(InstanceMeta));
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
		if (cullMode == CullMode::GPU)
		{
			DrawCulledIndexed(frameIndex, cmd);
			return;
		}

		if (useIndirectDrawing)
		{
			VkBuffer instanceBuf = instanceBuffer->GetBuffer(frameIndex);
			VkBuffer indirectBuf = indirectCommandBuffers[frameIndex]->GetBuffer();
			VkDeviceSize offsets[] = { 0, 0 };

			const auto& frameBatches = drawBatchesPerFrame[frameIndex];
			uint32_t offset = 0;

			for (const auto& batch : frameBatches)
			{
				auto& meshData = GetOrCreateMeshBuffers(batch.mesh);

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
				auto& meshData = GetOrCreateMeshBuffers(mesh);

				VkBuffer vertexBuffers[] = { meshData.vertexBuffer->GetBuffer(), instanceBuf };

				vkCmdBindVertexBuffers(cmd, 0, 2, vertexBuffers, offsets);
				vkCmdBindIndexBuffer(cmd, meshData.indexBuffer->GetBuffer(), 0, VK_INDEX_TYPE_UINT16);
				vkCmdDrawIndexed(cmd, meshData.indexCount, range.count, 0, 0, range.firstInstance);
			}
		}
	}

	// TODO: after fixing gpu culling make this use indirect as well
	void VulkanIndexDraw::DrawCulledIndexed(uint32_t /*frameIndex*/, VkCommandBuffer cmd)
	{
		// Early out if there's nothing to draw
		if (instanceCountCulled == 0)
		{
			return;
		}

		// This buffer holds interleaved instance metadata written by the compute shader
		VkBuffer instanceDataGPU = visibleDataBuffer->GetBuffer();

		// Vertex buffers: first is mesh vertex buffer (dynamic), second is instance data
		VkBuffer vertexBuffers[] = { nullptr, instanceDataGPU };
		VkDeviceSize offsets[] = { 0, 0 };

		// === Map draw calls by mesh ID ===
		auto& meshPool = MeshPool::GetInstance();

		// Group visible instances by meshID
		std::unordered_map<uint32_t, std::vector<uint32_t>> meshToInstances;

		for (uint32_t i = 0; i < instanceCountCulled; ++i)
		{
			const glm::uvec4& data = culledVisibleData[i]; // textureIndex, hasTexture, padA, meshID
			uint32_t meshID = data.w; // stored in padB (renamed to meshID)

			meshToInstances[meshID].push_back(i); // group by mesh
		}

		// For each mesh, draw all its instances
		for (const auto& [meshID, instanceIndices] : meshToInstances)
		{
			// Lookup mesh from ID
			auto mesh = meshPool.GetMeshByID(meshID);
			if (!mesh || !mesh->meshBufferData)
			{
				continue; // skip if mesh is missing
			}

			auto& meshData = *mesh->meshBufferData;

			// Bind this mesh's vertex + instance buffer
			vertexBuffers[0] = meshData.vertexBuffer->GetBuffer();
			vkCmdBindVertexBuffers(cmd, 0, 2, vertexBuffers, offsets);

			// Bind index buffer
			vkCmdBindIndexBuffer(cmd, meshData.indexBuffer->GetBuffer(), 0, VK_INDEX_TYPE_UINT16);

			// Draw each instance individually (we'll replace this with indirect drawing later)
			for (uint32_t instanceOffset : instanceIndices)
			{
				vkCmdDrawIndexed(cmd, meshData.indexCount, 1, 0, 0, instanceOffset);
			}
		}
	}

	void VulkanIndexDraw::ReadbackCulledInstanceData()
	{
		if (cullMode != CullMode::GPU) return;

		// === Read draw count ===
		const uint32_t* drawCountPtr = static_cast<const uint32_t*>(drawCountBuffer->GetMappedPointer());
		if (drawCountPtr == nullptr)
		{
			throw std::runtime_error("Draw count buffer is not mapped.");
		}
		instanceCountCulled = *drawCountPtr;

		if (instanceCountCulled == 0)
		{
			return;
		}

		// === Read visibleDataBuffer — each entry is a uvec4 (16 bytes) ===
		const glm::uvec4* rawData = static_cast<const glm::uvec4*>(visibleDataBuffer->GetMappedPointer());
		if (rawData == nullptr)
		{
			throw std::runtime_error("Visible data buffer is not mapped.");
		}

		culledVisibleData.resize(instanceCountCulled);
		memcpy(culledVisibleData.data(), rawData, instanceCountCulled * sizeof(glm::uvec4));
	}

	// This is unused because a naive vollatile write to this on a gpu buffer requires way more fencing and is not worth it for something lazy like this, better to use barriers
	void VulkanIndexDraw::ZeroDrawCount()
	{
		uint32_t* ptr = static_cast<uint32_t*>(drawCountBuffer->GetMappedPointer());
		if (!ptr)
		{
			throw std::runtime_error("drawCountBuffer not mapped");
		}
		*ptr = 0;
	}

	void VulkanIndexDraw::CleanUp()
	{
		if (instanceBuffer)
		{
			instanceBuffer->Cleanup();
			instanceBuffer.reset();
		}

		if (visibleModelBuffer) { visibleModelBuffer->Free(); visibleModelBuffer.reset(); }
		if (visibleDataBuffer) { visibleDataBuffer->Free(); visibleDataBuffer.reset(); }
		if (drawCountBuffer) { drawCountBuffer->Free();   drawCountBuffer.reset(); }
		if (instanceMetaBuffer) { instanceMetaBuffer->Free(); instanceMetaBuffer.reset(); }

		cpuInstanceData.clear();
		instanceBatches.clear();
		culledVisibleData.clear();
	}

}
