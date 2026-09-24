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
		// Camera cut/teleport: treat every in-frustum object as visible last frame, so
		// the early phase draws them all and nothing is occluded by stale history.
		ResetOcclusionHistory = 1u << 2,
		// Depth is conventional (near 0, far 1) rather than the canonical reverse-Z.
		ForwardDepth = 1u << 3,
		// The late phase draws every remaining in-frustum object (HZB test skipped).
		DisableOcclusion = 1u << 4,
		// Shadow views: only objects with RenderObjectFlags::CastShadows are drawable;
		// the rest count as NotDrawable (Phase 16 GPU caster culling).
		ShadowCasters = 1u << 5,
		// Both histories reset: what a camera cut or teleport sets.
		CameraCut = ResetLodHistory | ResetOcclusionHistory,
	};

	constexpr std::uint32_t operator|(GpuViewFlags left, GpuViewFlags right)
	{
		return static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right);
	}

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
