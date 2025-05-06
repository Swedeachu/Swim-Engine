#pragma once

#include "Library/glm/glm.hpp"

namespace Engine
{

  struct alignas(16) GpuInstanceData
  {
    glm::mat4 model;      ///< 64 B – world matrix

    /* Local-space bounding box ------------------------------------------ */
    glm::vec4 aabbMin;    ///< 16 B – xyz = min, w unused
    glm::vec4 aabbMax;    ///< 16 B – xyz = max, w unused

    /* Rendering miscellany --------------------------------------------- */
    uint32_t  textureIndex; ///< bindless index or UINT32_MAX
    float     hasTexture;   ///< 1 = yes, 0 = no (can be alpha value later)     
    uint32_t  meshIndex;    ///< ID -> MeshPool
    uint32_t  pad;          ///< keeps struct multiple-of-16
  };

	struct InstanceMeta
	{
		uint32_t instanceCount;
		uint32_t padA;
		uint32_t padB;
		uint32_t padC;
	};

}
