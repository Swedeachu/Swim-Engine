#include "PCH.h"
#include "VulkanIndexDraw.h"
#include "Engine/Components/Material.h"
#include "Engine/Components/CompositeMaterial.h"
#include "Engine/Components/Transform.h"
#include "Engine/Components/MeshDecorator.h"
#include "Engine/Components/TextComponent.h"
#include "Engine/Components/Internal/FrustumCullCache.h"
#include "Engine/Systems/Renderer/Core/Meshes/MeshPool.h"
#include "Engine/Systems/Renderer/Core/Camera/Frustum.h"
#include "Engine/Systems/Renderer/Core/Font/TextLayout.h"
#include "Engine/Utility/ParallelUtils.h"
#include "VulkanRenderer.h"

namespace Engine
{

	// NOTE: all these draw methods check if the draw instances fit in the ssbo, if not it tries to resize them.
	// The current execution flow will crash the program on trying to resize since we are reallocating device bound ssbos.
	// We would need a decently complex system to check if we should reallocate sizes before recording draw commands.
	// This is honestly stupid and not worth it. The fix is just allocate large ssbos once to avoid this problem entirely.
	// However I have kept these methods in since we might want to resize ssbos at other times of program execution,
	// as seen in VulkanDescriptorManager and the buffer classes. I also left in the method calls so exceptions can be hit
	// to let us know if we are causing problems instead of just letting the renderer screw up visually.

	VulkanIndexDraw::VulkanIndexDraw
	(
		VkDevice device,
		VkPhysicalDevice physicalDevice,
		const int MAX_EXPECTED_INSTANCES,
		const int MAX_FRAMES_IN_FLIGHT
	)
		: device(device), physicalDevice(physicalDevice)
	{
		uploadedWorldPacketVersions.resize(MAX_FRAMES_IN_FLIGHT, 0);
		uploadedFullScenePacketVersions.resize(MAX_FRAMES_IN_FLIGHT, 0);
		uploadedFullSceneCommandVersions.resize(MAX_FRAMES_IN_FLIGHT, 0);
		instanceBuffer = std::make_unique<Engine::VulkanInstanceBuffer>(
			device,
			physicalDevice,
			sizeof(GpuInstanceData),
			MAX_EXPECTED_INSTANCES,
			MAX_FRAMES_IN_FLIGHT
		);

		cpuInstanceData.reserve(MAX_EXPECTED_INSTANCES);
	}

	static void EnsureInstanceCapacity(VulkanInstanceBuffer& ib, size_t requiredInstances)
	{
		if (requiredInstances > ib.GetMaxInstances())
		{
			std::cout << "EnsureInstanceCapacity | Need to recreate buffer" << std::endl;
			// Grow (next pow2 to reduce churn):
			size_t newCap = 1;
			while (newCap < requiredInstances)
			{
				newCap <<= 1;
			}
			ib.Recreate(newCap);
		}
	}

	// Ensures an indirect buffer has enough room for 'commandCount' VkDrawIndexedIndirectCommand entries
	void VulkanIndexDraw::EnsureIndirectCapacity
	(
		std::unique_ptr<VulkanBuffer>& buf,
		size_t commandCount
	)
	{
		const VkDeviceSize need = static_cast<VkDeviceSize>(commandCount * sizeof(VkDrawIndexedIndirectCommand));
		if (buf->GetSize() < need)
		{
			std::cout << "EnsureIndirectCapacity | Need to grow buffer" << std::endl;
			const VkDeviceSize newSize = std::max<VkDeviceSize>(need, buf->GetSize() * 2);

			auto newBuf = std::make_unique<VulkanBuffer>(
				device,
				physicalDevice,
				newSize,
				VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
			);

			buf->Free();
			buf = std::move(newBuf);
		}
	}

