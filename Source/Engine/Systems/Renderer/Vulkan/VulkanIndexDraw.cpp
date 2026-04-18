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

#include <array>

namespace Engine
{

	namespace
	{
		static constexpr uint32_t GpuCullThreadGroupSize = 64;

		enum class GpuCullPassMode : uint32_t
		{
			Reset = 0,
			Cull = 1,
			Finalize = 2
		};

		static uint32_t DivideRoundUp(uint32_t value, uint32_t divisor)
		{
			if (divisor == 0)
			{
				return 0;
			}

			return (value + divisor - 1) / divisor;
		}
	}

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
		: device(device),
		physicalDevice(physicalDevice),
		maxExpectedInstances(MAX_EXPECTED_INSTANCES),
		maxFramesInFlight(MAX_FRAMES_IN_FLIGHT)
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

		worldInstanceBuffers.resize(MAX_FRAMES_IN_FLIGHT);
		const VkDeviceSize worldInstanceBytes = static_cast<VkDeviceSize>(sizeof(GpuInstanceData)) * static_cast<VkDeviceSize>(MAX_EXPECTED_INSTANCES);
		for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
		{
			worldInstanceBuffers[i] = std::make_unique<VulkanBuffer>(
				device,
				physicalDevice,
				worldInstanceBytes,
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
			);
		}

