#pragma once
#include "Engine/Systems/Renderer/Environment/EnvironmentMath.h"

#include <cstdint>
#include <span>
#include <vector>

namespace Swim::Render::Environment
{
	using Float4 = std::array<float, 4>;

	// A CPU cube map with a mip chain: RGBA float texels, per mip then per face, rows
	// top to bottom. It is the reference the GPU environment passes are checked
	// against, and it holds GPU results read back for comparison.
	class CubeImage
	{
	  public:
		CubeImage() = default;
		// Size must be a power of two; mipCount 0 means the full chain down to 1x1.
		CubeImage(std::uint32_t size, std::uint32_t mipCount = 0);

		std::uint32_t GetSize() const { return size; }

		std::uint32_t GetMipCount() const { return mipCount; }

		std::uint32_t GetMipSize(std::uint32_t mip) const;

		Float4& Texel(std::uint32_t mip, std::uint32_t face, std::uint32_t x, std::uint32_t y);
		const Float4& Texel(std::uint32_t mip, std::uint32_t face, std::uint32_t x, std::uint32_t y) const;
		std::span<Float4> Face(std::uint32_t mip, std::uint32_t face);
		std::span<const Float4> Face(std::uint32_t mip, std::uint32_t face) const;

		// Point sample: the nearest mip (Vulkan rule: ceil(lod + 0.5) - 1) and the texel containing the direction.
		Float4 SampleNearest(const Float3& direction, float lod) const;
		// Trilinear sample with Vulkan's seamless cube filtering: bilinear footprints
		// that leave a face fetch the adjacent face's edge texels, and a corner texel
		// is the average of the three texels meeting there; linear between mips.
		Float4 SampleTrilinear(const Float3& direction, float lod) const;

		// Recomputes mips 1.. as 2x2 box averages of the mip above (EnvironmentDownsample.slang).
		void GenerateMips();

		// Texel (x, y) of a face where coordinates one step outside the face resolve
		// to the adjacent face (seamless filtering's footprint).
		Float4 FetchSeamless(std::uint32_t mip, std::uint32_t face, int x, int y) const;

	  private:
		Float4 SampleBilinear(std::uint32_t mip, const CubeCoordinate& coordinate) const;
		Float4 FetchAcrossEdge(std::uint32_t mip, std::uint32_t face, int x, int y) const;

		std::uint32_t size = 0;
		std::uint32_t mipCount = 0;
		std::vector<std::size_t> mipOffsets;
		std::vector<Float4> texels;
	};

	std::uint32_t FullCubeMipCount(std::uint32_t size);
	// Environment source cubes stop at 4x4 faces: the prefilter's filtered importance
	// sampling never reads the corner-dominated 2x2 and 1x1 levels.
	std::uint32_t EnvironmentSourceMipCount(std::uint32_t size);
	bool IsPowerOfTwo(std::uint32_t value);

	// A 2D RGBA float image (the BRDF LUT), rows top to bottom.
	struct Image2D
	{
		std::uint32_t Width = 0;
		std::uint32_t Height = 0;
		std::vector<Float4> Texels;

		const Float4& At(std::uint32_t x, std::uint32_t y) const { return Texels[std::size_t(y) * Width + x]; }

		// Bilinear with clamp-to-edge at normalized coordinates (texel centers at (i + 0.5) / size).
		Float4 SampleBilinear(float u, float v) const;
	};
} // namespace Swim::Render::Environment
