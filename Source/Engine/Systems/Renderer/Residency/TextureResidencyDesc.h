#pragma once
#include <cstdint>
#include <string_view>

namespace Swim::Render
{
	struct TextureResidencyDesc
	{
		// Live + retiring GPU textures (the future bindless table size).
		std::uint32_t MaxTextures = 4096;
		std::string_view DebugName = "TextureResidency";
	};
} // namespace Swim::Render