		cpuInstanceData.reserve(MAX_EXPECTED_INSTANCES);
		overlayInstanceData.reserve(MAX_EXPECTED_INSTANCES);
		gpuCullInputData.reserve(MAX_EXPECTED_INSTANCES);
		gpuCullCommandTemplates.reserve(MAX_EXPECTED_INSTANCES);
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
				VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
			);

			buf->Free();
			buf = std::move(newBuf);
		}
	}

	uint64_t VulkanIndexDraw::MakeWorldRenderableKey(entt::entity entity, uint32_t subMaterialIndex)
	{
		const uint32_t entityID = static_cast<uint32_t>(entt::to_integral(entity));
		return (static_cast<uint64_t>(entityID) << 32) | static_cast<uint64_t>(subMaterialIndex);
	}

	uint32_t VulkanIndexDraw::AcquireWorldRenderableSlot()
	{
		if (!worldRenderableFreeSlots.empty())
		{
			const uint32_t slotIndex = worldRenderableFreeSlots.back();
			worldRenderableFreeSlots.pop_back();
			return slotIndex;
		}

		worldRenderableSlots.push_back(WorldRenderableSlot{});
		return static_cast<uint32_t>(worldRenderableSlots.size() - 1);
	}

	void VulkanIndexDraw::FillWorldRenderableSlot
	(
		WorldRenderableSlot& slot,
		entt::entity entity,
		uint32_t subMaterialIndex,
		const std::shared_ptr<MaterialData>& mat,
		bool canUseEntityCullCache
	)
	{
		slot.entity = entity;
		slot.subMaterialIndex = subMaterialIndex;
		slot.material = mat;
		slot.transformSpace = TransformSpace::World;
		slot.canUseEntityCullCache = canUseEntityCullCache;
		slot.active = true;

		if (mat && mat->mesh && mat->mesh->meshBufferData)
		{
			const MeshBufferData& mesh = *mat->mesh->meshBufferData;
			slot.meshID = mesh.GetMeshID();
			slot.indexCount = mesh.indexCount;
			slot.vertexOffsetInMegaBuffer = mesh.vertexOffsetInMegaBuffer;
			slot.indexOffsetInMegaBuffer = mesh.indexOffsetInMegaBuffer;
		}
		else
		{
			slot.meshID = 0;
			slot.indexCount = 0;
			slot.vertexOffsetInMegaBuffer = 0;
			slot.indexOffsetInMegaBuffer = 0;
		}
	}

	void VulkanIndexDraw::RebuildWorldRenderableSlots(Scene& scene)
	{
		entt::registry& registry = scene.GetRegistry();

		const std::unordered_map<uint64_t, uint32_t> oldKeyToSlot = worldRenderableKeyToSlot;

		worldRenderableKeyToSlot.clear();
		worldEntityToSlotIndices.clear();

		std::vector<uint8_t> slotTouched(worldRenderableSlots.size(), 0);

		auto touchSlot =
			[&](entt::entity entity, uint32_t subMaterialIndex, const std::shared_ptr<MaterialData>& mat, bool canUseEntityCullCache)
		{
			if (!mat || !mat->mesh || !mat->mesh->meshBufferData)
			{
				return;
			}

			const uint64_t key = MakeWorldRenderableKey(entity, subMaterialIndex);

			uint32_t slotIndex = 0;
			auto oldIt = oldKeyToSlot.find(key);
			if (oldIt != oldKeyToSlot.end())
			{
				slotIndex = oldIt->second;
			}
			else
			{
				slotIndex = AcquireWorldRenderableSlot();
				if (slotIndex >= slotTouched.size())
				{
					slotTouched.resize(slotIndex + 1, 0);
				}
			}

			FillWorldRenderableSlot(worldRenderableSlots[slotIndex], entity, subMaterialIndex, mat, canUseEntityCullCache);

			slotTouched[slotIndex] = 1;
			worldRenderableKeyToSlot[key] = slotIndex;
			worldEntityToSlotIndices[entity].push_back(slotIndex);
		};

		auto regularView = registry.view<Transform, Material>();
		for (auto entity : regularView)
		{
			if (registry.any_of<MeshDecorator>(entity))
			{
				continue;
			}

			const Transform& tf = regularView.get<Transform>(entity);
			if (tf.GetTransformSpace() != TransformSpace::World)
			{
				continue;
			}

			const bool canUseEntityCullCache =
				registry.any_of<FrustumCullCache>(entity)
				&& !registry.any_of<CompositeMaterial>(entity);

			touchSlot(entity, 0, regularView.get<Material>(entity).data, canUseEntityCullCache);
		}

		auto compositeView = registry.view<Transform, CompositeMaterial>();
		for (auto entity : compositeView)
		{
			if (registry.any_of<MeshDecorator>(entity))
			{
				continue;
			}

			const Transform& tf = compositeView.get<Transform>(entity);
			if (tf.GetTransformSpace() != TransformSpace::World)
			{
				continue;
			}

			const CompositeMaterial& composite = compositeView.get<CompositeMaterial>(entity);
			for (uint32_t i = 0; i < static_cast<uint32_t>(composite.subMaterials.size()); ++i)
			{
				touchSlot(entity, i, composite.subMaterials[i], false);
			}
		}

		worldRenderableFreeSlots.clear();
		for (uint32_t i = 0; i < static_cast<uint32_t>(worldRenderableSlots.size()); ++i)
		{
			if (i >= slotTouched.size() || !slotTouched[i])
			{
				worldRenderableSlots[i].active = false;
				worldRenderableFreeSlots.push_back(i);
			}
		}

		worldRenderableSlotsScene = &scene;
		worldRenderableSlotsRevision = scene.GetRenderablesRevision();
		gpuWorldSceneDirty = true;
	}

	void VulkanIndexDraw::SyncWorldRenderableSlots(Scene& scene)
	{
		if (worldRenderableSlotsScene != &scene || worldRenderableSlotsRevision != scene.GetRenderablesRevision())
		{
			RebuildWorldRenderableSlots(scene);
		}
	}

	void VulkanIndexDraw::CreateIndirectBuffers(uint32_t maxDrawCalls, uint32_t framesInFlight)
	{
		maxIndirectDrawCount = maxDrawCalls;
		indirectCommandBuffers.resize(framesInFlight);
		meshDecoratorIndirectCommandBuffers.resize(framesInFlight);
		msdfIndirectCommandBuffers.resize(framesInFlight);

		for (uint32_t i = 0; i < framesInFlight; ++i)
		{
			indirectCommandBuffers[i] = std::make_unique<VulkanBuffer>(
				device,
				physicalDevice,
				sizeof(VkDrawIndexedIndirectCommand) * maxDrawCalls,
				VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
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

		CreateGpuCullResources(maxDrawCalls, framesInFlight);
	}

	void VulkanIndexDraw::CreateGpuCullResources(uint32_t maxDrawCalls, uint32_t framesInFlight)
	{
		gpuCullDrawCountBuffers.resize(framesInFlight);
		gpuCullDescriptorSets.resize(framesInFlight);
		gpuWorldTransformBuffers.resize(framesInFlight);
		gpuWorldTransformStagingBuffers.resize(framesInFlight);
		gpuWorldVisibleIndexBuffers.resize(framesInFlight);
		gpuWorldIndirectCommandBuffers.resize(framesInFlight);

		const VkDeviceSize drawCountBytes = static_cast<VkDeviceSize>(sizeof(uint32_t)) * static_cast<VkDeviceSize>(maxDrawCalls);
		const VkDeviceSize templateBytes = static_cast<VkDeviceSize>(sizeof(VkDrawIndexedIndirectCommand)) * static_cast<VkDeviceSize>(maxDrawCalls);
		const VkDeviceSize staticBytes = static_cast<VkDeviceSize>(sizeof(GpuWorldInstanceStaticData)) * static_cast<VkDeviceSize>(maxExpectedInstances);
		const VkDeviceSize transformBytes = static_cast<VkDeviceSize>(sizeof(GpuWorldInstanceTransformData)) * static_cast<VkDeviceSize>(maxExpectedInstances);
		const VkDeviceSize visibleIndexBytes = static_cast<VkDeviceSize>(sizeof(uint32_t)) * static_cast<VkDeviceSize>(maxExpectedInstances);
		const VkDeviceSize indirectBytes = static_cast<VkDeviceSize>(sizeof(VkDrawIndexedIndirectCommand)) * static_cast<VkDeviceSize>(maxDrawCalls);

		gpuWorldStaticBuffer = std::make_unique<VulkanBuffer>(
			device,
			physicalDevice,
			staticBytes,
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
		);

		gpuWorldStaticStagingBuffer = std::make_unique<VulkanBuffer>(
			device,
			physicalDevice,
			staticBytes,
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
		);

		gpuCullCommandTemplateStaticBuffer = std::make_unique<VulkanBuffer>(
			device,
			physicalDevice,
			templateBytes,
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
		);

		gpuCullCommandTemplateStagingBuffer = std::make_unique<VulkanBuffer>(
			device,
			physicalDevice,
			templateBytes,
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
		);

		for (uint32_t i = 0; i < framesInFlight; ++i)
		{
			gpuCullDrawCountBuffers[i] = std::make_unique<VulkanBuffer>(
				device,
				physicalDevice,
				drawCountBytes,
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
			);

			gpuWorldTransformBuffers[i] = std::make_unique<VulkanBuffer>(
				device,
				physicalDevice,
				transformBytes,
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
			);

			gpuWorldTransformStagingBuffers[i] = std::make_unique<VulkanBuffer>(
				device,
				physicalDevice,
				transformBytes,
				VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
			);

			// Retained only so the existing descriptor manager wiring still has a valid buffer for binding 7.
			gpuWorldVisibleIndexBuffers[i] = std::make_unique<VulkanBuffer>(
				device,
				physicalDevice,
				visibleIndexBytes,
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
			);

			gpuWorldIndirectCommandBuffers[i] = std::make_unique<VulkanBuffer>(
				device,
				physicalDevice,
				indirectBytes,
				VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
			);
		}

		std::array<VkDescriptorSetLayoutBinding, 6> bindings{};
		for (uint32_t i = 0; i < static_cast<uint32_t>(bindings.size()); ++i)
		{
			bindings[i].binding = i;
			bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			bindings[i].descriptorCount = 1;
			bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
			bindings[i].pImmutableSamplers = nullptr;
		}

		VkDescriptorSetLayoutCreateInfo layoutInfo{};
		layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
		layoutInfo.pBindings = bindings.data();
		if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &gpuCullDescriptorSetLayout) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to create GPU cull descriptor set layout!");
		}

		VkDescriptorPoolSize poolSize{};
		poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		poolSize.descriptorCount = framesInFlight * static_cast<uint32_t>(bindings.size());

		VkDescriptorPoolCreateInfo poolInfo{};
		poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		poolInfo.poolSizeCount = 1;
		poolInfo.pPoolSizes = &poolSize;
		poolInfo.maxSets = framesInFlight;
		if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &gpuCullDescriptorPool) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to create GPU cull descriptor pool!");
		}

		std::vector<VkDescriptorSetLayout> layouts(framesInFlight, gpuCullDescriptorSetLayout);
		VkDescriptorSetAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = gpuCullDescriptorPool;
		allocInfo.descriptorSetCount = framesInFlight;
		allocInfo.pSetLayouts = layouts.data();
		if (vkAllocateDescriptorSets(device, &allocInfo, gpuCullDescriptorSets.data()) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to allocate GPU cull descriptor sets!");
		}

		for (uint32_t i = 0; i < framesInFlight; ++i)
		{
			VkDescriptorBufferInfo staticInfo{};
			staticInfo.buffer = gpuWorldStaticBuffer->GetBuffer();
			staticInfo.offset = 0;
			staticInfo.range = VK_WHOLE_SIZE;

			VkDescriptorBufferInfo transformInfo{};
			transformInfo.buffer = gpuWorldTransformBuffers[i]->GetBuffer();
			transformInfo.offset = 0;
			transformInfo.range = VK_WHOLE_SIZE;

			VkDescriptorBufferInfo outputInstanceInfo{};
			outputInstanceInfo.buffer = worldInstanceBuffers[i]->GetBuffer();
			outputInstanceInfo.offset = 0;
			outputInstanceInfo.range = VK_WHOLE_SIZE;

			VkDescriptorBufferInfo drawCountInfo{};
			drawCountInfo.buffer = gpuCullDrawCountBuffers[i]->GetBuffer();
			drawCountInfo.offset = 0;
			drawCountInfo.range = VK_WHOLE_SIZE;

			VkDescriptorBufferInfo templateInfo{};
			templateInfo.buffer = gpuCullCommandTemplateStaticBuffer->GetBuffer();
			templateInfo.offset = 0;
			templateInfo.range = VK_WHOLE_SIZE;

			VkDescriptorBufferInfo indirectInfo{};
			indirectInfo.buffer = gpuWorldIndirectCommandBuffers[i]->GetBuffer();
			indirectInfo.offset = 0;
			indirectInfo.range = VK_WHOLE_SIZE;

			std::array<VkWriteDescriptorSet, 6> writes{};
			for (uint32_t writeIndex = 0; writeIndex < static_cast<uint32_t>(writes.size()); ++writeIndex)
			{
				writes[writeIndex].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				writes[writeIndex].dstSet = gpuCullDescriptorSets[i];
				writes[writeIndex].dstBinding = writeIndex;
				writes[writeIndex].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
				writes[writeIndex].descriptorCount = 1;
			}

			writes[0].pBufferInfo = &staticInfo;
			writes[1].pBufferInfo = &transformInfo;
			writes[2].pBufferInfo = &outputInstanceInfo;
			writes[3].pBufferInfo = &drawCountInfo;
			writes[4].pBufferInfo = &templateInfo;
			writes[5].pBufferInfo = &indirectInfo;
			vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
		}
	}

	void VulkanIndexDraw::DestroyGpuCullResources()
	{
		if (gpuCullDescriptorPool != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorPool(device, gpuCullDescriptorPool, nullptr);
			gpuCullDescriptorPool = VK_NULL_HANDLE;
		}

		if (gpuCullDescriptorSetLayout != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorSetLayout(device, gpuCullDescriptorSetLayout, nullptr);
			gpuCullDescriptorSetLayout = VK_NULL_HANDLE;
		}

		gpuCullDescriptorSets.clear();
		gpuCullInputBuffers.clear();
		gpuCullDrawCountBuffers.clear();
		gpuCullVisibleDrawCountBuffers.clear();
		gpuCullCommandTemplateBuffers.clear();
		gpuWorldTransformBuffers.clear();
		gpuWorldTransformStagingBuffers.clear();
		gpuWorldVisibleIndexBuffers.clear();
		gpuWorldIndirectCommandBuffers.clear();
		gpuWorldStaticBuffer.reset();
		gpuWorldStaticStagingBuffer.reset();
		gpuCullCommandTemplateStaticBuffer.reset();
		gpuCullCommandTemplateStagingBuffer.reset();
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
		(void)scene;
		(void)frustum;
		return false;
	}


	void VulkanIndexDraw::UploadCachedWorldPacketToFrame(uint32_t frameIndex)
	{
		(void)frameIndex;
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

		SyncWorldRenderableSlots(scene);

		gatherCandidatesScratch.clear();
		gatherCandidatesScratch.reserve(worldRenderableSlots.size());

		for (const WorldRenderableSlot& slot : worldRenderableSlots)
		{
			if (!slot.active)
			{
				continue;
			}

			if (!registry.valid(slot.entity) || !registry.any_of<Transform>(slot.entity))
			{
				continue;
			}

			if (!scene.ShouldRenderBasedOnState(slot.entity))
			{
				continue;
			}

			const Transform& tf = registry.get<Transform>(slot.entity);

			GatherCandidate candidate{};
			candidate.entity = slot.entity;
			candidate.material = slot.material;
			candidate.worldMatrix = tf.GetWorldMatrix(registry);
			candidate.worldVersion = tf.GetWorldVersion();
			candidate.transformSpace = tf.GetTransformSpace();
			candidate.canUseEntityCullCache = false;
			gatherCandidatesScratch.push_back(std::move(candidate));
		}

		for (uint32_t meshID : activeMeshBucketKeys)
		{
			meshBuckets[meshID].instances.clear();
		}
		activeMeshBucketKeys.clear();

		ProcessGatherCandidates(registry, gatherCandidatesScratch, nullptr);

		fullScenePacketScene = &scene;
		fullScenePacketRenderablesRevision = scene.GetRenderablesRevision();
		++fullScenePacketVersion;
		fullScenePacketValid = true;
	}


	bool VulkanIndexDraw::UpdateFullScenePacketDirtyEntities(Scene& scene)
	{
		(void)scene;
		return false;
	}


	bool VulkanIndexDraw::PatchFullScenePacketFrame(uint32_t frameIndex)
	{
		(void)frameIndex;
		return false;
	}


	void VulkanIndexDraw::UploadFullScenePacket(uint32_t frameIndex, Scene& scene)
	{
		BuildFullScenePacket(scene);
		UploadAndBatchInstances(frameIndex);
	}


	void VulkanIndexDraw::UpdateInstanceBuffer(uint32_t frameIndex)
	{
		meshDecoratorInstanceData.clear();
		msdfInstancesData.clear();
		overlayInstanceData.clear();

		const std::shared_ptr<Scene>& scene = SwimEngine::GetInstance()->GetSceneSystem()->GetActiveScene();
		entt::registry& registry = scene->GetRegistry();

		const Frustum* frustum = nullptr;
		if (cullMode == CullMode::CPU || cullMode == CullMode::GPU)
		{
			std::shared_ptr<CameraSystem> camera = scene->GetCameraSystem();
			Frustum::SetCameraMatrices(camera->GetViewMatrix(), camera->GetProjectionMatrix());
			if (cullMode == CullMode::CPU)
			{
				frustum = &Frustum::Get();
			}
		}

		SyncWorldRenderableSlots(*scene);

		for (uint32_t meshID : activeMeshBucketKeys)
		{
			meshBuckets[meshID].instances.clear();
		}
		activeMeshBucketKeys.clear();
		cpuInstanceData.clear();
		worldDrawCommands.clear();
		gpuCullInputData.clear();
		gpuCullInputCount = 0;

		if (cullMode == CullMode::GPU)
		{
			// The GPU cull path keeps its draw-command templates and scene packet cached across frames.
			// Clearing them here and then early-outing from RebuildGpuWorldScenePacket() when the scene is unchanged
			// leaves gpuCullDrawCommandCount at 0 for perfectly valid frames, which manifests as alternating or missing world draws.
			RebuildGpuWorldScenePacket(*scene, registry);
			UpdateGpuWorldTransformPacket(*scene, registry, frameIndex);
			return;
		}

		gpuCullCommandTemplates.clear();
		gpuCullDrawCommandCount = 0;

		if (CanUseFullScenePacket(*scene, frustum))
		{
			UploadFullScenePacket(frameIndex, *scene);
			return;
		}

		if (cullMode == CullMode::CPU && frustum && useQueriedFrustumSceneBVH)
		{
			GatherCandidatesBVH(*scene, *frustum);
		}
		else
		{
			GatherCandidatesView(registry, TransformSpace::World, frustum);
		}

		UploadAndBatchInstances(frameIndex);
	}


	void VulkanIndexDraw::GatherCandidatesBVH(Scene& scene, const Frustum& frustum)
	{
		entt::registry& registry = scene.GetRegistry();
		visibleEntityScratch.clear();
		scene.GetSceneBVH()->QueryFrustumParallel(frustum, visibleEntityScratch);

		gatherCandidatesScratch.clear();
		gatherCandidatesScratch.reserve(visibleEntityScratch.size() * 2);
		for (entt::entity entity : visibleEntityScratch)
		{
			if (entity == entt::null || !registry.valid(entity))
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

			auto slotIt = worldEntityToSlotIndices.find(entity);
			if (slotIt == worldEntityToSlotIndices.end())
			{
				continue;
			}

			if (!registry.any_of<Transform>(entity))
			{
				continue;
			}

			const Transform& tf = registry.get<Transform>(entity);
			const glm::mat4& world = tf.GetWorldMatrix(registry);

			for (uint32_t slotIndex : slotIt->second)
			{
				if (slotIndex >= worldRenderableSlots.size())
				{
					continue;
				}

				const WorldRenderableSlot& slot = worldRenderableSlots[slotIndex];
				if (!slot.active || !slot.material)
				{
					continue;
				}

				GatherCandidate candidate{};
				candidate.entity = entity;
				candidate.material = slot.material;
				candidate.worldMatrix = world;
				candidate.worldVersion = tf.GetWorldVersion();
				candidate.transformSpace = tf.GetTransformSpace();
				candidate.canUseEntityCullCache = false;
				gatherCandidatesScratch.push_back(std::move(candidate));
			}
		}

		ProcessGatherCandidates(registry, gatherCandidatesScratch, nullptr);
	}


	void VulkanIndexDraw::GatherCandidatesView(entt::registry& registry, const TransformSpace space, const Frustum* frustum)
	{
		auto& scene = SwimEngine::GetInstance()->GetSceneSystem()->GetActiveScene();
		gatherCandidatesScratch.clear();
		gatherCandidatesScratch.reserve(worldRenderableSlots.size());

		for (const WorldRenderableSlot& slot : worldRenderableSlots)
		{
			if (!slot.active)
			{
				continue;
			}

			if (!registry.valid(slot.entity) || !registry.any_of<Transform>(slot.entity))
			{
				continue;
			}

			if (!scene->ShouldRenderBasedOnState(slot.entity))
			{
				continue;
			}

			const Transform& tf = registry.get<Transform>(slot.entity);
			if (space == TransformSpace::Ambiguous || tf.GetTransformSpace() == space)
			{
				GatherCandidate candidate{};
				candidate.entity = slot.entity;
				candidate.material = slot.material;
				candidate.worldMatrix = tf.GetWorldMatrix(registry);
				candidate.worldVersion = tf.GetWorldVersion();
				candidate.transformSpace = tf.GetTransformSpace();
				candidate.canUseEntityCullCache = slot.canUseEntityCullCache;
				gatherCandidatesScratch.push_back(std::move(candidate));
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

		// World instances live in their own per-frame SSBO so the GPU culling path can own it too
		if (!cpuInstanceData.empty())
		{
			worldInstanceBuffers[frameIndex]->CopyData(cpuInstanceData.data(), sizeof(GpuInstanceData) * cpuInstanceData.size());
		}

		// Ensure indirect buffer capacity for world draws
		EnsureIndirectCapacity(indirectCommandBuffers[frameIndex], worldDrawCommands.size());

		if (!worldDrawCommands.empty())
		{
			VulkanBuffer& indirectBuf = *indirectCommandBuffers[frameIndex];
			indirectBuf.CopyData(worldDrawCommands.data(), worldDrawCommands.size() * sizeof(VkDrawIndexedIndirectCommand));
		}
	}

	void VulkanIndexDraw::RebuildGpuWorldScenePacket(Scene& scene, entt::registry& registry)
	{
		if (!gpuWorldSceneDirty && gpuWorldSceneInstanceCount > 0)
		{
			// The scene packet is persistent for the GPU path. Make sure the live counters stay in sync with the cached data
			// even when we skip a rebuild for an unchanged scene.
			gpuCullDrawCommandCount = static_cast<uint32_t>(gpuCullCommandTemplates.size());
			gpuWorldVisibleCapacity = gpuWorldSceneInstanceCount;
			return;
		}

		struct GpuWorldBuildEntry
		{
			entt::entity entity{ entt::null };
			uint32_t meshID = 0;
			uint32_t indexCount = 0;
			VkDeviceSize vertexOffsetInMegaBuffer = 0;
			VkDeviceSize indexOffsetInMegaBuffer = 0;
			uint32_t textureIndex = 0;
			uint32_t flags = 0;
			glm::vec4 boundsCenterRadius{ 0.0f };
			bool canUseEntityCullCache = false;
		};

		std::vector<GpuWorldBuildEntry> buildEntries;
		buildEntries.reserve(worldRenderableSlots.size());

		for (const WorldRenderableSlot& slot : worldRenderableSlots)
		{
			if (!slot.active || !slot.material || !slot.material->mesh || !slot.material->mesh->meshBufferData)
			{
				continue;
			}

			if (!registry.valid(slot.entity) || !registry.any_of<Transform>(slot.entity))
			{
				continue;
			}

			const Transform& tf = registry.get<Transform>(slot.entity);
			if (tf.GetTransformSpace() != TransformSpace::World)
			{
				continue;
			}

			GpuWorldBuildEntry entry{};
			entry.entity = slot.entity;
			entry.meshID = slot.meshID;
			entry.indexCount = slot.indexCount;
			entry.vertexOffsetInMegaBuffer = slot.vertexOffsetInMegaBuffer;
			entry.indexOffsetInMegaBuffer = slot.indexOffsetInMegaBuffer;
			entry.textureIndex = slot.material->albedoMap ? slot.material->albedoMap->GetBindlessIndex() : 0u;
			entry.flags = slot.material->albedoMap ? GpuWorldInstanceFlags::HasTexture : 0u;
			const glm::vec3 localMin = glm::vec3(slot.material->mesh->meshBufferData->aabbMin);
			const glm::vec3 localMax = glm::vec3(slot.material->mesh->meshBufferData->aabbMax);
			const glm::vec3 center = (localMin + localMax) * 0.5f;
			const float radius = glm::length(localMax - center);
			entry.boundsCenterRadius = glm::vec4(center, radius);
			entry.canUseEntityCullCache = slot.canUseEntityCullCache;
			buildEntries.push_back(std::move(entry));
		}

		std::sort(buildEntries.begin(), buildEntries.end(), [](const GpuWorldBuildEntry& a, const GpuWorldBuildEntry& b)
		{
			if (a.meshID != b.meshID)
			{
				return a.meshID < b.meshID;
			}
			return entt::to_integral(a.entity) < entt::to_integral(b.entity);
		});

		gpuWorldSceneInstances.clear();
		gpuWorldStaticCpuData.clear();
		gpuWorldTransformCpuData.clear();
		gpuCullCommandTemplates.clear();
		gpuWorldSceneInstances.reserve(buildEntries.size());
		gpuWorldStaticCpuData.reserve(buildEntries.size());
		gpuWorldTransformCpuData.resize(buildEntries.size());
		gpuCullCommandTemplates.reserve(buildEntries.size());

		uint32_t currentBatchMeshID = UINT32_MAX;
		uint32_t currentBatchIndex = 0;
		uint32_t currentBatchBaseInstance = 0;
		uint32_t currentBatchMaxInstances = 0;

		for (const GpuWorldBuildEntry& entry : buildEntries)
		{
			if (entry.meshID != currentBatchMeshID)
			{
				currentBatchMeshID = entry.meshID;
				currentBatchIndex = static_cast<uint32_t>(gpuCullCommandTemplates.size());
				currentBatchBaseInstance += currentBatchMaxInstances;
				currentBatchMaxInstances = 0;

				VkDrawIndexedIndirectCommand cmd{};
				cmd.indexCount = entry.indexCount;
				cmd.instanceCount = 0;
				cmd.firstIndex = static_cast<uint32_t>(entry.indexOffsetInMegaBuffer / sizeof(uint32_t));
				cmd.vertexOffset = static_cast<int32_t>(entry.vertexOffsetInMegaBuffer / sizeof(Vertex));
				cmd.firstInstance = currentBatchBaseInstance;
				gpuCullCommandTemplates.push_back(cmd);
			}

			GpuWorldSceneInstance sceneInstance{};
			sceneInstance.entity = entry.entity;
			sceneInstance.meshID = entry.meshID;
			sceneInstance.drawCommandIndex = currentBatchIndex;
			sceneInstance.outputBaseInstance = currentBatchBaseInstance;
			sceneInstance.canUseEntityCullCache = entry.canUseEntityCullCache;
			gpuWorldSceneInstances.push_back(sceneInstance);

			GpuWorldInstanceStaticData staticData{};
			staticData.boundsCenterRadius = entry.boundsCenterRadius;
			staticData.textureIndex = entry.textureIndex;
			staticData.hasTexture = (entry.flags & GpuWorldInstanceFlags::HasTexture) ? 1.0f : 0.0f;
			staticData.meshInfoIndex = entry.meshID;
			staticData.materialIndex = 0;
			staticData.indexCount = entry.indexCount;
			staticData.space = static_cast<uint32_t>(TransformSpace::World);
			staticData.vertexOffsetInMegaBufferLo = static_cast<uint32_t>(entry.vertexOffsetInMegaBuffer & 0xffffffffull);
			staticData.vertexOffsetInMegaBufferHi = static_cast<uint32_t>((entry.vertexOffsetInMegaBuffer >> 32) & 0xffffffffull);
			staticData.indexOffsetInMegaBufferLo = static_cast<uint32_t>(entry.indexOffsetInMegaBuffer & 0xffffffffull);
			staticData.indexOffsetInMegaBufferHi = static_cast<uint32_t>((entry.indexOffsetInMegaBuffer >> 32) & 0xffffffffull);
			staticData.drawCommandIndex = currentBatchIndex;
			staticData.outputBaseInstance = currentBatchBaseInstance;
			gpuWorldStaticCpuData.push_back(staticData);

			++currentBatchMaxInstances;
		}

		gpuWorldSceneInstanceCount = static_cast<uint32_t>(gpuWorldSceneInstances.size());
		gpuCullDrawCommandCount = static_cast<uint32_t>(gpuCullCommandTemplates.size());
		gpuWorldVisibleCapacity = gpuWorldSceneInstanceCount;

		if (gpuWorldSceneInstanceCount > 0)
		{
			gpuWorldStaticStagingBuffer->CopyData(gpuWorldStaticCpuData.data(), static_cast<size_t>(gpuWorldSceneInstanceCount) * sizeof(GpuWorldInstanceStaticData));
			gpuWorldStaticUploadPending = true;
		}

		if (gpuCullDrawCommandCount > 0)
		{
			gpuCullCommandTemplateStagingBuffer->CopyData(gpuCullCommandTemplates.data(), static_cast<size_t>(gpuCullDrawCommandCount) * sizeof(VkDrawIndexedIndirectCommand));
			gpuWorldTemplateUploadPending = true;
		}

		gpuWorldSceneDirty = false;
	}

	void VulkanIndexDraw::UpdateGpuWorldTransformPacket(Scene& scene, entt::registry& registry, uint32_t frameIndex)
	{
		gpuWorldTransformCpuData.resize(gpuWorldSceneInstances.size());
		for (size_t i = 0; i < gpuWorldSceneInstances.size(); ++i)
		{
			const GpuWorldSceneInstance& sceneInstance = gpuWorldSceneInstances[i];
			GpuWorldInstanceTransformData transformData{};
			if (registry.valid(sceneInstance.entity) && registry.any_of<Transform>(sceneInstance.entity))
			{
				const Transform& tf = registry.get<Transform>(sceneInstance.entity);
				transformData.model = tf.GetWorldMatrix(registry);
				transformData.enabled = scene.ShouldRenderBasedOnState(sceneInstance.entity) ? 1u : 0u;
			}
			else
			{
				transformData.model = glm::mat4(1.0f);
				transformData.enabled = 0u;
			}

			gpuWorldTransformCpuData[i] = transformData;
		}

		if (!gpuWorldTransformCpuData.empty())
		{
			gpuWorldTransformStagingBuffers[frameIndex]->CopyData(
				gpuWorldTransformCpuData.data(),
				gpuWorldTransformCpuData.size() * sizeof(GpuWorldInstanceTransformData)
			);
		}
	}

	void VulkanIndexDraw::BuildGpuCullInputPacket(entt::registry& registry)
	{
		struct MeshBatchTemplate
		{
			uint32_t meshID = 0;
			uint32_t indexCount = 0;
			VkDeviceSize vertexOffsetInMegaBuffer = 0;
			VkDeviceSize indexOffsetInMegaBuffer = 0;
			uint32_t maxInstances = 0;
		};

		auto& scene = SwimEngine::GetInstance()->GetSceneSystem()->GetActiveScene();
		std::unordered_map<uint32_t, MeshBatchTemplate> meshTemplates;
		std::vector<uint32_t> uniqueMeshIDs;
		uniqueMeshIDs.reserve(worldRenderableSlots.size());
		gpuCullInputData.clear();
		gpuCullCommandTemplates.clear();

		for (const WorldRenderableSlot& slot : worldRenderableSlots)
		{
			if (!slot.active || !slot.material)
			{
				continue;
			}

			if (!registry.valid(slot.entity) || !registry.any_of<Transform>(slot.entity))
			{
				continue;
			}

			if (registry.any_of<MeshDecorator>(slot.entity))
			{
				continue;
			}

			if (!scene->ShouldRenderBasedOnState(slot.entity))
			{
				continue;
			}

			const Transform& tf = registry.get<Transform>(slot.entity);
			if (tf.GetTransformSpace() != TransformSpace::World)
			{
				continue;
			}

			GpuCullInputInstanceData input{};
			input.instance.model = tf.GetWorldMatrix(registry);
			input.instance.aabbMin = slot.material->mesh->meshBufferData->aabbMin;
			input.instance.aabbMax = slot.material->mesh->meshBufferData->aabbMax;
			input.instance.textureIndex = slot.material->albedoMap ? slot.material->albedoMap->GetBindlessIndex() : 0;
			input.instance.hasTexture = slot.material->albedoMap ? 1.0f : 0.0f;
			input.instance.meshInfoIndex = slot.meshID;
			input.instance.materialIndex = slot.subMaterialIndex;
			input.instance.indexCount = slot.indexCount;
			input.instance.space = static_cast<uint32_t>(TransformSpace::World);
			input.instance.vertexOffsetInMegaBuffer = slot.vertexOffsetInMegaBuffer;
			input.instance.indexOffsetInMegaBuffer = slot.indexOffsetInMegaBuffer;
			gpuCullInputData.push_back(input);

			auto [it, inserted] = meshTemplates.emplace(slot.meshID, MeshBatchTemplate{});
			if (inserted)
			{
				it->second.meshID = slot.meshID;
				it->second.indexCount = slot.indexCount;
				it->second.vertexOffsetInMegaBuffer = slot.vertexOffsetInMegaBuffer;
				it->second.indexOffsetInMegaBuffer = slot.indexOffsetInMegaBuffer;
				uniqueMeshIDs.push_back(slot.meshID);
			}

			it->second.maxInstances++;
		}

		std::sort(uniqueMeshIDs.begin(), uniqueMeshIDs.end());
		std::unordered_map<uint32_t, uint32_t> meshToBatchIndex;
		meshToBatchIndex.reserve(uniqueMeshIDs.size());
		gpuCullCommandTemplates.reserve(uniqueMeshIDs.size());

		uint32_t firstInstance = 0;
		for (uint32_t meshID : uniqueMeshIDs)
		{
			const MeshBatchTemplate& batch = meshTemplates[meshID];
			meshToBatchIndex.emplace(meshID, static_cast<uint32_t>(gpuCullCommandTemplates.size()));

			VkDrawIndexedIndirectCommand cmd{};
			cmd.indexCount = batch.indexCount;
			cmd.instanceCount = 0;
			cmd.firstIndex = static_cast<uint32_t>(batch.indexOffsetInMegaBuffer / sizeof(uint32_t));
			cmd.vertexOffset = static_cast<int32_t>(batch.vertexOffsetInMegaBuffer / sizeof(Vertex));
			cmd.firstInstance = firstInstance;
			gpuCullCommandTemplates.push_back(cmd);
			firstInstance += batch.maxInstances;
		}

		for (GpuCullInputInstanceData& input : gpuCullInputData)
		{
			input.drawCommandIndex = meshToBatchIndex.at(input.instance.meshInfoIndex);
		}

		gpuCullInputCount = static_cast<uint32_t>(gpuCullInputData.size());
		gpuCullDrawCommandCount = static_cast<uint32_t>(gpuCullCommandTemplates.size());

		if (gpuCullInputCount > static_cast<uint32_t>(maxExpectedInstances))
		{
			throw std::runtime_error("GPU cull input exceeded maxExpectedInstances capacity.");
		}

		if (gpuCullDrawCommandCount > maxIndirectDrawCount)
		{
			throw std::runtime_error("GPU cull indirect command count exceeded CreateIndirectBuffers capacity.");
		}

		worldDrawCommands = gpuCullCommandTemplates;
	}

	void VulkanIndexDraw::UploadGpuCullInput(uint32_t frameIndex)
	{
		if (gpuCullInputCount > 0)
		{
			gpuCullInputBuffers[frameIndex]->CopyData(
				gpuCullInputData.data(),
				static_cast<size_t>(gpuCullInputCount) * sizeof(GpuCullInputInstanceData)
			);
		}

		if (gpuCullDrawCommandCount > 0)
		{
			gpuCullCommandTemplateBuffers[frameIndex]->CopyData(
				gpuCullCommandTemplates.data(),
				static_cast<size_t>(gpuCullDrawCommandCount) * sizeof(VkDrawIndexedIndirectCommand)
			);
		}
	}

	void VulkanIndexDraw::DispatchGpuCull(uint32_t frameIndex, VkCommandBuffer cmd)
	{
		std::shared_ptr<VulkanRenderer> renderer = SwimEngine::GetInstance()->GetVulkanRenderer();
		const std::unique_ptr<VulkanPipelineManager>& pipelineManager = renderer->GetPipelineManager();
		if (!pipelineManager || !pipelineManager->HasGpuCullComputePipeline() || gpuCullDrawCommandCount == 0 || gpuWorldSceneInstanceCount == 0)
		{
			return;
		}

		const VkPipeline computePipeline = pipelineManager->GetGpuCullComputePipeline();
		const VkPipelineLayout computePipelineLayout = pipelineManager->GetGpuCullComputePipelineLayout();
		const Frustum& frustum = Frustum::Get();

		GpuCullPushConstants pushConstants{};
		pushConstants.instanceCount = gpuWorldSceneInstanceCount;
		pushConstants.drawCommandCount = gpuCullDrawCommandCount;
		pushConstants.compactDraws = 0;
		for (int i = 0; i < 6; ++i)
		{
			pushConstants.frustumPlanes[i] = frustum.planes[i];
		}

		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline);
		vkCmdBindDescriptorSets(
			cmd,
			VK_PIPELINE_BIND_POINT_COMPUTE,
			computePipelineLayout,
			0,
			1,
			&gpuCullDescriptorSets[frameIndex],
			0,
			nullptr
		);

		const uint32_t drawGroupCount = DivideRoundUp(gpuCullDrawCommandCount, GpuCullThreadGroupSize);
		const uint32_t instanceGroupCount = DivideRoundUp(gpuWorldSceneInstanceCount, GpuCullThreadGroupSize);

		vkCmdFillBuffer(cmd, gpuCullDrawCountBuffers[frameIndex]->GetBuffer(), 0, VK_WHOLE_SIZE, 0);

		VkBufferMemoryBarrier fillBarrier{};
		fillBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
		fillBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		fillBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
		fillBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		fillBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		fillBarrier.buffer = gpuCullDrawCountBuffers[frameIndex]->GetBuffer();
		fillBarrier.offset = 0;
		fillBarrier.size = VK_WHOLE_SIZE;

		vkCmdPipelineBarrier(
			cmd,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			0,
			0,
			nullptr,
			1,
			&fillBarrier,
			0,
			nullptr
		);

		if (instanceGroupCount > 0)
		{
			pushConstants.mode = static_cast<uint32_t>(GpuCullPassMode::Cull);
			vkCmdPushConstants(cmd, computePipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(GpuCullPushConstants), &pushConstants);
			vkCmdDispatch(cmd, instanceGroupCount, 1, 1);

			VkBufferMemoryBarrier cullBarrier{};
			cullBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
			cullBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
			cullBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
			cullBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			cullBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			cullBarrier.buffer = gpuCullDrawCountBuffers[frameIndex]->GetBuffer();
			cullBarrier.offset = 0;
			cullBarrier.size = VK_WHOLE_SIZE;

			vkCmdPipelineBarrier(
				cmd,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				0,
				0,
				nullptr,
				1,
				&cullBarrier,
				0,
				nullptr
			);
		}

		if (drawGroupCount > 0)
		{
			pushConstants.mode = static_cast<uint32_t>(GpuCullPassMode::Finalize);
			vkCmdPushConstants(cmd, computePipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(GpuCullPushConstants), &pushConstants);
			vkCmdDispatch(cmd, drawGroupCount, 1, 1);
		}

		std::array<VkBufferMemoryBarrier, 3> finalBarriers{};
		finalBarriers[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
		finalBarriers[0].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		finalBarriers[0].dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
		finalBarriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		finalBarriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		finalBarriers[0].buffer = gpuWorldIndirectCommandBuffers[frameIndex]->GetBuffer();
		finalBarriers[0].offset = 0;
		finalBarriers[0].size = VK_WHOLE_SIZE;

		finalBarriers[1].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
		finalBarriers[1].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		finalBarriers[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		finalBarriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		finalBarriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		finalBarriers[1].buffer = worldInstanceBuffers[frameIndex]->GetBuffer();
		finalBarriers[1].offset = 0;
		finalBarriers[1].size = VK_WHOLE_SIZE;

		finalBarriers[2].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
		finalBarriers[2].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		finalBarriers[2].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		finalBarriers[2].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		finalBarriers[2].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		finalBarriers[2].buffer = gpuWorldTransformBuffers[frameIndex]->GetBuffer();
		finalBarriers[2].offset = 0;
		finalBarriers[2].size = VK_WHOLE_SIZE;

		vkCmdPipelineBarrier(
			cmd,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
			0,
			0,
			nullptr,
			static_cast<uint32_t>(finalBarriers.size()),
			finalBarriers.data(),
			0,
			nullptr
		);
	}

	void VulkanIndexDraw::PrepareWorldDrawCommands(uint32_t frameIndex, VkCommandBuffer cmd)
	{
		if (cullMode != CullMode::GPU)
		{
			return;
		}

		std::vector<VkBufferCopy> copyRegions;
		copyRegions.reserve(3);

		if (gpuWorldStaticUploadPending && gpuWorldSceneInstanceCount > 0)
		{
			VkBufferCopy copy{};
			copy.srcOffset = 0;
			copy.dstOffset = 0;
			copy.size = static_cast<VkDeviceSize>(gpuWorldSceneInstanceCount) * sizeof(GpuWorldInstanceStaticData);
			vkCmdCopyBuffer(cmd, gpuWorldStaticStagingBuffer->GetBuffer(), gpuWorldStaticBuffer->GetBuffer(), 1, &copy);
			gpuWorldStaticUploadPending = false;
		}

		if (gpuWorldTemplateUploadPending && gpuCullDrawCommandCount > 0)
		{
			VkBufferCopy copy{};
			copy.srcOffset = 0;
			copy.dstOffset = 0;
			copy.size = static_cast<VkDeviceSize>(gpuCullDrawCommandCount) * sizeof(VkDrawIndexedIndirectCommand);
			vkCmdCopyBuffer(cmd, gpuCullCommandTemplateStagingBuffer->GetBuffer(), gpuCullCommandTemplateStaticBuffer->GetBuffer(), 1, &copy);
			gpuWorldTemplateUploadPending = false;
		}

		if (gpuWorldSceneInstanceCount > 0)
		{
			VkBufferCopy copy{};
			copy.srcOffset = 0;
			copy.dstOffset = 0;
			copy.size = static_cast<VkDeviceSize>(gpuWorldSceneInstanceCount) * sizeof(GpuWorldInstanceTransformData);
			vkCmdCopyBuffer(cmd, gpuWorldTransformStagingBuffers[frameIndex]->GetBuffer(), gpuWorldTransformBuffers[frameIndex]->GetBuffer(), 1, &copy);
		}

		std::array<VkBufferMemoryBarrier, 3> uploadBarriers{};
		uint32_t barrierCount = 0;

		if (gpuWorldSceneInstanceCount > 0)
		{
			uploadBarriers[barrierCount].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
			uploadBarriers[barrierCount].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			uploadBarriers[barrierCount].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			uploadBarriers[barrierCount].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			uploadBarriers[barrierCount].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			uploadBarriers[barrierCount].buffer = gpuWorldTransformBuffers[frameIndex]->GetBuffer();
			uploadBarriers[barrierCount].offset = 0;
			uploadBarriers[barrierCount].size = VK_WHOLE_SIZE;
			++barrierCount;
		}

		if (gpuCullDrawCommandCount > 0)
		{
			uploadBarriers[barrierCount].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
			uploadBarriers[barrierCount].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			uploadBarriers[barrierCount].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			uploadBarriers[barrierCount].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			uploadBarriers[barrierCount].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			uploadBarriers[barrierCount].buffer = gpuCullCommandTemplateStaticBuffer->GetBuffer();
			uploadBarriers[barrierCount].offset = 0;
			uploadBarriers[barrierCount].size = VK_WHOLE_SIZE;
			++barrierCount;
		}

		if (gpuWorldSceneInstanceCount > 0)
		{
			uploadBarriers[barrierCount].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
			uploadBarriers[barrierCount].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			uploadBarriers[barrierCount].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			uploadBarriers[barrierCount].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			uploadBarriers[barrierCount].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			uploadBarriers[barrierCount].buffer = gpuWorldStaticBuffer->GetBuffer();
			uploadBarriers[barrierCount].offset = 0;
			uploadBarriers[barrierCount].size = VK_WHOLE_SIZE;
			++barrierCount;
		}

		if (barrierCount > 0)
		{
			vkCmdPipelineBarrier(
				cmd,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
				0,
				0,
				nullptr,
				barrierCount,
				uploadBarriers.data(),
				0,
				nullptr
			);
		}

		if (gpuCullDrawCommandCount == 0 && !gpuCullCommandTemplates.empty())
		{
			gpuCullDrawCommandCount = static_cast<uint32_t>(gpuCullCommandTemplates.size());
		}

		DispatchGpuCull(frameIndex, cmd);
	}

	// Draws everything in world space that isn't decorated or requiring custom shaders that was processed into the command buffers via UploadAndBatchInstances()
	void VulkanIndexDraw::DrawIndexedWorldMeshes(uint32_t frameIndex, VkCommandBuffer cmd)
	{
		VkBuffer indirectBuf = (cullMode == CullMode::GPU)
			? gpuWorldIndirectCommandBuffers[frameIndex]->GetBuffer()
			: indirectCommandBuffers[frameIndex]->GetBuffer();
		VkDeviceSize offset = 0;
		VkBuffer vertexBuffer = megaVertexBuffer->GetBuffer();

		vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer, &offset);
		vkCmdBindIndexBuffer(cmd, megaIndexBuffer->GetBuffer(), 0, VK_INDEX_TYPE_UINT32);

		const uint32_t totalCommands = (cullMode == CullMode::GPU)
			? gpuCullDrawCommandCount
			: static_cast<uint32_t>(worldDrawCommands.size());

		if (totalCommands > 0)
		{
			vkCmdDrawIndexedIndirect(
				cmd,
				indirectBuf,
				0,
				totalCommands,
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
		const uint32_t baseDecoratorInstanceID = static_cast<uint32_t>(overlayInstanceData.size());
		const size_t baseInstanceID = overlayInstanceData.size();
		uint32_t instanceCount = baseDecoratorInstanceID;

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
		// overlayInstanceData only contains decorator and screen-space instances
		const size_t totalInstancesNeeded = overlayInstanceData.size();
		EnsureInstanceCapacity(*instanceBuffer, totalInstancesNeeded);

		// === Upload new instance data for section (append only the decorator range) ===
		void* dst = instanceBuffer->GetBufferRaw(frameIndex)->GetMappedPointer();

		// If we use aligned stride, replace sizeof(GpuInstanceData) with instanceBuffer->GetAlignedInstanceSize()
		std::memcpy(
			static_cast<uint8_t*>(dst) + baseInstanceID * sizeof(GpuInstanceData),
			overlayInstanceData.data() + baseInstanceID,
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
		VkBuffer vertexBuffer = megaVertexBuffer->GetBuffer();
		VkDeviceSize offset = 0;

		vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer, &offset);
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
			overlayInstanceData.push_back(instance);

			// === Draw command ===
			VkDrawIndexedIndirectCommand cmd{};
			cmd.indexCount = mesh.indexCount;
			cmd.instanceCount = 1;
			cmd.firstIndex = static_cast<uint32_t>(mesh.indexOffsetInMegaBuffer / sizeof(uint32_t));
			cmd.vertexOffset = static_cast<int32_t>(mesh.vertexOffsetInMegaBuffer / sizeof(Vertex));
			cmd.firstInstance = static_cast<uint32_t>(overlayInstanceData.size() - 1); // latest one
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
		DestroyGpuCullResources();

		if (instanceBuffer)
		{
			instanceBuffer->Cleanup();
			instanceBuffer.reset();
		}

		for (auto& buffer : worldInstanceBuffers)
		{
			if (buffer)
			{
				buffer->Free();
			}
		}
		worldInstanceBuffers.clear();

		for (auto& buffer : gpuWorldTransformBuffers)
		{
			if (buffer)
			{
				buffer->Free();
			}
		}
		gpuWorldTransformBuffers.clear();

		for (auto& buffer : gpuWorldTransformStagingBuffers)
		{
			if (buffer)
			{
				buffer->Free();
			}
		}
		gpuWorldTransformStagingBuffers.clear();

		for (auto& buffer : gpuWorldVisibleIndexBuffers)
		{
			if (buffer)
			{
				buffer->Free();
			}
		}
		gpuWorldVisibleIndexBuffers.clear();

		for (auto& buffer : gpuWorldIndirectCommandBuffers)
		{
			if (buffer)
			{
				buffer->Free();
			}
		}
		gpuWorldIndirectCommandBuffers.clear();

		if (gpuWorldStaticBuffer)
		{
			gpuWorldStaticBuffer->Free();
			gpuWorldStaticBuffer.reset();
		}

		if (gpuWorldStaticStagingBuffer)
		{
			gpuWorldStaticStagingBuffer->Free();
			gpuWorldStaticStagingBuffer.reset();
		}

		if (gpuCullCommandTemplateStaticBuffer)
		{
			gpuCullCommandTemplateStaticBuffer->Free();
			gpuCullCommandTemplateStaticBuffer.reset();
		}

		if (gpuCullCommandTemplateStagingBuffer)
		{
			gpuCullCommandTemplateStagingBuffer->Free();
			gpuCullCommandTemplateStagingBuffer.reset();
		}

		for (auto& buffer : indirectCommandBuffers)
		{
			if (buffer)
			{
				buffer->Free();
			}
		}
		indirectCommandBuffers.clear();

		for (auto& buffer : meshDecoratorIndirectCommandBuffers)
		{
			if (buffer)
			{
				buffer->Free();
			}
		}
		meshDecoratorIndirectCommandBuffers.clear();

		for (auto& buffer : msdfIndirectCommandBuffers)
		{
			if (buffer)
			{
				buffer->Free();
			}
		}
		msdfIndirectCommandBuffers.clear();

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

		cpuInstanceData.clear();
		overlayInstanceData.clear();
		meshDecoratorInstanceData.clear();
		msdfInstancesData.clear();
		gpuCullInputData.clear();
		gpuCullCommandTemplates.clear();
		gpuWorldSceneInstances.clear();
		gpuWorldStaticCpuData.clear();
		gpuWorldTransformCpuData.clear();
		culledVisibleData.clear();
		worldRenderableSlots.clear();
		worldRenderableFreeSlots.clear();
		worldRenderableKeyToSlot.clear();
		worldEntityToSlotIndices.clear();
	}


}
