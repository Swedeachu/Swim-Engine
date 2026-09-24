#pragma once
#include <array>
#include <cstdint>

namespace Swim::Render
{
	// Visibility material bins Clustered Forward+ draws (GpuVisibility routes each
	// material set with SetMaterialBin; unmapped sets land in bin 0).
	enum class ForwardPlusBin : std::uint32_t
	{
		Opaque = 0,		 // Opaque and alpha-masked materials: depth-tested and written.
		Transparent = 1, // StandardPbr::FlagAlphaBlend: sorted back to front, blended.
	};

	inline constexpr std::uint32_t ForwardPlusBinCount = 2;

	// ForwardViewRecord::Flags.
	inline constexpr std::uint32_t ForwardViewFlagEnvironment = 1u << 0; // Image-based lighting is bound.
	inline constexpr std::uint32_t ForwardViewFlagShadows = 1u << 1;	 // A shadow atlas is bound (Phase 16).
	// A split-sum BRDF LUT is bound (always with an environment; item 76 also allows it
	// alone, for the specular reflectance screen-space reflections are weighted by).
	inline constexpr std::uint32_t ForwardViewFlagBrdfLut = 1u << 2;

	// ForwardViewRecord::DebugMode.
	enum class ForwardPlusDebugMode : std::uint32_t
	{
		None = 0,
		// Opaque pixels show their cluster's light-count heatmap color
		// (Clustering::HeatmapColor; black where the cluster is empty).
		ClusterHeatmap = 1,
	};

	// ForwardPlusRecords.slang's ForwardView (std430, 208 bytes): one per view.
	struct ForwardViewRecord
	{
		float ViewProjection[16] = {}; // Row-major rows (clip = M * world), unjittered.
		float CameraPosition[3] = {};
		float EnvironmentIntensity = 1.0f;
		float CameraForward[3] = { 0, 0, -1 }; // Unit; transparent sort depth axis.
		float EnvironmentRotation = 0.0f;	   // Radians around +Y.
		float Ambient[3] = {};				   // Constant ambient radiance.
		float Reserved0 = 0.0f;
		std::uint32_t MaterialCount = 0; // GpuMaterialTable rows; others use row 0.
		std::uint32_t PrefilteredMipCount = 1;
		std::uint32_t Flags = 0;
		std::uint32_t DebugMode = 0;
		// Item 75: the previous frame's unjittered view-projection (motion vectors) and
		// this frame's sub-pixel jitter as an NDC offset added to clip.xy / clip.w.
		float PreviousViewProjection[16] = {};
		float Jitter[2] = {};
		float Reserved1[2] = {};
	};

	static_assert(sizeof(ForwardViewRecord) == 208);

	// ForwardTransparentSort.slang's scratch entry (16 bytes).
	struct ForwardSortEntry
	{
		float Depth = 0.0f; // Along CameraForward; larger is farther and draws first.
		std::uint32_t InstanceRow = 0;
		std::uint32_t SubmeshRow = 0;
		std::uint32_t Slot = 0; // Command / draw-record slot in the visibility bin.
	};

	static_assert(sizeof(ForwardSortEntry) == 16);
} // namespace Swim::Render
