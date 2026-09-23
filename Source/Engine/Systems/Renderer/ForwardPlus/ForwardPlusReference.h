#pragma once
#include "Engine/Systems/Renderer/ClusteredLighting/ClusterGrid.h"
#include "Engine/Systems/Renderer/Environment/EnvironmentReference.h"
#include "Engine/Systems/Renderer/ForwardPlus/ForwardPlusRecords.h"
#include "Engine/Systems/Renderer/GpuScene/GpuInstanceRecord.h"
#include "Engine/Systems/Renderer/GpuScene/GpuTransformRecord.h"
#include "Engine/Systems/Renderer/Lights/GpuLightRecord.h"
#include "Engine/Systems/Renderer/Materials/StandardPbr.h"
#include "Engine/Systems/Renderer/Visibility/GpuDrawRecord.h"

#include <array>
#include <span>
#include <vector>

namespace Swim::Render
{
	// Per-view inputs of a Forward+ frame (BuildForwardViewRecord packs them).
	struct ForwardPlusView
	{
		std::array<float, 16> ViewProjection{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 }; // Row-major.
		std::array<float, 3> CameraPosition{ 0, 0, 0 };
		std::array<float, 3> CameraForward{ 0, 0, -1 }; // Normalized when packed.
		std::array<float, 3> Ambient{ 0, 0, 0 };
		float EnvironmentIntensity = 1.0f;
		float EnvironmentRotation = 0.0f;
		ForwardPlusDebugMode DebugMode = ForwardPlusDebugMode::None;
	};

	// Throws std::invalid_argument for a non-finite matrix/position, a zero forward
	// vector, negative ambient or intensity, or an unknown debug mode.
	ForwardViewRecord BuildForwardViewRecord(
		const ForwardPlusView& view, std::uint32_t materialCount, std::uint32_t prefilteredMipCount, bool hasEnvironment);
} // namespace Swim::Render

namespace Swim::Render::ForwardPlus
{
	// The CPU definition of Clustered Forward+ shading (items 66-67).
	// Shaders/Slang/ForwardPlus mirrors every function; the native Forward+ smoke
	// compares the two pixel by pixel.
	using Float3 = std::array<float, 3>;
	using Float4 = std::array<float, 4>;

	// Which visibility bin a material draws in: FlagAlphaBlend -> Transparent, the
	// rest (opaque, alpha-masked, double-sided) -> Opaque.
	ForwardPlusBin MaterialBin(const StandardPbr::Parameters& parameters);

	// GpuTransformRecord rows (row-major 3x4 affine).
	Float3 TransformPoint(const float (&rows)[12], const Float3& point);
	Float3 TransformDirection(const float (&rows)[12], const Float3& direction);
	float Determinant(const float (&rows)[12]);
	// The cofactor (determinant x inverse-transpose) of the linear part times the
	// normal: exact for non-uniform scale and, for mirroring transforms (negative
	// determinant), still pointing out of the mirrored surface. Not normalized.
	Float3 TransformNormal(const float (&rows)[12], const Float3& normal);

	// The fragment's shading frame from interpolated attributes: N normalized, T
	// Gram-Schmidt against N, B = cross(N, T) * sign(tangent.w). The rasterizer's
	// facing is corrected for mirroring transforms (their winding is reversed).
	StandardPbr::Frame BuildFrame(const Float3& normal, const Float4& tangent, bool rasterFrontFacing, bool mirrored);
	// Single-sided materials skip back faces (the pipelines rasterize both sides).
	bool CullsFace(const StandardPbr::Parameters& parameters, bool frontFacing);

	// Transparent sort key: the instance's world bounds center along CameraForward
	// (ForwardTransparentSort.slang).
	float SortDepth(const GpuInstanceRecord& instance, const GpuTransformRecord& transform, const ForwardViewRecord& view);
	// Back to front: larger depth first, then lower instance row, then lower
	// submesh row, so the order never depends on the bin's compaction order.
	bool SortsBefore(const ForwardSortEntry& a, const ForwardSortEntry& b);
	// The slots [firstSlot, firstSlot + records.size()) of one transparent bin in draw
	// order. `instances`/`transforms` are GPU Scene rows (GetInstanceRow/GetTransformRow).
	std::vector<std::uint32_t> SortTransparentDraws(std::span<const GpuDrawRecord> records, std::uint32_t firstSlot,
		std::span<const GpuInstanceRecord> instances, std::span<const GpuTransformRecord> transforms, const ForwardViewRecord& view);

	// Lights and clusters a pixel is shaded with. Without a grid every local light
	// is summed (brute force); with one, the pixel's cluster list is used.
	struct LightingInputs
	{
		std::span<const GpuLightRecord> Lights;
		GpuLightHeader Header{};
		const ClusterGridRecord* Grid = nullptr;
		std::span<const ClusterRecord> Records;
		std::span<const std::uint32_t> Indices;
		const Environment::EnvironmentProbe* Environment = nullptr; // Used when the view has ForwardViewFlagEnvironment.
	};

	// View depth of a world point under the grid's view (> 0 in front of the camera).
	float ViewDepth(const ClusterGridRecord& grid, const Float3& world);

	// Linear radiance toward the camera (rgb) and the surface's alpha: direct light
	// (directional + clustered local) + ambient * base color * occlusion + IBL +
	// emission. pixelX/pixelY select the cluster (framebuffer pixels, top-left).
	Float4 Shade(const LightingInputs& inputs, const ForwardViewRecord& view, const StandardPbr::ResolvedSurface& surface,
		const Float3& position, float pixelX, float pixelY);

	// ForwardPlusDebugMode::ClusterHeatmap: the cluster's heatmap color, opaque black
	// where the cluster has no lights.
	Float4 DebugColor(const ClusterGridRecord& grid, std::span<const ClusterRecord> records, float pixelX, float pixelY, float viewDepth);

	// The transparent pass's blend (premultiplied source, One / OneMinusSourceAlpha):
	// straight-alpha `source` over `destination`.
	Float4 Over(const Float4& source, const Float4& destination);
} // namespace Swim::Render::ForwardPlus
