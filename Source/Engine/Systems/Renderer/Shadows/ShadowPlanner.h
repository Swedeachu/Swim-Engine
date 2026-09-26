#pragma once
#include "Engine/Systems/Renderer/Lights/GpuLightRecord.h"
#include "Engine/Systems/Renderer/Shadows/ShadowAtlasAllocator.h"
#include "Engine/Systems/Renderer/Shadows/ShadowMath.h"
#include "Engine/Systems/Renderer/Shadows/ShadowRecords.h"
#include "Engine/Systems/Renderer/Visibility/RenderViewDesc.h"

#include <cstdint>
#include <span>
#include <vector>

namespace Swim::Render
{
	// Shadow quality and cost controls (Phase 16, items 70-72).
	struct ShadowSettings
	{
		std::uint32_t AtlasSize = 4096; // One D32 atlas holds every shadow view.
		std::uint32_t MinTile = 128;	// Smallest tile a downgraded request may get.
		std::uint32_t MaxSlots = 64;	// GpuShadowRecord slots (GpuLightRecord::ShadowIndex range).

		Shadows::CascadeSettings Cascades;
		std::uint32_t CascadeResolution = 2048; // Per cascade tile.
		// GpuShadowRecord::CascadeBlend: the far fraction of each cascade's depth range that
		// cross-fades into the next (and fades the last one out), hiding the seams.
		float CascadeBlend = 0.2f;
		std::uint32_t SpotResolution = 512;
		std::uint32_t PointResolution = 256; // Per cube face.

		// Budgets: casters beyond these (lowest priority first) get no shadow this frame.
		std::uint32_t MaxDirectionalShadows = 1;
		std::uint32_t MaxSpotShadows = 16;
		std::uint32_t MaxPointShadows = 2; // Point shadows cost six views each.

		float Near = 0.05f; // Spot and point near plane.
		float NormalBias = 1.0f;
		float SlopeBias = 1.5f;
		float DepthBias = 0.0f;
		std::uint32_t PcfRadius = 1;
	};

	// A light that wants a shadow this frame. Light.ShadowIndex must equal Slot and
	// Light.Flags must include LightFlags::CastsShadows (what shading checks).
	struct ShadowCasterDesc
	{
		std::uint32_t Slot = 0;
		GpuLightRecord Light;
		float Priority = 0.0f;		  // Larger first (budgets, atlas placement).
		std::uint32_t Resolution = 0; // Power of two; 0 = the settings' default for the type.
	};

	// One tile to render: its GpuShadowView, the visibility view that culls its
	// casters (GpuViewFlags::ShadowCasters) and the atlas tile.
	struct ShadowViewPlan
	{
		GpuShadowView View;
		RenderViewDesc Visibility;
		ShadowTile Tile;
		std::uint32_t Slot = 0;
		ShadowKind Kind = ShadowKind::None;
		std::uint32_t Index = 0; // Cascade or cube face.
	};

	struct ShadowPlanStats
	{
		std::uint32_t Casters = 0;
		std::uint32_t OverBudget = 0; // Dropped by MaxDirectional/Spot/PointShadows.
		std::uint32_t Placed = 0;
		std::uint32_t Reused = 0;
		std::uint32_t Downgraded = 0;
		std::uint32_t Evicted = 0; // No atlas room even at MinTile.
		std::uint32_t Views = 0;
	};

	struct ShadowPlan
	{
		std::uint32_t AtlasSize = 0;
		std::vector<GpuShadowRecord> Records; // MaxSlots; ShadowKind::None where unshadowed.
		std::vector<GpuShadowView> Views;	  // Records' FirstView/ViewCount index these.
		std::vector<ShadowViewPlan> Draws;	  // Same order as Views.
		ShadowPlanStats Stats;
	};

	// Budgets the casters by priority, places their tiles (stable across frames
	// through `atlas`) and builds every record and view: cascades fitted to the camera
	// for directional lights, one cone view for spots, six cube faces for points.
	// Throws std::invalid_argument for a slot out of range or repeated, a light whose
	// ShadowIndex/CastsShadows do not match, a non-power-of-two resolution, or an atlas
	// of another size than the settings.
	ShadowPlan PlanShadows(const ShadowSettings& settings, const Shadows::ShadowCamera& camera, std::span<const ShadowCasterDesc> casters,
		ShadowAtlasAllocator& atlas);
} // namespace Swim::Render
