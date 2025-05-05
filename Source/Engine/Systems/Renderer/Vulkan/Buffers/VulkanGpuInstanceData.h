#pragma once

#include "Library/glm/glm.hpp"

namespace Engine
{

	struct GpuInstanceData
	{
		glm::mat4 model;         // 64 bytes
		uint32_t textureIndex;   //  4
		float hasTexture;        //  4
		float padA;              //  4
		float padB;              //  4
		uint32_t meshIndex;      //  4
		float padC;              //  4
		float padD;              //  4
		float padE;              //  4 -> Total = 96 bytes
	};

}
