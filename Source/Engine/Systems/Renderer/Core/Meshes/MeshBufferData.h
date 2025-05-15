#pragma once

#include <memory>
#include "Engine/SwimEngine.h"
#include "Engine/Systems/Renderer/Vulkan/Buffers/VulkanBuffer.h"
#include "Engine/Systems/Renderer/OpenGL/OpenGLBuffer.h"

namespace Engine
{

	struct MeshBufferData
	{

		// Vulkan buffers
		std::unique_ptr<VulkanBuffer> vertexBuffer;
		std::unique_ptr<VulkanBuffer> indexBuffer;

		// For compute shader AABB culling
		glm::vec3 aabbMin;
		glm::vec3 aabbMax;

		// OpenGL buffer
		std::unique_ptr<OpenGLBuffer> glBuffer;

		// Count of indices for rendering
		uint32_t indexCount = 0;

		// The offsets to use in the mega mesh buffer on the gpu
		// These are set in VulkanIndexDraw::UploadMeshToMegaBuffer()
		VkDeviceSize vertexOffsetInMegaBuffer = 0;
		VkDeviceSize indexOffsetInMegaBuffer = 0;

		// ID of the mesh used in the GPU
		uint32_t meshID = UINT32_MAX;

		GLuint GetGLVAO() const
		{
			if (glBuffer) { return glBuffer->GetVAO(); }
			return 0;
		}

		uint32_t GetMeshID() const { return meshID; }

		GLuint GetIndexCount() const { return indexCount; }

		// GPU buffer generation, you don't need to pass devices if you are not in a vulkan context
		void GenerateBuffers(const std::vector<Vertex>& vertices, const std::vector<uint16_t>& indices, VkDevice vkDevice = {}, VkPhysicalDevice vkPhysicalDevice = {})
		{
			indexCount = static_cast<uint32_t>(indices.size());

			if constexpr (SwimEngine::CONTEXT == SwimEngine::RenderContext::Vulkan)
			{
				vertexBuffer = std::make_unique<VulkanBuffer>(
					vkDevice, vkPhysicalDevice,
					vertices.size() * sizeof(Vertex),
					VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
					VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
				);
				vertexBuffer->CopyData(vertices.data(), vertices.size() * sizeof(Vertex));

				indexBuffer = std::make_unique<VulkanBuffer>(
					vkDevice, vkPhysicalDevice,
					indices.size() * sizeof(uint16_t),
					VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
					VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
				);
				indexBuffer->CopyData(indices.data(), indices.size() * sizeof(uint16_t));
			}
			else if constexpr (SwimEngine::CONTEXT == SwimEngine::RenderContext::OpenGL)
			{
				glBuffer = std::make_unique<OpenGLBuffer>();
				glBuffer->Create(vertices.data(), vertices.size() * sizeof(Vertex), indices.data(), indices.size() * sizeof(uint16_t));
				indexCount = glBuffer->GetIndexCount(); // sync afterwards
			}

			// Calculate the meshes AABB
			aabbMin = glm::vec3(std::numeric_limits<float>::max());
			aabbMax = glm::vec3(std::numeric_limits<float>::lowest());

			for (const auto& v : vertices)
			{
				aabbMin = glm::min(aabbMin, v.position);
				aabbMax = glm::max(aabbMax, v.position);
			}
		}

		// Free buffers based on render context being used
		void Free()
		{
			if constexpr (SwimEngine::CONTEXT == SwimEngine::RenderContext::Vulkan)
			{
				if (vertexBuffer)
				{
					vertexBuffer->Free();
					vertexBuffer.reset();
				}
				if (indexBuffer)
				{
					indexBuffer->Free();
					indexBuffer.reset();
				}
			}
			else if constexpr (SwimEngine::CONTEXT == SwimEngine::RenderContext::OpenGL)
			{
				if (glBuffer)
				{
					glBuffer->Free();
					glBuffer.reset();
				}
			}
		}

	};

}
