#include "PCH.h"
#include "VulkanIndexDraw.h"
#include "Engine/Components/Material.h"
#include "Engine/Components/CompositeMaterial.h"
#include "Engine/Components/Transform.h"
#include "Engine/Components/MeshDecorator.h"
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
		meshDecoratorIndirectCommandBuffers.resize(framesInFlight);

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
		decoratorParamData.clear();
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
		}
		else
		{
			// Render everything in world space with regular frustum culling (this is still CPU culling just not using the BVH)
			GatherCandidatesView(registry, TransformSpace::World, frustum);
		}

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
	// This only does world space entities as the BVH only takes world space objects into account.
	void VulkanIndexDraw::GatherCandidatesBVH(Scene& scene, const Frustum& frustum)
	{
		entt::registry& registry = scene.GetRegistry();

		scene.GetSceneBVH()->QueryFrustumCallback(frustum, [&](entt::entity entity)
		{
			// Skip decorators
			if (registry.any_of<MeshDecorator>(entity))
			{
				return;
			}

			const Transform& tf = registry.get<Transform>(entity);

			if (registry.all_of<Material>(entity))
			{
				const std::shared_ptr<MaterialData>& mat = registry.get<Material>(entity).data;
				AddInstance(tf, mat, nullptr);
			}
			else if (registry.all_of<CompositeMaterial>(entity))
			{
				const CompositeMaterial& composite = registry.get<CompositeMaterial>(entity);
				for (const std::shared_ptr<MaterialData>& mat : composite.subMaterials)
				{
					AddInstance(tf, mat, nullptr);
				}
			}
		});
	}

	// Passing space as TransformSpace::Ambiguous will just render all entities
	void VulkanIndexDraw::GatherCandidatesView(const entt::registry& registry, const TransformSpace space, const Frustum* frustum)
	{
		auto regularView = registry.view<Transform, Material>();
		for (auto entity : regularView)
		{
			const Transform& tf = regularView.get<Transform>(entity);
			if (space == TransformSpace::Ambiguous || tf.GetTransformSpace() == space)
			{
				const auto& mat = regularView.get<Material>(entity).data;
				AddInstance(tf, mat, frustum);
			}
		}

		auto compositeView = registry.view<Transform, CompositeMaterial>();
		for (auto entity : compositeView)
		{
			const Transform& tf = compositeView.get<Transform>(entity);
			if (space == TransformSpace::Ambiguous || tf.GetTransformSpace() == space)
			{
				const auto& composite = compositeView.get<CompositeMaterial>(entity);
				for (const auto& mat : composite.subMaterials)
				{
					AddInstance(tf, mat, frustum);
				}
			}
		}
	}

	void VulkanIndexDraw::AddInstance(const Transform& transform, const std::shared_ptr<MaterialData>& mat, const Frustum* frustum)
	{
		const std::shared_ptr<Mesh>& mesh = mat->mesh;

		const glm::vec4& min = mesh->meshBufferData->aabbMin;
		const glm::vec4& max = mesh->meshBufferData->aabbMax;

		// Frustum culling if world-space
		if (frustum && transform.GetTransformSpace() == TransformSpace::World)
		{
			if (!frustum->IsVisibleLazy(min, max, transform.GetModelMatrix()))
			{
				return;
			}
		}

		GpuInstanceData instance{};

		instance.space = static_cast<uint32_t>(transform.GetTransformSpace());
		instance.model = transform.GetModelMatrix();
		instance.aabbMin = min;
		instance.aabbMax = max;
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

	// Draws everything in world space that isn't decorated or requiring custom shaders that was processed into the command buffers via UploadAndBatchInstances()
	void VulkanIndexDraw::DrawIndexedWorldMeshes(uint32_t frameIndex, VkCommandBuffer cmd)
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

	// Draws everything that is in screen space or has a decorator on it, meaning this can render billboards in world space.
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
		decoratorParamData.clear(); // clear previous frame's params

		DrawDecoratorsAndScreenSpaceEntitiesInRegistry(
			registry,
			cameraUBO,
			worldView,
			windowHeight,
			windowHeight,
			frustum,
			instanceCount,
			drawCommands,
			true // run culling
		);

		// Complete hack to inject in the decorator wireframe meshes
	#ifdef _DEBUG
		SceneDebugDraw* debugDraw = scene->GetSceneDebugDraw();
		if (debugDraw && debugDraw->IsEnabled())
		{
			DrawDecoratorsAndScreenSpaceEntitiesInRegistry(
				debugDraw->GetRegistry(),
				cameraUBO,
				worldView,
				windowHeight,
				windowHeight,
				frustum,
				instanceCount,
				drawCommands,
				false // no culling
			);
		}
	#endif

		if (drawCommands.empty())
		{
			return;
		}

		// === Upload MeshDecoratorGpuInstanceData via descriptorManager ===
		descriptorManager->UpdatePerFrameMeshDecoratorBuffer(
			frameIndex,
			decoratorParamData.data(),
			decoratorParamData.size() * sizeof(MeshDecoratorGpuInstanceData)
		);

		// === Upload new instance data for section ===
		void* dst = instanceBuffer->GetBufferRaw(frameIndex)->GetMappedPointer();
		std::memcpy(
			static_cast<uint8_t*>(dst) + baseInstanceID * sizeof(GpuInstanceData),
			cpuInstanceData.data() + baseInstanceID,
			decoratorParamData.size() * sizeof(GpuInstanceData)
		);

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
			megaVertexBuffer->GetBuffer(),
			instanceBuffer->GetBuffer(frameIndex)
		};
		VkDeviceSize offsets[] = { 0, 0 };

		vkCmdBindVertexBuffers(cmd, 0, 2, vertexBuffers, offsets);
		vkCmdBindIndexBuffer(cmd, megaIndexBuffer->GetBuffer(), 0, VK_INDEX_TYPE_UINT16);

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

			const glm::vec3& pos = transform.GetPosition(); // In virtual canvas units
			const glm::vec3& scale = transform.GetScale();  // Width & height in virtual canvas units

			const std::shared_ptr<MaterialData>& mat = matComp.data;
			const MeshBufferData& mesh = *mat->mesh->meshBufferData;
			const glm::mat4& model = transform.GetModelMatrix();

			// First do a simple cull check 
			if (cull)
			{
				if (space == TransformSpace::World)
				{
					if (!frustum.IsVisibleLazy(matComp.data->mesh->meshBufferData->aabbMin, matComp.data->mesh->meshBufferData->aabbMax, model))
					{
						return;
					}
				}
				else
				{
					// Sceen space 2D check using window width and height

					glm::vec2 halfSize = glm::vec2(scale) * 0.5f;
					glm::vec2 center = glm::vec2(pos) * screenScale;
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
			instance.indexCount = mesh.indexCount;
			instance.indexOffsetInMegaBuffer = mesh.indexOffsetInMegaBuffer;
			instance.vertexOffsetInMegaBuffer = mesh.vertexOffsetInMegaBuffer;
			instance.meshInfoIndex = mesh.GetMeshID();

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
			}

			data.resolution = glm::vec2(windowWidth, windowHeight);
			data.quadSize = quadSizeInPixels;

			// Finally add
			decoratorParamData.push_back(data);
			cpuInstanceData.push_back(instance);

			// === Draw command ===
			VkDrawIndexedIndirectCommand cmd{};
			cmd.indexCount = mesh.indexCount;
			cmd.instanceCount = 1;
			cmd.firstIndex = static_cast<uint32_t>(mesh.indexOffsetInMegaBuffer / sizeof(uint16_t));
			cmd.vertexOffset = static_cast<int32_t>(mesh.vertexOffsetInMegaBuffer / sizeof(Vertex));
			cmd.firstInstance = static_cast<uint32_t>(cpuInstanceData.size() - 1); // latest one
			drawCommands.push_back(cmd);

			instanceCount++;
		});
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
		decoratorParamData.clear();
		culledVisibleData.clear();
	}

}
