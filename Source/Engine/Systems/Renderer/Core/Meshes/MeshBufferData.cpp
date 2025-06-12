#include "PCH.h"
#include "MeshBufferData.h"
#include "Engine/Systems/Renderer/Vulkan/VulkanRenderer.h"
#include "Engine/Systems/Renderer/OpenGL/OpenGLRenderer.h"

namespace Engine
{

	void MeshBufferData::GenerateBuffersAndAABB(const std::vector<Vertex>& vertices, const std::vector<uint16_t>& indices)
	{
		indexCount = static_cast<uint32_t>(indices.size());

		if constexpr (SwimEngine::CONTEXT == SwimEngine::RenderContext::Vulkan)
		{
			SwimEngine::GetInstance()->GetVulkanRenderer()->GetIndexDraw()->UploadMeshToMegaBuffer(
				vertices,
				indices,
				*this
			);
		}
		else if constexpr (SwimEngine::CONTEXT == SwimEngine::RenderContext::OpenGL)
		{
			SwimEngine::GetInstance()->GetOpenGLRenderer()->UploadMeshToMegaBuffer(
				vertices,
				indices,
				*this
			);
		}

		// Calculate the meshes AABB
		aabbMin = glm::vec4(std::numeric_limits<float>::max());
		aabbMax = glm::vec4(std::numeric_limits<float>::lowest());

		// Update the min/max using only xyz parts
		for (const auto& v : vertices)
		{
			aabbMin.x = std::min(aabbMin.x, v.position.x);
			aabbMin.y = std::min(aabbMin.y, v.position.y);
			aabbMin.z = std::min(aabbMin.z, v.position.z);

			aabbMax.x = std::max(aabbMax.x, v.position.x);
			aabbMax.y = std::max(aabbMax.y, v.position.y);
			aabbMax.z = std::max(aabbMax.z, v.position.z);
		}

		// Ensure w = 1 for alignment consistency
		aabbMin.w = 1.0f;
		aabbMax.w = 1.0f;
	}

}
