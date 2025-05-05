#pragma once

#include "Library/glm/glm.hpp"

namespace Engine
{

	struct GpuInstanceData
	{
		glm::mat4 model;
		uint32_t textureIndex; // index into bindless texture array
		float hasTexture; // kinda scuffed flag, might be used for transparency later beyond just a "use texture" flag
		// Pad to 16 bytes
		float padA;
		float padB;
	};

}
