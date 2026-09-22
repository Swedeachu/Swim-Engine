#pragma once
#include <cstdint>

namespace Swim::Render
{
	struct TextureResidencyStats
	{
		std::uint32_t PendingTextures = 0;
		std::uint32_t RecordedTextures = 0;
		std::uint32_t UploadingTextures = 0;
		std::uint32_t ResidentTextures = 0;
		std::uint32_t RetiringTextures = 0;
		std::uint64_t PendingUploadBytes = 0;
		std::uint64_t ResidentBytes = 0; // Tightly packed texel bytes of resident textures.
	};
} // namespace Swim::Render
