#pragma once
#include "Engine/Systems/Renderer/ClusteredLighting/ClusterGrid.h"
#include "Engine/Systems/Renderer/Environment/EnvironmentReference.h"
#include "Engine/Systems/Renderer/ForwardPlus/ForwardPlusRecords.h"
#include "Engine/Systems/Renderer/GpuScene/GpuInstanceRecord.h"
#include "Engine/Systems/Renderer/GpuScene/GpuTransformRecord.h"
#include "Engine/Systems/Renderer/Lights/GpuLightRecord.h"
#include "Engine/Systems/Renderer/Materials/StandardPbr.h"
#include "Engine/Systems/Renderer/Shadows/ShadowMath.h"
#include "Engine/Systems/Renderer/Visibility/GpuDrawRecord.h"

#include <array>
#include <optional>
#include <span>
#include <vector>

namespace Swim::Render
{
	// Per-view inputs of a Forward+ frame (BuildForwardViewRecord packs them).
	struct ForwardPlusView
	{
		std::array<float, 16> ViewProjection{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 }; // Row-major, unjittered.
		// Item 75: the previous frame's unjittered view-projection (defaults to this
		// frame's: no camera motion) and the sub-pixel jitter as an NDC offset
		// (Temporal::JitterNdc). Jitter moves only the rasterized position.
		std::optional<std::array<float, 16>> PreviousViewProjection;
		std::array<float, 2> Jitter{ 0, 0 };
		std::array<float, 3> CameraPosition{ 0, 0, 0 };
		std::array<float, 3> CameraForward{ 0, 0, -1 }; // Normalized when packed.
		std::array<float, 3> Ambient{ 0, 0, 0 };
		float EnvironmentIntensity = 1.0f;
		float EnvironmentRotation = 0.0f;
		ForwardPlusDebugMode DebugMode = ForwardPlusDebugMode::None;
	};

	// Throws std::invalid_argument for a non-finite matrix/position/jitter, a zero forward
	// vector, negative ambient or intensity, or an unknown debug mode.
	// ForwardViewFlagBrdfLut is set with an environment or hasBrdfLut.
	ForwardViewRecord BuildForwardViewRecord(const ForwardPlusView& view, std::uint32_t materialCount, std::uint32_t prefilteredMipCount,
		bool hasEnvironment, bool hasShadows = false, bool hasBrdfLut = false);
} // namespace Swim::Render

namespace Swim::Render::ForwardPlus
{
	// The CPU definition of Clustered Forward+ shading (items 66-67).
	// Shaders/Slang/ForwardPlus mirrors every function; the native Forward+ smoke
	// compares the two pixel by pixel.
	using Float3 = std::array<float, 3>;
	using Float4 = std::array<float, 4>;
	using Float2 = std::array<float, 2>;

	// Which visibility bin a material draws in: FlagAlphaBlend -> Transparent, the
	// rest (opaque, alpha-masked, double-sided) -> Opaque.
	ForwardPlusBin MaterialBin(const StandardPbr::Parameters& parameters);

	// GpuTransformRecord rows (row-major 3x4 affine).
	Float3 TransformPoint(const float (&rows)[12], const Float3& point);
	// Item 75: screen-space motion of an object-space point, current minus previous UV
	// (x right, y down; prevUV = uv - motion), from the current transform with the
	// unjittered view-projection and the previous transform with the previous one.
	Float2 MotionVector(const ForwardViewRecord& view, const float (&current)[12], const float (&previous)[12], const Float3& local);
	// Deforming meshes (item 78): the vertex's previous local position is its own
	// (GpuInstanceRecord::PreviousVertexOffset vertices further in the same page).
	Float2 MotionVector(const ForwardViewRecord& view, const float (&current)[12], const float (&previous)[12], const Float3& local,
		const Float3& previousLocal);
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
		const Shadows::ShadowSampleInputs* Shadows = nullptr;		// Used when the view has ForwardViewFlagShadows.
		// Item 76: a BRDF LUT bound without an environment (ForwardViewFlagBrdfLut); the
		// environment's own LUT is used when this is null.
		const Environment::Image2D* BrdfLut = nullptr;
	};

	// Camera view depth of a world point along the view record's forward axis (the
	// depth cascades are selected by).
	float CameraDepth(const ForwardViewRecord& view, const Float3& world);

	// A light's shadow factor at a point (1 = lit): Shadows::ShadowFactor for lights
	// with a ShadowIndex and LightFlags::CastsShadows when the view has
	// ForwardViewFlagShadows and shadows are supplied; 1 otherwise.
	float LightShadow(const LightingInputs& inputs, const ForwardViewRecord& view, const GpuLightRecord& light, const Float3& position,
		const Float3& normal, const Float3& toLight);

	// View depth of a world point under the grid's view (> 0 in front of the camera).
	float ViewDepth(const ClusterGridRecord& grid, const Float3& world);

	// Linear radiance toward the camera (rgb) and the surface's alpha: direct light
	// (directional + clustered local, each times LightShadow along the shading
	// normal) + ambient * base color * occlusion + IBL + emission. pixelX/pixelY select the cluster (framebuffer pixels, top-left).
	Float4 Shade(const LightingInputs& inputs, const ForwardViewRecord& view, const StandardPbr::ResolvedSurface& surface,
		const Float3& position, float pixelX, float pixelY);

	// Item 76: the indirect part of Shade, ambient * base color * occlusion + IBL: what
	// the opaque pass writes to ForwardPlusTargets::Indirect and what ambient occlusion
	// attenuates.
	Float3 IndirectRadiance(
		const LightingInputs& inputs, const ForwardViewRecord& view, const StandardPbr::ResolvedSurface& surface, const Float3& position);

	// Item 76 (screen-space reflections): what the opaque pass writes to
	// ForwardPlusTargets::Reflectance and ::Specular. SpecularReflectance is the
	// split-sum weight StandardPbr::EnvironmentSpecularWeight from the bilinear BRDF LUT
	// at (N.V, roughness) when the view has ForwardViewFlagBrdfLut, 0 otherwise (no
	// reflections without a LUT). SpecularRadiance is the specular IBL, Prefiltered x
	// SpecularReflectance, 0 without an environment: the part of IndirectRadiance a
	// screen-space reflection replaces.
	struct SpecularTerms
	{
		Float3 Reflectance{ 0, 0, 0 };
		Float3 Radiance{ 0, 0, 0 };
	};

	SpecularTerms SpecularEnvironment(
		const LightingInputs& inputs, const ForwardViewRecord& view, const StandardPbr::ResolvedSurface& surface, const Float3& position);

	// ForwardPlusDebugMode::ClusterHeatmap: the cluster's heatmap color, opaque black
	// where the cluster has no lights.
	Float4 DebugColor(const ClusterGridRecord& grid, std::span<const ClusterRecord> records, float pixelX, float pixelY, float viewDepth);

	// The transparent pass's blend (premultiplied source, One / OneMinusSourceAlpha):
	// straight-alpha `source` over `destination`.
	Float4 Over(const Float4& source, const Float4& destination);
} // namespace Swim::Render::ForwardPlus
