#pragma once
#include "Engine/Systems/Renderer/Lights/GpuLightRecord.h"
#include "Engine/Systems/Renderer/Shadows/ShadowRecords.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace Swim::Render::Shadows
{
	// The CPU definition of the shadow views and of shadow sampling (Phase 16, items
	// 70-72). Shaders/Slang/Shadows/ShadowRecords.slang mirrors ShadowFactor line for
	// line; the native shadow smoke compares GPU shadow maps and lit images with it.
	using Float3 = std::array<float, 3>;
	using Matrix = std::array<float, 16>; // Row-major.

	inline constexpr float MaxSpotShadowFov = 2.6f; // Radians; wider cones are clipped (outside = lit).
	inline constexpr float PointShadowFov = 1.57079633f;

	// The camera cascades are fitted to: a row-major affine world -> view (looking
	// down -Z) and its symmetric perspective.
	struct ShadowCamera
	{
		Matrix View{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
		float VerticalFov = 1.0f;
		float Aspect = 1.0f;
		float Near = 0.1f;
	};

	struct CascadeSettings
	{
		std::uint32_t Count = 3;	   // 1 .. MaxShadowCascades.
		float MaxDistance = 60.0f;	   // Camera view depth covered by the last cascade.
		float SplitLambda = 0.75f;	   // 0 = uniform splits, 1 = logarithmic.
		float CasterExtension = 50.0f; // Depth range added toward the light for off-screen casters.
	};

	// Count + 1 view depths from near to far: the practical split scheme,
	// lambda * log + (1 - lambda) * uniform.
	std::vector<float> CascadeSplits(float nearDepth, float farDepth, std::uint32_t count, float lambda);

	struct BoundingSphere
	{
		Float3 Center{ 0, 0, 0 };
		float Radius = 0.0f;
	};

	// The smallest sphere around the camera frustum slice [sliceNear, sliceFar]
	// centered on the view axis. Its radius depends only on the depths, field of view
	// and aspect, never on the camera's rotation or position: cascades do not shimmer.
	BoundingSphere CascadeSphere(const ShadowCamera& camera, float sliceNear, float sliceFar);

	// Row-major look-along view (right, up, -forward rows). `up` is world +Y unless
	// the forward axis is nearly vertical, then +Z.
	Matrix LookAlong(const Float3& eye, const Float3& forward);

	struct CascadeView
	{
		Matrix ViewProjection{};
		BoundingSphere Sphere;
		float Near = 0.0f; // Camera view depths of the slice.
		float Far = 0.0f;
		float TexelWorldSize = 0.0f;
	};

	// One orthographic reverse-Z view per cascade, looking along lightDirection (the
	// direction light travels). The sphere's center is snapped to whole shadow texels
	// in light space, so a moving camera moves each cascade in texel steps.
	std::vector<CascadeView> ComputeCascades(
		const ShadowCamera& camera, const Float3& lightDirection, const CascadeSettings& settings, std::uint32_t resolution);

	// The spot light's cone angle as a shadow field of view (clamped to MaxSpotShadowFov).
	float SpotShadowFov(const GpuLightRecord& light);
	// Perspective reverse-Z (infinite far) from the light along its axis.
	Matrix SpotShadowViewProjection(const GpuLightRecord& light, float nearPlane);
	// Cube faces +X, -X, +Y, -Y, +Z, -Z: 90-degree reverse-Z perspectives.
	std::array<Matrix, 6> PointShadowViewProjections(const Float3& position, float nearPlane);
	// The face whose frustum contains `fromLight` (its major axis; ties go to X, then Y).
	std::uint32_t PointShadowFace(const Float3& fromLight);

	// A point projected into a shadow view: atlas pixel coordinates and reverse-Z depth.
	struct ShadowProjection
	{
		float PixelX = 0.0f;
		float PixelY = 0.0f;
		float Depth = 0.0f;
	};

	// Empty behind the view or outside its [-1, 1] x [-1, 1] x [0, 1] clip volume.
	std::optional<ShadowProjection> ProjectToShadowView(const GpuShadowView& view, const Float3& position);

	// A read-back (or CPU-rendered) depth atlas: Size x Size texels, rows top to bottom.
	struct ShadowAtlasImage
	{
		std::uint32_t Size = 0;
		std::vector<float> Depth;

		float At(std::uint32_t x, std::uint32_t y) const { return Depth[std::size_t(y) * Size + x]; }
	};

	struct ShadowSampleInputs
	{
		const ShadowAtlasImage* Atlas = nullptr;
		std::span<const GpuShadowRecord> Records;
		std::span<const GpuShadowView> Views;
	};

	// The view record `shadowIndex` uses at a point, or empty (unshadowed there):
	// the first cascade whose CascadeFar exceeds cameraViewDepth, the spot view, or the
	// point light's cube face.
	std::optional<std::uint32_t> SelectShadowView(const GpuShadowRecord& record, const Float3& position, float cameraViewDepth);

	// Fraction of the light reaching `position` (1 = lit): the point is offset along
	// the surface normal by (NormalBias + SlopeBias * (1 - N.L)) * max(PcfRadius, 1) shadow texels,
	// projected into its view (selected again for the offset point, which can cross
	// into another cube face), and compared with the (2 PcfRadius + 1)^2 texels around
	// it, clamped to the tile: lit where depth + DepthBias >= stored (reverse-Z).
	// Unknown or None records, points outside every view and missing atlases are lit.
	float ShadowFactor(const ShadowSampleInputs& inputs, std::uint32_t shadowIndex, const Float3& position, const Float3& normal,
		const Float3& toLight, float cameraViewDepth);
} // namespace Swim::Render::Shadows
