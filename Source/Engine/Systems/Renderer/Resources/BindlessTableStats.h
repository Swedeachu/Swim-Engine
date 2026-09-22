#pragma once
#include <cstdint>

namespace Swim::Render
{
	struct BindlessTableStats
	{
		std::uint32_t LiveTextures = 0; // Including the permanent fallback element.
		std::uint32_t RetiringTextures = 0;
		std::uint32_t TextureCapacity = 0;
		std::uint32_t LiveSamplers = 0; // Including the permanent fallback element.
		std::uint32_t RetiringSamplers = 0;
		std::uint32_t SamplerCapacity = 0;
		std::uint64_t DescriptorWrites = 0; // Elements written, fallback rewrites included.
	};
} // namespace Swim::Render