	void VulkanIndexDraw::CreateIndirectBuffers(uint32_t maxDrawCalls, uint32_t framesInFlight)
	{
		indirectCommandBuffers.resize(framesInFlight);
		meshDecoratorIndirectCommandBuffers.resize(framesInFlight);
		msdfIndirectCommandBuffers.resize(framesInFlight);

		for (uint32_t i = 0; i < framesInFlight; ++i)
		{
			indirectCommandBuffers[i] = std::make_unique<VulkanBuffer>(
				device,
				physicalDevice,
				sizeof(VkDrawIndexedIndirectCommand) * maxDrawCalls,
				VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
			);

			meshDecoratorIndirectCommandBuffers[i] = std::make_unique<VulkanBuffer>(
				device,
				physicalDevice,
				sizeof(VkDrawIndexedIndirectCommand) * maxDrawCalls,
				VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
			);

			msdfIndirectCommandBuffers[i] = std::make_unique<VulkanBuffer>(
				device, physicalDevice,
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
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
		);

		// === Create mega index buffer ===
		megaIndexBuffer = std::make_unique<VulkanBuffer>(
			device,
			physicalDevice,
			totalIndexBufferSize,
			VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
		);
	}

	void VulkanIndexDraw::EnsureGlyphQuadUploaded()
	{
		if (hasUploadedGlyphQuad)
		{
			return;
		}

		// Simple unit quad with pos/uv (0..1). We add both windings so it faces both ways.
		std::vector<Vertex> verts(4);
		verts[0].position = { 0.0f, 0.0f, 0.0f }; verts[0].uv = { 0.0f, 0.0f };
		verts[1].position = { 1.0f, 0.0f, 0.0f }; verts[1].uv = { 1.0f, 0.0f };
		verts[2].position = { 1.0f, 1.0f, 0.0f }; verts[2].uv = { 1.0f, 1.0f };
		verts[3].position = { 0.0f, 1.0f, 0.0f }; verts[3].uv = { 0.0f, 1.0f };

		// CCW front faces + CW back faces (same UVs)
		std::vector<uint32_t> idx = {
			// Front (CCW)
			0, 1, 2,
			2, 3, 0,
			// Back (CW = reverse winding)
			2, 1, 0,
			0, 3, 2
		};

		// Allocate space inside mega buffers and fill MeshBufferData
		std::shared_ptr<Mesh> m = MeshPool::GetInstance().RegisterMesh("glyph", verts, idx);
		glyphQuadMesh = *m->meshBufferData.get();
		hasUploadedGlyphQuad = true;
	}

	// This makes 2 temporary index and vertex buffers and then copies them into our mega buffers. Also supports auto growth. This also sets the offsets in mesh data.
	void VulkanIndexDraw::UploadMeshToMegaBuffer(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices, MeshBufferData& meshData)
	{
		VkDeviceSize vertexSize = vertices.size() * sizeof(Vertex);
		VkDeviceSize indexSize = indices.size() * sizeof(uint32_t);

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

	bool VulkanIndexDraw::CanReuseCachedWorldPacket(const Scene& scene, const Frustum* frustum) const
	{
		if (!cachedWorldPacketState.valid)
		{
			return false;
		}

		if (cachedWorldPacketState.scene != &scene)
		{
			return false;
		}

		if (cachedWorldPacketState.cullMode != cullMode)
		{
			return false;
		}

		if (cachedWorldPacketState.usedSceneBVH != useQueriedFrustumSceneBVH)
		{
			return false;
		}

		if (cachedWorldPacketState.renderablesRevision != scene.GetRenderablesRevision())
		{
			return false;
		}

		if (Transform::AreAnyTransformsDirty()) // this is pretty much always going to be true
		{
			return false;
		}

		const uint64_t frustumRevision = frustum ? Frustum::GetRevision() : 0;
		if (cachedWorldPacketState.frustumRevision != frustumRevision)
		{
			return false;
		}

		return true;
	}

	void VulkanIndexDraw::UploadCachedWorldPacketToFrame(uint32_t frameIndex)
	{
		if (frameIndex < uploadedWorldPacketVersions.size()
			&& uploadedWorldPacketVersions[frameIndex] == cachedWorldPacketState.packetVersion)
		{
			return;
		}

		EnsureInstanceCapacity(*instanceBuffer, cachedWorldInstanceCount);

		if (cachedWorldInstanceCount > 0)
		{
			void* dst = instanceBuffer->BeginFrame(frameIndex);
			memcpy(dst, cpuInstanceData.data(), sizeof(GpuInstanceData) * cachedWorldInstanceCount);
		}

		EnsureIndirectCapacity(indirectCommandBuffers[frameIndex], worldDrawCommands.size());

		if (!worldDrawCommands.empty())
		{
			VulkanBuffer& indirectBuf = *indirectCommandBuffers[frameIndex];
			indirectBuf.CopyData(worldDrawCommands.data(), worldDrawCommands.size() * sizeof(VkDrawIndexedIndirectCommand));
		}

		if (frameIndex < uploadedWorldPacketVersions.size())
		{
			uploadedWorldPacketVersions[frameIndex] = cachedWorldPacketState.packetVersion;
		}
	}

	bool VulkanIndexDraw::CanUseFullScenePacket(const Scene& scene, const Frustum* frustum) const
	{
		if (frustum == nullptr)
		{
			return false;
		}

		if (cullMode != CullMode::CPU || !useQueriedFrustumSceneBVH)
		{
			return false;
		}

		SceneBVH* sceneBVH = scene.GetSceneBVH();
		if (sceneBVH == nullptr)
		{
			return false;
		}

		return sceneBVH->IsFullyVisible(*frustum);
	}



	bool VulkanIndexDraw::TryBuildGatheredInstance(entt::registry& registry, const VulkanIndexDraw::GatherCandidate& candidate, const Frustum* frustum, VulkanIndexDraw::GatheredInstance& outInstance) const
	{
		const std::shared_ptr<MaterialData>& mat = candidate.material;
		if (!mat || !mat->mesh || !mat->mesh->meshBufferData)
		{
			return false;
		}

		const MeshBufferData& mesh = *mat->mesh->meshBufferData;
		const glm::vec3 localMin = glm::vec3(mesh.aabbMin);
		const glm::vec3 localMax = glm::vec3(mesh.aabbMax);

		if (frustum && candidate.transformSpace == TransformSpace::World)
		{
			if (candidate.canUseEntityCullCache)
			{
				auto& cache = registry.get<FrustumCullCache>(candidate.entity);
				if (!frustum->IsVisibleCached(cache, localMin, localMax, candidate.worldMatrix, candidate.worldVersion))
				{
					return false;
				}
			}
			else if (!frustum->IsVisibleLazy(mesh.aabbMin, mesh.aabbMax, candidate.worldMatrix))
			{
				return false;
			}
		}

		outInstance.meshID = mesh.GetMeshID();
		outInstance.indexCount = mesh.indexCount;
		outInstance.vertexOffsetInMegaBuffer = mesh.vertexOffsetInMegaBuffer;
		outInstance.indexOffsetInMegaBuffer = mesh.indexOffsetInMegaBuffer;
		outInstance.instance.space = static_cast<uint32_t>(candidate.transformSpace);
		outInstance.instance.model = candidate.worldMatrix;
		outInstance.instance.textureIndex = mat->albedoMap ? mat->albedoMap->GetBindlessIndex() : UINT32_MAX;
		outInstance.instance.hasTexture = mat->albedoMap ? 1.0f : 0.0f;
		outInstance.instance.materialIndex = 0u;
		return true;
	}

	void VulkanIndexDraw::AppendGatheredInstance(const VulkanIndexDraw::GatheredInstance& gathered)
	{
		auto [it, inserted] = meshBuckets.try_emplace(gathered.meshID);
		MeshBucket& bucket = it->second;
		if (inserted)
		{
			bucket.indexCount = gathered.indexCount;
			bucket.indexOffsetInMegaBuffer = gathered.indexOffsetInMegaBuffer;
			bucket.vertexOffsetInMegaBuffer = gathered.vertexOffsetInMegaBuffer;
		}

		if (bucket.instances.empty())
		{
			activeMeshBucketKeys.push_back(gathered.meshID);
		}

		bucket.instances.push_back(gathered.instance);
	}

	void VulkanIndexDraw::AppendGatheredInstances(const std::vector<VulkanIndexDraw::GatheredInstance>& gatheredInstances)
	{
		for (const GatheredInstance& gathered : gatheredInstances)
		{
			AppendGatheredInstance(gathered);
		}
	}

	void VulkanIndexDraw::EnsureGatherThreadScratch(size_t workerSlots, size_t reservePerSlot)
	{
		if (gatherThreadScratch.size() < workerSlots)
		{
			gatherThreadScratch.resize(workerSlots);
		}

		for (size_t slot = 0; slot < workerSlots; ++slot)
		{
			auto& gathered = gatherThreadScratch[slot].gathered;
			gathered.clear();
			if (gathered.capacity() < reservePerSlot)
			{
				gathered.reserve(reservePerSlot);
			}
		}
	}

	void VulkanIndexDraw::EnsureDirtyThreadScratch(size_t workerSlots, size_t reservePerSlot)
	{
		if (dirtyThreadScratch.size() < workerSlots)
		{
			dirtyThreadScratch.resize(workerSlots);
		}

		for (size_t slot = 0; slot < workerSlots; ++slot)
		{
			auto& dirty = dirtyThreadScratch[slot].dirtyIndices;
			dirty.clear();
			if (dirty.capacity() < reservePerSlot)
			{
				dirty.reserve(reservePerSlot);
			}
		}
	}

	void VulkanIndexDraw::ProcessGatherCandidates(entt::registry& registry, const std::vector<VulkanIndexDraw::GatherCandidate>& candidates, const Frustum* frustum)
	{
		if (candidates.empty())
		{
			return;
		}

		const bool allowParallel = (frustum == nullptr)
			&& RenderCpuJobConfig::Enabled
			&& candidates.size() >= RenderCpuJobConfig::MinParallelItemCount
			&& GetRenderParallelWorkerSlots() > 1;

		if (!allowParallel)
		{
			for (const GatherCandidate& candidate : candidates)
			{
				GatheredInstance gathered{};
				if (TryBuildGatheredInstance(registry, candidate, frustum, gathered))
				{
					AppendGatheredInstance(gathered);
				}
			}
			return;
		}

		const size_t workerSlots = GetRenderParallelWorkerSlots();
		const size_t reservePerSlot = std::max<size_t>((candidates.size() + workerSlots - 1) / workerSlots, 32);
		EnsureGatherThreadScratch(workerSlots, reservePerSlot);

		ParallelForRender(candidates.size(), RenderCpuJobConfig::DefaultMinItemsPerChunk, [&](size_t begin, size_t end, uint32_t workerIndex)
		{
			auto& localGathered = gatherThreadScratch[workerIndex].gathered;
			for (size_t i = begin; i < end; ++i)
			{
				GatheredInstance gathered{};
				if (TryBuildGatheredInstance(registry, candidates[i], nullptr, gathered))
				{
					localGathered.push_back(std::move(gathered));
				}
			}
		});

		for (size_t slot = 0; slot < workerSlots; ++slot)
		{
			AppendGatheredInstances(gatherThreadScratch[slot].gathered);
		}
	}

	void VulkanIndexDraw::BuildFullScenePacket(Scene& scene)
	{
		entt::registry& registry = scene.GetRegistry();

		fullSceneRenderables.clear();
		fullSceneDrawCommands.clear();
		fullSceneEntityToInstanceIndices.clear();
		fullSceneDirtyHistory.clear();

		auto regularView = registry.view<Transform, Material>();
		fullSceneRenderables.reserve(static_cast<size_t>(regularView.size_hint()));
		for (auto entity : regularView)
		{
			if (!scene.ShouldRenderBasedOnState(entity) || registry.any_of<MeshDecorator>(entity))
			{
				continue;
			}

			const Transform& tf = regularView.get<Transform>(entity);
			if (tf.GetTransformSpace() != TransformSpace::World)
			{
				continue;
			}

			const std::shared_ptr<MaterialData>& mat = regularView.get<Material>(entity).data;
			if (!mat || !mat->mesh || !mat->mesh->meshBufferData)
			{
				continue;
			}

			FullSceneRenderable item{};
			item.entity = entity;
			item.material = mat;
			item.baseInstance.space = static_cast<uint32_t>(TransformSpace::World);
			item.baseInstance.model = tf.GetWorldMatrix(registry);
			item.baseInstance.textureIndex = mat->albedoMap ? mat->albedoMap->GetBindlessIndex() : UINT32_MAX;
			item.baseInstance.hasTexture = mat->albedoMap ? 1.0f : 0.0f;
			item.baseInstance.materialIndex = 0u;
			fullSceneRenderables.push_back(std::move(item));
		}

		auto compositeView = registry.view<Transform, CompositeMaterial>();
		for (auto entity : compositeView)
		{
			if (!scene.ShouldRenderBasedOnState(entity) || registry.any_of<MeshDecorator>(entity))
			{
				continue;
			}

			const Transform& tf = compositeView.get<Transform>(entity);
			if (tf.GetTransformSpace() != TransformSpace::World)
			{
				continue;
			}

			const glm::mat4& world = tf.GetWorldMatrix(registry);
			const CompositeMaterial& composite = compositeView.get<CompositeMaterial>(entity);
			for (const auto& mat : composite.subMaterials)
			{
				if (!mat || !mat->mesh || !mat->mesh->meshBufferData)
				{
					continue;
				}

				FullSceneRenderable item{};
				item.entity = entity;
				item.material = mat;
				item.baseInstance.space = static_cast<uint32_t>(TransformSpace::World);
				item.baseInstance.model = world;
				item.baseInstance.textureIndex = mat->albedoMap ? mat->albedoMap->GetBindlessIndex() : UINT32_MAX;
				item.baseInstance.hasTexture = mat->albedoMap ? 1.0f : 0.0f;
				item.baseInstance.materialIndex = 0u;
				fullSceneRenderables.push_back(std::move(item));
			}
		}

		std::sort(fullSceneRenderables.begin(), fullSceneRenderables.end(), [](const FullSceneRenderable& a, const FullSceneRenderable& b)
		{
			return a.material->mesh->meshBufferData->GetMeshID() < b.material->mesh->meshBufferData->GetMeshID();
		});

		fullSceneDrawCommands.reserve(fullSceneRenderables.size());
		cpuInstanceData.resize(fullSceneRenderables.size());

		if (!fullSceneRenderables.empty())
		{
			const bool allowParallelCopy = RenderCpuJobConfig::Enabled
				&& fullSceneRenderables.size() >= RenderCpuJobConfig::MinParallelItemCount
				&& GetRenderParallelWorkerSlots() > 1;

			if (allowParallelCopy)
			{
				ParallelForRender(fullSceneRenderables.size(), RenderCpuJobConfig::DefaultMinItemsPerChunk, [&](size_t begin, size_t end, uint32_t workerIndex)
				{
					for (size_t i = begin; i < end; ++i)
					{
						cpuInstanceData[i] = fullSceneRenderables[i].baseInstance;
					}
				});
			}
			else
			{
				for (size_t i = 0; i < fullSceneRenderables.size(); ++i)
				{
					cpuInstanceData[i] = fullSceneRenderables[i].baseInstance;
				}
			}
		}

		fullSceneEntityToInstanceIndices.reserve(fullSceneRenderables.size());
		for (size_t i = 0; i < fullSceneRenderables.size(); ++i)
		{
			const FullSceneRenderable& item = fullSceneRenderables[i];
			fullSceneEntityToInstanceIndices[item.entity].push_back(static_cast<uint32_t>(i));
		}

		uint32_t firstInstance = 0;
		size_t begin = 0;
		while (begin < fullSceneRenderables.size())
		{
			const MeshBufferData& mesh = *fullSceneRenderables[begin].material->mesh->meshBufferData;
			size_t end = begin + 1;
			while (end < fullSceneRenderables.size()
				&& fullSceneRenderables[end].material->mesh->meshBufferData->GetMeshID() == mesh.GetMeshID())
			{
				++end;
			}

			VkDrawIndexedIndirectCommand cmd{};
			cmd.indexCount = mesh.indexCount;
			cmd.instanceCount = static_cast<uint32_t>(end - begin);
			cmd.firstIndex = static_cast<uint32_t>(mesh.indexOffsetInMegaBuffer / sizeof(uint32_t));
			cmd.vertexOffset = static_cast<int32_t>(mesh.vertexOffsetInMegaBuffer / sizeof(Vertex));
			cmd.firstInstance = firstInstance;
			fullSceneDrawCommands.push_back(cmd);

			firstInstance += cmd.instanceCount;
			begin = end;
		}

		fullScenePacketScene = &scene;
		fullScenePacketRenderablesRevision = scene.GetRenderablesRevision();
		++fullScenePacketVersion;
		fullScenePacketValid = true;

		std::fill(uploadedFullScenePacketVersions.begin(), uploadedFullScenePacketVersions.end(), 0);
		std::fill(uploadedFullSceneCommandVersions.begin(), uploadedFullSceneCommandVersions.end(), 0);
	}

	bool VulkanIndexDraw::UpdateFullScenePacketDirtyEntities(Scene& scene)
	{
		const std::vector<entt::entity>& dirtyEntities = Transform::GetDirtyEntities();
		if (dirtyEntities.empty())
		{
			return false;
		}

		struct DirtyUpdateWork
		{
			glm::mat4 worldMatrix{ 1.0f };
			const std::vector<uint32_t>* indices = nullptr;
		};

		entt::registry& registry = scene.GetRegistry();
		std::vector<DirtyUpdateWork> dirtyWork;
		dirtyWork.reserve(dirtyEntities.size());

		for (entt::entity entity : dirtyEntities)
		{
			auto it = fullSceneEntityToInstanceIndices.find(entity);
			if (it == fullSceneEntityToInstanceIndices.end())
			{
				continue;
			}

			if (!registry.valid(entity) || !registry.any_of<Transform>(entity))
			{
				continue;
			}

			const Transform& tf = registry.get<Transform>(entity);
			DirtyUpdateWork work{};
			work.worldMatrix = tf.GetWorldMatrix(registry);
			work.indices = &it->second;
			dirtyWork.push_back(std::move(work));
		}

		if (dirtyWork.empty())
		{
			return false;
		}

		dirtyInstanceIndexScratch.clear();
		dirtyInstanceIndexScratch.reserve(dirtyWork.size() * 2);

		const bool allowParallelUpdate = RenderCpuJobConfig::Enabled
			&& dirtyWork.size() >= 32
			&& GetRenderParallelWorkerSlots() > 1;

		if (allowParallelUpdate)
		{
			const size_t workerSlots = GetRenderParallelWorkerSlots();
			EnsureDirtyThreadScratch(workerSlots, std::max<size_t>((dirtyWork.size() * 2 + workerSlots - 1) / workerSlots, 16));

			ParallelForRender(dirtyWork.size(), 32, [&](size_t begin, size_t end, uint32_t workerIndex)
			{
				auto& localDirty = dirtyThreadScratch[workerIndex].dirtyIndices;
				for (size_t i = begin; i < end; ++i)
				{
					const DirtyUpdateWork& work = dirtyWork[i];
					if (work.indices == nullptr)
					{
						continue;
					}

					for (uint32_t index : *work.indices)
					{
						cpuInstanceData[index].model = work.worldMatrix;
						localDirty.push_back(index);
					}
				}
			});

			for (size_t slot = 0; slot < workerSlots; ++slot)
			{
				auto& localDirty = dirtyThreadScratch[slot].dirtyIndices;
				dirtyInstanceIndexScratch.insert(dirtyInstanceIndexScratch.end(), localDirty.begin(), localDirty.end());
			}
		}
		else
		{
			for (const DirtyUpdateWork& work : dirtyWork)
			{
				if (work.indices == nullptr)
				{
					continue;
				}

				for (uint32_t index : *work.indices)
				{
					cpuInstanceData[index].model = work.worldMatrix;
					dirtyInstanceIndexScratch.push_back(index);
				}
			}
		}

		if (dirtyInstanceIndexScratch.empty())
		{
			return false;
		}

		std::sort(dirtyInstanceIndexScratch.begin(), dirtyInstanceIndexScratch.end());
		dirtyInstanceIndexScratch.erase(std::unique(dirtyInstanceIndexScratch.begin(), dirtyInstanceIndexScratch.end()), dirtyInstanceIndexScratch.end());

		FullSceneDirtyHistoryEntry entry{};
		++fullScenePacketVersion;
		entry.version = fullScenePacketVersion;

		uint32_t rangeStart = dirtyInstanceIndexScratch[0];
		uint32_t rangeCount = 1;
		for (size_t i = 1; i < dirtyInstanceIndexScratch.size(); ++i)
		{
			if (dirtyInstanceIndexScratch[i] == dirtyInstanceIndexScratch[i - 1] + 1)
			{
				++rangeCount;
			}
			else
			{
				entry.ranges.emplace_back(rangeStart, rangeCount);
				rangeStart = dirtyInstanceIndexScratch[i];
				rangeCount = 1;
			}
		}
		entry.ranges.emplace_back(rangeStart, rangeCount);

		fullSceneDirtyHistory.push_back(std::move(entry));
		while (fullSceneDirtyHistory.size() > 16)
		{
			fullSceneDirtyHistory.pop_front();
		}

		return true;
	}

	bool VulkanIndexDraw::PatchFullScenePacketFrame(uint32_t frameIndex)
	{
		if (frameIndex >= uploadedFullScenePacketVersions.size())
		{
			return false;
		}

		const uint64_t uploadedVersion = uploadedFullScenePacketVersions[frameIndex];
		if (uploadedVersion == 0 || uploadedVersion == fullScenePacketVersion)
		{
			return false;
		}

		if (fullSceneDirtyHistory.empty())
		{
			return false;
		}

		const uint64_t oldestTrackedVersion = fullSceneDirtyHistory.front().version;
		if (uploadedVersion + 1 < oldestTrackedVersion)
		{
			return false;
		}

		void* dst = instanceBuffer->BeginFrame(frameIndex);
		for (const FullSceneDirtyHistoryEntry& entry : fullSceneDirtyHistory)
		{
			if (entry.version <= uploadedVersion)
			{
				continue;
			}

			for (const auto& range : entry.ranges)
			{
				std::memcpy(
					static_cast<uint8_t*>(dst) + static_cast<size_t>(range.first) * sizeof(GpuInstanceData),
					cpuInstanceData.data() + range.first,
					static_cast<size_t>(range.second) * sizeof(GpuInstanceData)
				);
			}
		}

		uploadedFullScenePacketVersions[frameIndex] = fullScenePacketVersion;
		return true;
	}

	void VulkanIndexDraw::UploadFullScenePacket(uint32_t frameIndex, Scene& scene)
	{
		if (!fullScenePacketValid
			|| fullScenePacketScene != &scene
			|| fullScenePacketRenderablesRevision != scene.GetRenderablesRevision())
		{
			BuildFullScenePacket(scene);
		}
		else
		{
			UpdateFullScenePacketDirtyEntities(scene);
		}

		worldDrawCommands = fullSceneDrawCommands;
		cachedWorldInstanceCount = cpuInstanceData.size();

		EnsureInstanceCapacity(*instanceBuffer, cpuInstanceData.size());
		if (!cpuInstanceData.empty())
		{
			if (!PatchFullScenePacketFrame(frameIndex))
			{
				void* dst = instanceBuffer->BeginFrame(frameIndex);
				memcpy(dst, cpuInstanceData.data(), sizeof(GpuInstanceData) * cpuInstanceData.size());
				uploadedFullScenePacketVersions[frameIndex] = fullScenePacketVersion;
			}
		}

		EnsureIndirectCapacity(indirectCommandBuffers[frameIndex], worldDrawCommands.size());
		if (!worldDrawCommands.empty()
			&& frameIndex < uploadedFullSceneCommandVersions.size()
			&& uploadedFullSceneCommandVersions[frameIndex] != fullScenePacketRenderablesRevision)
		{
			VulkanBuffer& indirectBuf = *indirectCommandBuffers[frameIndex];
			indirectBuf.CopyData(worldDrawCommands.data(), worldDrawCommands.size() * sizeof(VkDrawIndexedIndirectCommand));
			uploadedFullSceneCommandVersions[frameIndex] = fullScenePacketRenderablesRevision;
		}
	}

	// This actually does CPU culling logic here if that mode is activated
	void VulkanIndexDraw::UpdateInstanceBuffer(uint32_t frameIndex)
	{
		meshDecoratorInstanceData.clear();
		msdfInstancesData.clear();

		const std::shared_ptr<Scene>& scene = SwimEngine::GetInstance()->GetSceneSystem()->GetActiveScene();
		entt::registry& registry = scene->GetRegistry();

		const Frustum* frustum = nullptr;
		if (cullMode == CullMode::CPU)
		{
			std::shared_ptr<CameraSystem> camera = scene->GetCameraSystem();
			Frustum::SetCameraMatrices(camera->GetViewMatrix(), camera->GetProjectionMatrix());
			frustum = &Frustum::Get();
		}

		if (CanReuseCachedWorldPacket(*scene, frustum))
		{
			cpuInstanceData.resize(cachedWorldInstanceCount);
			UploadCachedWorldPacketToFrame(frameIndex);
			return;
		}

		if (CanUseFullScenePacket(*scene, frustum))
		{
			UploadFullScenePacket(frameIndex, *scene);
			return;
		}

		cpuInstanceData.clear();

		for (uint32_t meshID : activeMeshBucketKeys)
		{
			meshBuckets[meshID].instances.clear();
		}
		activeMeshBucketKeys.clear();

		// CPU culling can leverage the scenes BVH spacial partitions
		if (cullMode == CullMode::CPU && frustum && useQueriedFrustumSceneBVH)
		{
			GatherCandidatesBVH(*scene, *frustum);
		}
		else
		{
			// Render everything in world space with regular frustum culling (this is still CPU culling just not using the BVH)
			GatherCandidatesView(registry, TransformSpace::World, frustum);
		}

		UploadAndBatchInstances(frameIndex);

		cachedWorldInstanceCount = cpuInstanceData.size();
		cachedWorldPacketState.scene = scene.get();
		cachedWorldPacketState.frustumRevision = frustum ? Frustum::GetRevision() : 0;
		cachedWorldPacketState.renderablesRevision = scene->GetRenderablesRevision();
		cachedWorldPacketState.cullMode = cullMode;
		cachedWorldPacketState.usedSceneBVH = useQueriedFrustumSceneBVH;
		cachedWorldPacketState.valid = true;
		++cachedWorldPacketState.packetVersion;

		if (frameIndex < uploadedWorldPacketVersions.size())
		{
			uploadedWorldPacketVersions[frameIndex] = cachedWorldPacketState.packetVersion;
		}
	}

	// Fires off the frustum query in the scene's bounding volume hierarchy and then builds world instances from the visible entities.
	// This only does world space entities as the BVH only takes world space objects into account.
	void VulkanIndexDraw::GatherCandidatesBVH(Scene& scene, const Frustum& frustum)
	{
		entt::registry& registry = scene.GetRegistry();
		visibleEntityScratch.clear();
		scene.GetSceneBVH()->QueryFrustumParallel(frustum, visibleEntityScratch);

		gatherCandidatesScratch.clear();
		gatherCandidatesScratch.reserve(visibleEntityScratch.size() * 2);
		for (entt::entity entity : visibleEntityScratch)
		{
			if (entity == entt::null || !registry.valid(entity) || !registry.any_of<Transform>(entity))
			{
				continue;
			}

			if (registry.any_of<MeshDecorator>(entity))
			{
				continue;
			}

			if (!scene.ShouldRenderBasedOnState(entity))
			{
				continue;
			}

			const Transform& tf = registry.get<Transform>(entity);
			const glm::mat4& world = tf.GetWorldMatrix(registry);

			if (registry.all_of<Material>(entity))
			{
				GatherCandidate candidate{};
				candidate.entity = entity;
				candidate.material = registry.get<Material>(entity).data;
				candidate.worldMatrix = world;
				candidate.worldVersion = tf.GetWorldVersion();
				candidate.transformSpace = tf.GetTransformSpace();
				candidate.canUseEntityCullCache = false;
				gatherCandidatesScratch.push_back(std::move(candidate));
			}
			else if (registry.all_of<CompositeMaterial>(entity))
			{
				const CompositeMaterial& composite = registry.get<CompositeMaterial>(entity);
				for (const std::shared_ptr<MaterialData>& mat : composite.subMaterials)
				{
					GatherCandidate candidate{};
					candidate.entity = entity;
					candidate.material = mat;
					candidate.worldMatrix = world;
					candidate.worldVersion = tf.GetWorldVersion();
					candidate.transformSpace = tf.GetTransformSpace();
					candidate.canUseEntityCullCache = false;
					gatherCandidatesScratch.push_back(std::move(candidate));
				}
			}
		}

		ProcessGatherCandidates(registry, gatherCandidatesScratch, nullptr);
	}

	// Passing space as TransformSpace::Ambiguous will just render all entities
	void VulkanIndexDraw::GatherCandidatesView(entt::registry& registry, const TransformSpace space, const Frustum* frustum)
	{
		auto& scene = SwimEngine::GetInstance()->GetSceneSystem()->GetActiveScene();
		gatherCandidatesScratch.clear();

		auto regularView = registry.view<Transform, Material>();
		gatherCandidatesScratch.reserve(static_cast<size_t>(regularView.size_hint()));
		for (auto entity : regularView)
		{
			if (registry.any_of<MeshDecorator>(entity))
			{
				continue;
			}

			if (!scene->ShouldRenderBasedOnState(entity))
			{
				continue;
			}

			const Transform& tf = regularView.get<Transform>(entity);
			if (space == TransformSpace::Ambiguous || tf.GetTransformSpace() == space)
			{
				GatherCandidate candidate{};
				candidate.entity = entity;
				candidate.material = regularView.get<Material>(entity).data;
				candidate.worldMatrix = tf.GetWorldMatrix(registry);
				candidate.worldVersion = tf.GetWorldVersion();
				candidate.transformSpace = tf.GetTransformSpace();
				candidate.canUseEntityCullCache = registry.any_of<FrustumCullCache>(entity) && !registry.any_of<CompositeMaterial>(entity);
				gatherCandidatesScratch.push_back(std::move(candidate));
			}
		}

		auto compositeView = registry.view<Transform, CompositeMaterial>();
		for (auto entity : compositeView)
		{
			if (registry.any_of<MeshDecorator>(entity))
			{
				continue;
			}

			if (!scene->ShouldRenderBasedOnState(entity))
			{
				continue;
			}

			const Transform& tf = compositeView.get<Transform>(entity);
			if (space == TransformSpace::Ambiguous || tf.GetTransformSpace() == space)
			{
				const glm::mat4& world = tf.GetWorldMatrix(registry);
				const CompositeMaterial& composite = compositeView.get<CompositeMaterial>(entity);
				for (const auto& mat : composite.subMaterials)
				{
					GatherCandidate candidate{};
					candidate.entity = entity;
					candidate.material = mat;
					candidate.worldMatrix = world;
					candidate.worldVersion = tf.GetWorldVersion();
					candidate.transformSpace = tf.GetTransformSpace();
					candidate.canUseEntityCullCache = false;
					gatherCandidatesScratch.push_back(std::move(candidate));
				}
			}
		}

		ProcessGatherCandidates(registry, gatherCandidatesScratch, frustum);
	}

	void VulkanIndexDraw::AddInstance(entt::registry& registry, entt::entity entity, const Transform& transform, const std::shared_ptr<MaterialData>& mat, const Frustum* frustum)
	{
		GatherCandidate candidate{};
		candidate.entity = entity;
		candidate.material = mat;
		candidate.worldMatrix = transform.GetWorldMatrix(registry);
		candidate.worldVersion = transform.GetWorldVersion();
		candidate.transformSpace = transform.GetTransformSpace();
		candidate.canUseEntityCullCache = registry.any_of<FrustumCullCache>(entity) && !registry.any_of<CompositeMaterial>(entity);

		GatheredInstance gathered{};
		if (TryBuildGatheredInstance(registry, candidate, frustum, gathered))
		{
			AppendGatheredInstance(gathered);
		}
	}

	void VulkanIndexDraw::UploadAndBatchInstances(uint32_t frameIndex)
	{
		worldDrawCommands.clear();
		cpuInstanceData.clear();

		std::sort(activeMeshBucketKeys.begin(), activeMeshBucketKeys.end());

		size_t totalWorldInstances = 0;
		for (uint32_t meshID : activeMeshBucketKeys)
		{
			totalWorldInstances += meshBuckets[meshID].instances.size();
		}

		cpuInstanceData.resize(totalWorldInstances);
		worldDrawCommands.reserve(activeMeshBucketKeys.size());
		bucketFirstInstanceScratch.resize(activeMeshBucketKeys.size(), 0);

		uint32_t firstInstance = 0;
		for (size_t i = 0; i < activeMeshBucketKeys.size(); ++i)
		{
			MeshBucket& bucket = meshBuckets[activeMeshBucketKeys[i]];
			if (bucket.instances.empty())
			{
				continue;
			}

			bucketFirstInstanceScratch[i] = firstInstance;

			VkDrawIndexedIndirectCommand cmd{};
			cmd.indexCount = bucket.indexCount;
			cmd.instanceCount = static_cast<uint32_t>(bucket.instances.size());
			cmd.firstIndex = static_cast<uint32_t>(bucket.indexOffsetInMegaBuffer / sizeof(uint32_t));
			cmd.vertexOffset = static_cast<int32_t>(bucket.vertexOffsetInMegaBuffer / sizeof(Vertex));
			cmd.firstInstance = firstInstance;
			worldDrawCommands.push_back(cmd);

			firstInstance += cmd.instanceCount;
		}

		const bool allowParallelCopy = RenderCpuJobConfig::Enabled
			&& totalWorldInstances >= RenderCpuJobConfig::MinParallelItemCount
			&& activeMeshBucketKeys.size() >= 4
			&& GetRenderParallelWorkerSlots() > 1;

		if (allowParallelCopy)
		{
			ParallelForRender(activeMeshBucketKeys.size(), 1, [&](size_t begin, size_t end, uint32_t workerIndex)
			{
				for (size_t i = begin; i < end; ++i)
				{
					const MeshBucket& bucket = meshBuckets[activeMeshBucketKeys[i]];
					if (bucket.instances.empty())
					{
						continue;
					}

					std::memcpy(
						cpuInstanceData.data() + bucketFirstInstanceScratch[i],
						bucket.instances.data(),
						bucket.instances.size() * sizeof(GpuInstanceData)
					);
				}
			});
		}
		else
		{
			for (size_t i = 0; i < activeMeshBucketKeys.size(); ++i)
			{
				const MeshBucket& bucket = meshBuckets[activeMeshBucketKeys[i]];
				if (bucket.instances.empty())
				{
					continue;
				}

				std::memcpy(
					cpuInstanceData.data() + bucketFirstInstanceScratch[i],
					bucket.instances.data(),
					bucket.instances.size() * sizeof(GpuInstanceData)
				);
			}
		}

		// Ensure the per-frame instance buffer can hold the world instances
		EnsureInstanceCapacity(*instanceBuffer, cpuInstanceData.size());

		if (!cpuInstanceData.empty())
		{
			void* dst = instanceBuffer->BeginFrame(frameIndex);
			memcpy(dst, cpuInstanceData.data(), sizeof(GpuInstanceData) * cpuInstanceData.size());
		}

		// Ensure indirect buffer capacity for world draws
		EnsureIndirectCapacity(indirectCommandBuffers[frameIndex], worldDrawCommands.size());

		if (!worldDrawCommands.empty())
		{
			VulkanBuffer& indirectBuf = *indirectCommandBuffers[frameIndex];
			indirectBuf.CopyData(worldDrawCommands.data(), worldDrawCommands.size() * sizeof(VkDrawIndexedIndirectCommand));
		}
	}

	// Draws everything in world space that isn't decorated or requiring custom shaders that was processed into the command buffers via UploadAndBatchInstances()
	void VulkanIndexDraw::DrawIndexedWorldMeshes(uint32_t frameIndex, VkCommandBuffer cmd)
	{
		VkBuffer indirectBuf = indirectCommandBuffers[frameIndex]->GetBuffer();
		VkDeviceSize offsets[] = { 0, 0 };

		// Use the mega mesh buffer to do the scene in one single draw call
		VkBuffer vertexBuffers[] = {
				megaVertexBuffer->GetBuffer(),      // Binding 0: packed mesh vertices
				instanceBuffer->GetBuffer(frameIndex) // Binding 1: per-instance data / legacy shader compatibility
		};

		vkCmdBindVertexBuffers(cmd, 0, 2, vertexBuffers, offsets);
		vkCmdBindIndexBuffer(cmd, megaIndexBuffer->GetBuffer(), 0, VK_INDEX_TYPE_UINT32);

		// UploadAndBatchInstances() already flattened all commands into indirect buffer, 
		// so we can call vkCmdDrawIndexedIndirect once over entire buffer.

		size_t totalCommands = worldDrawCommands.size();

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

	// Draws everything that is in screen space or has a decorator on it (including world space decorated meshes)
	// This will then also do immediate mode drawing from the debug registry if that is enabled.
	void VulkanIndexDraw::DrawIndexedScreenSpaceAndDecoratedMeshes(uint32_t frameIndex, VkCommandBuffer cmd)
	{
		std::shared_ptr<SwimEngine> engine = SwimEngine::GetInstance();

		unsigned int windowWidth = engine->GetWindowWidth();
		unsigned int windowHeight = engine->GetWindowHeight();

		const std::shared_ptr<Scene>& scene = engine->GetSceneSystem()->GetActiveScene();
		entt::registry& registry = scene->GetRegistry();

		std::shared_ptr<VulkanRenderer> renderer = engine->GetVulkanRenderer();
		const std::unique_ptr<VulkanPipelineManager>& pipelineManager = renderer->GetPipelineManager();
		const std::unique_ptr<VulkanDescriptorManager>& descriptorManager = renderer->GetDescriptorManager();

		const CameraUBO& cameraUBO = renderer->GetCameraUBO();
		const glm::mat4& worldView = engine->GetCameraSystem()->GetViewMatrix();

		const Frustum& frustum = Frustum::Get();

		// Gather instances info before we send anymore new draws since we need to add to the current buffer
		const uint32_t baseDecoratorInstanceID = static_cast<uint32_t>(cpuInstanceData.size());
		const size_t baseInstanceID = cpuInstanceData.size();
		uint32_t instanceCount = 0;

		std::vector<VkDrawIndexedIndirectCommand> drawCommands;

		DrawDecoratorsAndScreenSpaceEntitiesInRegistry(
			registry,
			cameraUBO,
			worldView,
			windowWidth,
			windowHeight,
			frustum,
			instanceCount,
			drawCommands,
			true // run culling
		);

		// Complete hack to inject in the decorator wireframe meshes because debug rendering uses the same pipeline.
		SceneDebugDraw* debugDraw = scene->GetSceneDebugDraw();
		if (debugDraw && debugDraw->IsEnabled())
		{
			DrawDecoratorsAndScreenSpaceEntitiesInRegistry(
				debugDraw->GetRegistry(),
				cameraUBO,
				worldView,
				windowWidth,
				windowHeight,
				frustum,
				instanceCount,
				drawCommands,
				false // no culling
			);
		}

		if (drawCommands.empty())
		{
			return;
		}

		// === Ensure SSBO capacity for MeshDecoratorGpuInstanceData ===
		const size_t decoBytes = meshDecoratorInstanceData.size() * sizeof(MeshDecoratorGpuInstanceData);
		// descriptorManager->EnsurePerFrameMeshDecoratorCapacity(decoBytes);

		// === Upload MeshDecoratorGpuInstanceData via descriptorManager ===
		descriptorManager->UpdatePerFrameMeshDecoratorBuffer(
			frameIndex,
			meshDecoratorInstanceData.data(),
			decoBytes
		);

		// === Ensure instance buffer can hold world + decorator instances ===
		// cpuInstanceData already has both world and newly appended decorator instance structs
		const size_t totalInstancesNeeded = cpuInstanceData.size();
		EnsureInstanceCapacity(*instanceBuffer, totalInstancesNeeded);

		// === Upload new instance data for section (append only the decorator range) ===
		void* dst = instanceBuffer->GetBufferRaw(frameIndex)->GetMappedPointer();

		// If we use aligned stride, replace sizeof(GpuInstanceData) with instanceBuffer->GetAlignedInstanceSize()
		std::memcpy(
			static_cast<uint8_t*>(dst) + baseInstanceID * sizeof(GpuInstanceData),
			cpuInstanceData.data() + baseInstanceID,
			meshDecoratorInstanceData.size() * sizeof(GpuInstanceData)
		);

		// === Ensure indirect buffer capacity for decorator draws ===
		EnsureIndirectCapacity(meshDecoratorIndirectCommandBuffers[frameIndex], drawCommands.size());

		// === Upload indirect draw commands ===
		meshDecoratorIndirectCommandBuffers[frameIndex]->CopyData(
			drawCommands.data(),
			drawCommands.size() * sizeof(VkDrawIndexedIndirectCommand)
		);

		// === Bind pipeline and descriptors ===
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineManager->GetDecoratorPipeline());

		std::array<VkDescriptorSet, 2> descriptorSets = {
				descriptorManager->GetPerFrameDescriptorSet(frameIndex),
				descriptorManager->GetBindlessSet()
		};

		vkCmdBindDescriptorSets(
			cmd,
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			pipelineManager->GetDecoratorPipelineLayout(),
			0,
			static_cast<uint32_t>(descriptorSets.size()),
			descriptorSets.data(),
			0,
			nullptr
		);

		// === Bind buffers ===
		VkBuffer vertexBuffers[] = {
				megaVertexBuffer->GetBuffer(),      // Binding 0: packed mesh vertices
				instanceBuffer->GetBuffer(frameIndex) // Binding 1: per-instance data / legacy shader compatibility
		};
		VkDeviceSize offsets[] = { 0, 0 };

		vkCmdBindVertexBuffers(cmd, 0, 2, vertexBuffers, offsets);
		vkCmdBindIndexBuffer(cmd, megaIndexBuffer->GetBuffer(), 0, VK_INDEX_TYPE_UINT32);

		// === Issue indirect draw ===
		vkCmdDrawIndexedIndirect(
			cmd,
			meshDecoratorIndirectCommandBuffers[frameIndex]->GetBuffer(),
			0,
			static_cast<uint32_t>(drawCommands.size()),
			sizeof(VkDrawIndexedIndirectCommand)
		);
	}

	void VulkanIndexDraw::DrawDecoratorsAndScreenSpaceEntitiesInRegistry
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
	)
	{
		// First cache screen scale
		glm::vec2 screenScale = glm::vec2(
			static_cast<float>(windowWidth) / Renderer::VirtualCanvasWidth,
			static_cast<float>(windowHeight) / Renderer::VirtualCanvasHeight
		);

		auto& scene = SwimEngine::GetInstance()->GetSceneSystem()->GetActiveScene();

		registry.view<Transform, Material>().each(
			[&](entt::entity entity, Transform& transform, Material& matComp)
		{
			bool hasDecorator = registry.any_of<MeshDecorator>(entity); // hopefully not too expensive
			TransformSpace space = transform.GetTransformSpace();

			// We can render UI in world space if it has a decorator on it.
			if (!hasDecorator && space != TransformSpace::Screen)
			{
				return;
			}

			// Skip what should not be rendered
			if (!scene->ShouldRenderBasedOnState(entity))
			{
				return;
			}

			const glm::vec3& pos = transform.GetPosition(); // In virtual canvas units
			const glm::vec3& scale = transform.GetScale();  // Width & height in virtual canvas units

			const std::shared_ptr<MaterialData>& mat = matComp.data;
			const MeshBufferData& mesh = *mat->mesh->meshBufferData;
			const glm::mat4& model = transform.GetWorldMatrix(registry);

			// First do a simple cull check 
			if (cull)
			{
				if (space == TransformSpace::World)
				{
					if (registry.any_of<FrustumCullCache>(entity))
					{
						auto& cache = registry.get<FrustumCullCache>(entity);
						if (!frustum.IsVisibleCached(cache, glm::vec3(matComp.data->mesh->meshBufferData->aabbMin), glm::vec3(matComp.data->mesh->meshBufferData->aabbMax), model, transform.GetWorldVersion()))
						{
							return;
						}
					}
					else if (!frustum.IsVisibleLazy(matComp.data->mesh->meshBufferData->aabbMin, matComp.data->mesh->meshBufferData->aabbMax, model))
					{
						return;
					}
				}
				else
				{
					// Sceen space 2D check using window width and height with respect to world matrix scale and position values

					// Extract translation (position) directly from the last column
					const glm::vec3 worldPos = transform.GetWorldPosition(registry);

					// Extract per-axis scale as lengths of the basis columns
					const glm::vec3 worldScale = transform.GetWorldScale(registry);

					glm::vec2 halfSize = glm::vec2(worldScale) * 0.5f;
					glm::vec2 center = glm::vec2(worldPos) * screenScale;
					glm::vec2 halfSizePx = halfSize * screenScale;

					glm::vec2 minPx = center - halfSizePx;
					glm::vec2 maxPx = center + halfSizePx;

					// Clamp against the actual framebuffer
					if (maxPx.x < 0.0f || maxPx.y < 0.0f)
					{
						return; // off left or bottom
					}

					if (minPx.x > windowWidth || minPx.y > windowHeight)
					{
						return; // off right or top
					}
				}
			}

			// === GpuInstanceData ===
			GpuInstanceData instance{};
			instance.model = model;
			instance.space = static_cast<uint32_t>(space);

			// The vertex shader then sets this on output.instanceID, which the fragment shader then uses for MeshDecoratorGpuInstanceData data = decoratorBuffer[paramIndex]
			instance.materialIndex = instanceCount;

			// === MeshDecoratorGpuInstanceData ===
			glm::vec2 pixelSize = glm::vec2(scale);
			glm::vec2 quadSizeInPixels;

			if (space == TransformSpace::Screen)
			{
				// In screen space, just scale pixels directly
				quadSizeInPixels = pixelSize * screenScale;
			}
			else
			{
				// In world space, convert world scale to screen-space pixel size using perspective

				// Transform center into view space
				glm::vec4 viewPos = worldView * glm::vec4(pos, 1.0f);
				float absZ = std::abs(viewPos.z);

				// Avoid division by zero near camera
				if (absZ < 0.0001f)
				{
					absZ = 0.0001f;
				}

				// camParams.x = tan(fovX * 0.5)
				// camParams.y = tan(fovY * 0.5)
				const float worldPerPixelX = (2.0f * absZ * cameraUBO.camParams.x) / static_cast<float>(windowWidth);
				const float worldPerPixelY = (2.0f * absZ * cameraUBO.camParams.y) / static_cast<float>(windowHeight);

				quadSizeInPixels = glm::vec2(
					scale.x / worldPerPixelX,
					scale.y / worldPerPixelY
				);
			}

			MeshDecoratorGpuInstanceData data{};

			if (hasDecorator)
			{
				MeshDecorator& deco = registry.get<MeshDecorator>(entity);

				bool useTex = deco.useMaterialTexture && mat->albedoMap;

				instance.hasTexture = useTex ? 1.0f : 0.0f;
				instance.textureIndex = useTex ? mat->albedoMap->GetBindlessIndex() : 0;

				glm::vec2 radiusPx;
				glm::vec2 strokePx;

				if (space == TransformSpace::Screen)
				{
					radiusPx = glm::min(deco.cornerRadius * screenScale, quadSizeInPixels * 0.5f);
					strokePx = glm::min(deco.strokeWidth * screenScale, quadSizeInPixels * 0.5f);
				}
				else
				{
					// Convert decorator values (specified in world units) to pixels so they scale with the quad
					glm::vec4 viewPos = worldView * glm::vec4(pos, 1.0f);
					float absZ2 = std::abs(viewPos.z);

					if (absZ2 < 0.0001f)
					{
						absZ2 = 0.0001f;
					}

					const float worldPerPixelX2 = (2.0f * absZ2 * cameraUBO.camParams.x) / static_cast<float>(windowWidth);
					const float worldPerPixelY2 = (2.0f * absZ2 * cameraUBO.camParams.y) / static_cast<float>(windowHeight);

					glm::vec2 scaler = { 250, 250 }; // BS number that just works well

					radiusPx = glm::min((deco.cornerRadius / scaler) / glm::vec2(worldPerPixelX2, worldPerPixelY2), quadSizeInPixels * 0.5f);
					strokePx = glm::min((deco.strokeWidth / scaler) / glm::vec2(worldPerPixelX2, worldPerPixelY2), quadSizeInPixels * 0.5f);
				}

				data.fillColor = deco.fillColor;
				data.strokeColor = deco.strokeColor;
				data.strokeWidth = strokePx;
				data.cornerRadius = radiusPx;
				data.enableFill = deco.enableFill ? 1 : 0;
				data.enableStroke = deco.enableStroke ? 1 : 0;
				data.roundCorners = deco.roundCorners ? 1 : 0;
				data.useTexture = useTex ? 1 : 0;
				data.renderOnTop = deco.renderOnTop;
			}
			else
			{
				// If we have no decorator to determine special texture handling, then set it the regular way.
				instance.hasTexture = mat->albedoMap ? 1.0f : 0.0f;
				instance.textureIndex = mat->albedoMap ? mat->albedoMap->GetBindlessIndex() : 0;

				// Meshes in screen space with no decorator need to be drawn with their meshes color, since fill color is a property of Decorator.
				// So we mark fill color as -1.0f as a flag to the shader to use mesh color sample instead.
				data.fillColor = glm::vec4(-1.0f);
				// Everything else is zero'd out or default
				data.strokeColor = glm::vec4(0.0f);
				data.strokeWidth = glm::vec2(0.0f);
				data.cornerRadius = glm::vec2(0.0f);
				data.enableFill = 1;
				data.enableStroke = 0;
				data.roundCorners = 0;
				data.useTexture = mat->albedoMap ? 1 : 0;
				data.renderOnTop = 0;
			}

			data.resolution = glm::vec2(windowWidth, windowHeight);
			data.quadSize = quadSizeInPixels;

			// Finally add
			meshDecoratorInstanceData.push_back(data);
			cpuInstanceData.push_back(instance);

			// === Draw command ===
			VkDrawIndexedIndirectCommand cmd{};
			cmd.indexCount = mesh.indexCount;
			cmd.instanceCount = 1;
			cmd.firstIndex = static_cast<uint32_t>(mesh.indexOffsetInMegaBuffer / sizeof(uint32_t));
			cmd.vertexOffset = static_cast<int32_t>(mesh.vertexOffsetInMegaBuffer / sizeof(Vertex));
			cmd.firstInstance = static_cast<uint32_t>(cpuInstanceData.size() - 1); // latest one
			drawCommands.push_back(cmd);

			instanceCount++;
		});
	}

	void VulkanIndexDraw::DrawIndexedMsdfText(uint32_t frameIndex, VkCommandBuffer cmd, TransformSpace space)
	{
		std::shared_ptr<SwimEngine> engine = SwimEngine::GetInstance();
		auto scene = engine->GetSceneSystem()->GetActiveScene();
		if (!scene) return;

		auto& registry = scene->GetRegistry();
		const CameraUBO& ubo = engine->GetVulkanRenderer()->GetCameraUBO();
		const glm::mat4& view = engine->GetCameraSystem()->GetViewMatrix();
		const glm::mat4& proj = engine->GetCameraSystem()->GetProjectionMatrix();

		unsigned int ww = engine->GetWindowWidth();
		unsigned int wh = engine->GetWindowHeight();

		BuildMsdfInstancesForSpace(registry, space, ubo, view, proj, ww, wh, msdfInstancesData);
		UploadAndDrawMsdfBatch(frameIndex, cmd, msdfInstancesData, msdfIndirectCommandBuffers[frameIndex]);
	}

	// If we are in world space we should probably cull our quad against the camera frustum
	void VulkanIndexDraw::BuildMsdfInstancesForSpace
	(
		entt::registry& registry,
		TransformSpace space,
		const CameraUBO& cameraUBO,
		const glm::mat4& view,
		const glm::mat4& proj,
		unsigned int windowWidth,
		unsigned int windowHeight,
		std::vector<MsdfTextGpuInstanceData>& outInstances
	)
	{
		auto& scene = SwimEngine::GetInstance()->GetSceneSystem()->GetActiveScene();

		registry.view<Transform, TextComponent>().each(
			[&](entt::entity entity, Transform& tf, TextComponent& tc)
		{
			if (tf.GetTransformSpace() != space) return;
			if (!tc.GetFont() || !tc.GetFont()->msdfAtlas) return;

			// Skip what should not be rendered
			if (!scene->ShouldRenderBasedOnState(entity))
			{
				return;
			}

			const FontInfo& fi = *tc.GetFont();
			const uint32_t atlasIndex = tc.GetFont()->msdfAtlas->GetBindlessIndex();

			MsdfTextGpuInstanceData s = (space == TransformSpace::Screen)
				? BuildMsdfStateScreen(registry, tf, tc, fi, windowWidth, windowHeight, Renderer::VirtualCanvasWidth, Renderer::VirtualCanvasHeight, atlasIndex)
				: BuildMsdfStateWorld(registry, tf, tc, fi, atlasIndex);

			EmitMsdf(tc, fi, s, [&](uint32_t /*lineIdx*/, const GlyphQuad& q, const MsdfTextGpuInstanceData& st)
			{
				MsdfTextGpuInstanceData inst = st;
				inst.plane = q.plane;
				inst.uvRect = q.uv;
				outInstances.push_back(inst);
			});
		});
	}

	void VulkanIndexDraw::UploadAndDrawMsdfBatch
	(
		uint32_t frameIndex,
		VkCommandBuffer cmd,
		const std::vector<MsdfTextGpuInstanceData>& instances,
		std::unique_ptr<VulkanBuffer>& outIndirectBuf
	)
	{
		if (instances.empty()) return;

		auto engine = SwimEngine::GetInstance();
		auto renderer = engine->GetVulkanRenderer();
		auto& pm = renderer->GetPipelineManager();
		auto& dm = renderer->GetDescriptorManager();

		// 0) Ensure the unit glyph quad exists in the mega buffers
		EnsureGlyphQuadUploaded();

		// 0.5) Ensure per-frame MSDF SSBO capacity
		const size_t msdfBytes = instances.size() * sizeof(MsdfTextGpuInstanceData);
		dm->EnsurePerFrameMsdfCapacity(msdfBytes);

		// 1) Upload the MSDF instance data to the per-frame descriptor buffer
		dm->UpdatePerFrameMsdfBuffer(
			frameIndex,
			instances.data(),
			msdfBytes
		);

		// 2) Build the indirect command for the glyph quad (same mesh used for all glyphs)
		VkDrawIndexedIndirectCommand cmdInfo{};
		cmdInfo.indexCount = glyphQuadMesh.indexCount;
		cmdInfo.instanceCount = static_cast<uint32_t>(instances.size());
		cmdInfo.firstIndex = static_cast<uint32_t>(glyphQuadMesh.indexOffsetInMegaBuffer / sizeof(uint32_t));
		cmdInfo.vertexOffset = static_cast<int32_t>(glyphQuadMesh.vertexOffsetInMegaBuffer / sizeof(Vertex));
		cmdInfo.firstInstance = 0;

		// Ensure indirect buffer capacity for a single command
		EnsureIndirectCapacity(outIndirectBuf, 1);

		outIndirectBuf->CopyData(&cmdInfo, sizeof(VkDrawIndexedIndirectCommand));

		// 3) Bind MSDF text pipeline
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pm->GetMsdfTextPipeline());

		// 4) Bind descriptor sets
		std::array<VkDescriptorSet, 2> descriptorSets = {
						dm->GetPerFrameDescriptorSet(frameIndex),
						dm->GetBindlessSet()
		};

		vkCmdBindDescriptorSets(
			cmd,
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			pm->GetMsdfTextPipelineLayout(),
			0,
			static_cast<uint32_t>(descriptorSets.size()),
			descriptorSets.data(),
			0,
			nullptr
		);

		// 5) Bind vertex and index buffers
		VkBuffer vertexBuffers[] = {
						megaVertexBuffer->GetBuffer()
		};
		VkDeviceSize offsets[] = { 0 };

		vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
		vkCmdBindIndexBuffer(cmd, megaIndexBuffer->GetBuffer(), 0, VK_INDEX_TYPE_UINT32);

		// 6) Issue indirect draw
		vkCmdDrawIndexedIndirect(
			cmd,
			outIndirectBuf->GetBuffer(),
			0,
			1,
			sizeof(VkDrawIndexedIndirectCommand)
		);
	}

	void VulkanIndexDraw::GrowMegaBuffers(VkDeviceSize additionalVertexSize, VkDeviceSize additionalIndexSize)
	{
		std::cout << "Growing mega mesh buffers" << std::endl;
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
		meshDecoratorInstanceData.clear();
		msdfInstancesData.clear();
		culledVisibleData.clear();
	}

}
