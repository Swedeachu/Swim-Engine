#pragma once
#include "Engine/Systems/Renderer/ForwardPlus/StandardVertex.h"
#include "Engine/Systems/Renderer/GpuScene/RenderBounds.h"
#include "Engine/Systems/Renderer/Skinning/SkinningRecords.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace Swim::Render
{
	// Up to four joint influences of one vertex. Weights should sum to 1 (the asset
	// compiler normalizes them); zero weights are skipped.
	struct SkinInfluence
	{
		std::array<std::uint16_t, 4> Joints{ 0, 0, 0, 0 };
		std::array<float, 4> Weights{ 1.0f, 0.0f, 0.0f, 0.0f };
	};

	// One blend shape as dense per-vertex deltas (each span empty or one entry per
	// vertex); tangent deltas move xyz only.
	struct SkinnedMorphTarget
	{
		std::span<const std::array<float, 3>> Positions;
		std::span<const std::array<float, 3>> Normals;
		std::span<const std::array<float, 3>> Tangents;
	};

	using SkinMatrix = std::array<float, 12>; // Row-major 3x4 (Animation::Matrix3x4, RenderAffine rows).

	// Per-joint bind-space boxes of the vertices each joint moves, plus each morph
	// target's largest displacement of those vertices: enough to bound any pose.
	struct SkinnedBoundsData
	{
		std::vector<std::array<float, 3>> JointMin; // Per joint; min > max when the joint moves nothing.
		std::vector<std::array<float, 3>> JointMax;
		std::vector<float> MorphReach; // [target * JointCount + joint]: largest |position delta|.
		std::uint32_t JointCount = 0;
		std::uint32_t MorphTargetCount = 0;
	};

	// A mesh's GPU source rows: influences with per-vertex morph delta ranges
	// (relative to the mesh's first delta) and the sparse deltas themselves.
	struct SkinnedSource
	{
		std::vector<GpuSkinVertex> SkinVertices;
		std::vector<GpuMorphDelta> MorphDeltas;
		SkinnedBoundsData Bounds;
	};

	namespace Skinning
	{
		// Validates and packs influences and morph targets (throws std::invalid_argument
		// for mismatched counts, joints >= jointCount or non-finite data). Morph deltas
		// whose nine components are all zero are dropped.
		SkinnedSource BuildSource(std::span<const StandardVertex> vertices, std::span<const SkinInfluence> influences,
			std::span<const SkinnedMorphTarget> targets, std::uint32_t jointCount);

		// The CPU definition of SwimSkinning for one vertex: morphs in bind space, then
		// linear blend skinning. Position by the blended 3x4; normal by its cofactor
		// (like ForwardPlus::TransformNormal), normalized; tangent xyz by its 3x3,
		// normalized, w kept. Zero-length results keep the source direction.
		// `deltas` are the vertex's own rows; palette/weights are one pose's.
		StandardVertex SkinVertex(const StandardVertex& source, const GpuSkinVertex& skin, std::span<const GpuMorphDelta> deltas,
			std::span<const SkinMatrix> palette, std::span<const float> morphWeights);
		// The same vertex's position under another pose (the previous frame's).
		std::array<float, 3> SkinPosition(const StandardVertex& source, const GpuSkinVertex& skin, std::span<const GpuMorphDelta> deltas,
			std::span<const SkinMatrix> palette, std::span<const float> morphWeights);

		// Conservative local bounds of the skinned mesh under a pose: each vertex is a
		// convex combination of its joints' transforms of its morphed bind position,
		// so the union of the transformed (morph-expanded) joint boxes contains it.
		RenderBounds ComputeBounds(const SkinnedBoundsData& data, std::span<const SkinMatrix> palette, std::span<const float> morphWeights);

		inline std::uint16_t Joint(const GpuSkinVertex& skin, std::uint32_t slot)
		{
			return static_cast<std::uint16_t>((skin.Joints[slot / 2] >> ((slot % 2) * 16)) & 0xffffu);
		}
	} // namespace Skinning
} // namespace Swim::Render
