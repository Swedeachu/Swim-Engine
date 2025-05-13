#pragma once

#include "Library/glm/glm.hpp"

namespace Engine
{

	struct alignas(16) GpuInstanceData
	{
		glm::mat4 model;         // 64 bytes

		glm::vec4 aabbMin;       // 16 bytes
		glm::vec4 aabbMax;       // 16 bytes

		uint32_t textureIndex;   // 4 bytes
		float hasTexture;        // 4 bytes
		uint32_t meshInfoIndex;  // 4 bytes  
		uint32_t materialIndex;  // 4 bytes  
	};

	struct InstanceMeta
	{
		uint32_t instanceCount;
		uint32_t padA;
		uint32_t padB;
		uint32_t padC;
	};

}
