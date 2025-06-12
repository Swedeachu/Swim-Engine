#pragma once

#include <memory>
#include "Engine/SwimEngine.h"
#include "Engine/Systems/Renderer/Vulkan/Buffers/VulkanBuffer.h"
#include "Engine/Systems/Renderer/OpenGL/OpenGLBuffer.h"
#include "Vertex.h"

namespace Engine
{

	struct MeshBufferData
	{

		// For AABB culling (these are vec4s with a w component of 1 for the sake allignment when pushed onto the GPU)
		glm::vec4 aabbMin;
		glm::vec4 aabbMax;

		// Count of indices for rendering
		uint32_t indexCount = 0;

		// The offsets to use in the mega mesh buffer on the gpu
		// These are set in UploadMeshToMegaBuffer() when calling GenerateBuffersAndAABB()
		uint64_t vertexOffsetInMegaBuffer = 0;
		uint64_t indexOffsetInMegaBuffer = 0;

		// ID of the mesh used in the GPU
		uint32_t meshID = UINT32_MAX;

		uint32_t GetMeshID() const { return meshID; }

		GLuint GetIndexCount() const { return indexCount; }

		void GenerateBuffersAndAABB(const std::vector<Vertex>& vertices, const std::vector<uint16_t>& indices);

	};

}
