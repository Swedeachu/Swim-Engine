#pragma once
#include <cstddef>
#include <cstdint>

namespace Swim::Render
{
	enum class GpuViewFlags : std::uint32_t
	{
		None = 0,
		DisableFrustumCulling = 1u << 0,
		// Camera cut/teleport: ignore every object's previous LOD this frame.
		ResetLodHistory = 1u << 1,
	};

	// One std430 view for GPU visibility (Shaders/Slang/GpuScene/GpuVisibility.slang).
	// Build it with BuildGpuViewRecord; the planes are derived from ViewProjection.
	struct GpuViewRecord
	{
		float ViewProjection[16] = {}; // Row-major rows: clip = dot(row, float4(world, 1)).
		float FrustumPlanes[24] = {};  // Six normalized planes (x, y, z, d); inside when dot >= 0.
		float CameraPosition[3] = {};
		float LodScale = 1.0f;		 // Pixels per world unit at distance 1 (viewport height / (2 tan(fovY / 2))).
		float LodPixelError = 1.0f;	 // Coarsest LOD whose projected error stays within this many pixels.
		float LodHysteresis = 0.25f; // Relative band around the threshold that keeps the previous LOD.
		std::uint32_t Flags = 0;	 // GpuViewFlags.
		std::uint32_t Reserved = 0;
	};

	static_assert(sizeof(GpuViewRecord) == 192);
	static_assert(offsetof(GpuViewRecord, CameraPosition) == 160);
} // namespace Swim::Render
