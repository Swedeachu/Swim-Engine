#pragma once

#include "Library/glm/glm.hpp"

namespace Engine
{

  struct alignas(16) GpuInstanceData
  {
    glm::mat4 model;          // 64 bytes

    glm::vec4 aabbMin;        // 16 bytes
    glm::vec4 aabbMax;        // 16 bytes

    uint32_t textureIndex;    // 4 bytes
    float hasTexture;         // 4 bytes (maybe should be a uint for consistency)
    uint32_t meshInfoIndex;   // 4 bytes  
    uint32_t materialIndex;   // 4 bytes  

    uint32_t indexCount;      // 4 bytes
    uint32_t space;           // 4 bytes (0 = world, 1 = screen)
    VkDeviceSize vertexOffsetInMegaBuffer; // 8 bytes
    VkDeviceSize indexOffsetInMegaBuffer;  // 8 bytes
  };

	struct alignas(16) UIParams
	{
		glm::vec4 fillColor;
		glm::vec4 strokeColor;
		glm::vec2 strokeWidth;
		glm::vec2 cornerRadius;
		int enableFill;
		int enableStroke;
		int roundCorners;
		int useTexture;
		glm::vec2 resolution;
		glm::vec2 quadSize;
	};

	struct InstanceMeta
	{
		uint32_t instanceCount;
		uint32_t padA;
		uint32_t padB;
		uint32_t padC;
	};

}
