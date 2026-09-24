#pragma once
#include <cstdint>

namespace Swim::Render
{
	// What a GpuShadowRecord describes (Phase 16).
	enum class ShadowKind : std::uint32_t
	{
		None = 0,		 // No shadow this frame (unallocated or evicted): the light is unshadowed.
		Directional = 1, // Cascaded shadow maps, one view per cascade.
		Spot = 2,		 // One perspective view.
		Point = 3,		 // Six 90-degree cube-face views.
	};

	inline constexpr std::uint32_t MaxShadowCascades = 4;

	// ShadowRecords.slang's GpuShadowRecord (std430, 64 bytes). A light whose
	// GpuLightRecord::ShadowIndex names slot s (and that has LightFlags::CastsShadows)
	// is shadowed by record s; lighting never sees how the views were produced.
	struct GpuShadowRecord
	{
		std::uint32_t Kind = 0; // ShadowKind.
		std::uint32_t FirstView = 0;
		std::uint32_t ViewCount = 0;
		std::uint32_t PcfRadius = 1; // (2R + 1)^2 texel comparisons.
		float NormalBias = 1.0f;	 // Normal offset in shadow texels.
		float SlopeBias = 1.0f;		 // Extra texels of normal offset at grazing light (1 - N.L).
		float DepthBias = 0.0f;		 // Added to the receiver's shadow-map depth (reverse-Z: toward the light).
		float Reserved0 = 0.0f;
		float CascadeFar[MaxShadowCascades] = {}; // Directional: camera view depth where cascade i ends.
		float LightPosition[3] = {};			  // Point: the light (cube-face selection).
		float Reserved1 = 0.0f;
	};

	static_assert(sizeof(GpuShadowRecord) == 64);

	// ShadowRecords.slang's GpuShadowView (std430, 96 bytes): one rendered shadow-map tile.
	struct GpuShadowView
	{
		float ViewProjection[16] = {}; // Row-major; reverse-Z (near 1, far 0).
		float AtlasRect[4] = {};	   // Tile in atlas pixels: x, y, size, size.
		float TexelWorldSize = 0.0f;   // Ortho: world size of one texel. Perspective: per unit distance.
		std::uint32_t Perspective = 0;
		float Reserved[2] = {};
	};

	static_assert(sizeof(GpuShadowView) == 96);
} // namespace Swim::Render
