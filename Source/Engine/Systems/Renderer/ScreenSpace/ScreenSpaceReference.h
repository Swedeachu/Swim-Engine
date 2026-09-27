#pragma once
#include "Engine/Systems/Renderer/ScreenSpace/ScreenSpaceRecords.h"
#include "Engine/Systems/Renderer/ScreenSpace/ScreenSpaceSettings.h"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace Swim::Render
{
	// The camera the screen-space inputs were rendered with.
	struct ScreenSpaceView
	{
		std::array<float, 16> View{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 }; // World -> view (rigid, right-handed, -Z forward).
		std::array<float, 16> Projection{};	 // View -> clip, row-major, unjittered perspective (w = -z), reverse-Z.
		std::array<float, 2> Jitter{ 0, 0 }; // NDC jitter of the frame (ForwardPlusView::Jitter).
	};

	// Packs the settings and view for a width x height frame. Throws std::invalid_argument
	// for invalid settings, a zero size, a non-finite or non-invertible view/projection,
	// a projection that is not perspective (row 3 must be 0, 0, -1, 0), non-finite
	// jitter, or (with reflections on) a projection whose near plane is not in front of
	// the camera.
	GpuScreenSpaceParams BuildScreenSpaceParams(const ScreenSpaceSettings& settings, const ScreenSpaceView& view, std::uint32_t width,
		std::uint32_t height, std::uint32_t noiseFrame);
} // namespace Swim::Render

namespace Swim::Render::ScreenSpace
{
	// The CPU definition of the screen-space effects (critical-path item 76).
	// Shaders/Slang/ScreenSpace mirrors every function, and the native smoke compares
	// the two texel by texel.
	using Float2 = std::array<float, 2>;
	using Float3 = std::array<float, 3>;
	using Float4 = std::array<float, 4>;

	template <typename T> struct Plane
	{
		std::uint32_t Width = 0;
		std::uint32_t Height = 0;
		std::vector<T> Texels; // Row-major, top row first.

		Plane() = default;

		Plane(std::uint32_t width, std::uint32_t height, T value = {})
			: Width(width), Height(height), Texels(std::size_t(width) * height, value)
		{
		}

		T& At(std::uint32_t x, std::uint32_t y) { return Texels[std::size_t(y) * Width + x]; }

		const T& At(std::uint32_t x, std::uint32_t y) const { return Texels[std::size_t(y) * Width + x]; }
	};

	using ColorImage = Plane<Float4>;
	using ScalarImage = Plane<float>;

	// General 4x4 inverse (row-major); std::nullopt when singular or non-finite.
	std::optional<std::array<float, 16>> Inverse(const std::array<float, 16>& m);

	// Interleaved gradient noise in [0, 1) at a pixel, rotated by the frame (0 .. 63).
	float InterleavedGradientNoise(float x, float y, std::uint32_t frame);

	// View-space position of a pixel centre (px, py in pixels, top-left origin) with a
	// reverse-Z depth; std::nullopt for the sky (depth <= 0).
	std::optional<Float3> ViewPosition(const GpuScreenSpaceParams& params, float px, float py, float depth);
	// The world normal rotated into view space and normalized; std::nullopt for a zero normal.
	std::optional<Float3> ViewNormal(const GpuScreenSpaceParams& params, const Float3& worldNormal);

	// GTAO cosine-weighted visibility, >= 0 and not yet clamped (1 = unoccluded, and for
	// sky or normal-less pixels; single slices of open surfaces can exceed 1). Slice s of SliceCount runs along screen
	// direction (s + noise) * pi / SliceCount. Each side takes StepCount samples at
	// (k + noise2) / StepCount of the projected radius, snapped to pixel centres.
	// Horizons fade to the normal's hemisphere with distance (AoFalloff), are
	// clamped to it, and each slice integrates the visible arc weighted by the
	// projected normal length.
	float GtaoTexel(
		const GpuScreenSpaceParams& params, const ScalarImage& depth, const ColorImage& normal, std::uint32_t x, std::uint32_t y);
	ScalarImage Gtao(const GpuScreenSpaceParams& params, const ScalarImage& depth, const ColorImage& normal);

	// 5x5 depth-aware blur: weight max(0, 1 - |dz| / (tolerance * |z|)) in view depth; sky
	// neighbours are skipped and sky pixels keep their value. The result is clamped to
	// [0, 1] and raised to AoPower: the final visibility.
	float BlurTexel(const GpuScreenSpaceParams& params, const ScalarImage& ao, const ScalarImage& depth, std::uint32_t x, std::uint32_t y);
	ScalarImage Blur(const GpuScreenSpaceParams& params, const ScalarImage& ao, const ScalarImage& depth);

	// Screen-space reflections (item 76). The pixel-space position (px, py, top-left
	// origin, jittered like the inputs) and clip w of a view-space point.
	struct ScreenPoint
	{
		float X = 0.0f;
		float Y = 0.0f;
		float W = 0.0f;
	};

	ScreenPoint ProjectToScreen(const GpuScreenSpaceParams& params, const Float3& view);

	// Depth-buffer points within SelfPlaneTolerance x view depth + SelfPlaneOffset of the
	// start pixel's tangent plane never count as hits (self-intersection).
	inline constexpr float SelfPlaneTolerance = 0.002f;
	inline constexpr float SelfPlaneOffset = 0.002f;
	// Hits fade out as the hit surface turns edge-on to the ray: facing fade =
	// clamp(-dot(hit normal, ray) / FacingFade, 0, 1).
	inline constexpr float FacingFade = 0.05f;
	// With the back-face depth (params.SsrBackDepth = 1), the surface at a pixel extends from
	// the depth buffer to the nearest back face behind it, at least BackFaceMinThickness;
	// where no back face exists (open ground, a plane seen from its front) it is solid
	// (OpenThickness). Without it: the constant SsrThickness.
	inline constexpr float BackFaceMinThickness = 0.02f;
	inline constexpr float OpenThickness = 1.0e30f;
	// Output alpha of a ray that reached a surface from its hidden side (with back faces): the
	// geometry it hit is not on screen, so the reflection fallback (probes, environment)
	// provides it. Treated as a miss (0) by the resolve and composite.
	inline constexpr float RejectedHit = -1.0f;
	// With back faces, a hit is taken only where the ray crossed the visible surface: just
	// before the hit (the bisection's last sample) the ray must be in front of the depth
	// buffer and that depth on the same surface, within max(2 x the final interval's ray depth span, this fraction of
	// the depth). Otherwise the ray came in across the object's silhouette, i.e. through a
	// side the camera does not see, and reports RejectedHit.
	inline constexpr float SilhouetteTolerance = 0.03f;
	float SurfaceThickness(const GpuScreenSpaceParams& params, const ScalarImage* backDepth, std::uint32_t x, std::uint32_t y, float frontDepth);

	// Where a pixel's mirror ray hits the depth buffer, and how much the hit is trusted.
	struct ReflectionHit
	{
		std::uint32_t X = 0; // Hit pixel.
		std::uint32_t Y = 0;
		float HitX = 0.0f; // Exact hit position in pixels (top-left origin); X, Y = floor.
		float HitY = 0.0f;
		float Distance = 0.0f;	 // View-space distance travelled.
		float Confidence = 0.0f; // (0, 1].
		bool Rejected = false;	 // Reached a surface from its hidden side (RejectedHit); X, Y are that surface.
	};

	// The march, in order:
	//  1. the pixel's view position and normal (sky or normal-less pixels, and those at
	//     or above SsrMaxRoughness, miss);
	//  2. r = reflect(normalize(p), n) in view space, SsrMaxDistance long, clipped to
	//     SsrNearZ; both ends are projected (ProjectToScreen), the projected segment is
	//     cut where it leaves the screen (tExit = ScreenExit) and sampled at
	//     t_i = min((i + j) / N, 1) x tExit, i = 1 .. N, N = min(SsrMaxSteps,
	//     ceil(on-screen pixel length / SsrStride)), j the pixel's interleaved-gradient
	//     noise (x + 23, y + 41);
	//  3. each sample's pixel (floor) is compared with the depth buffer: the sample is a
	//     candidate when the ray depth (perspective-correct) lies within [scene, scene +
	//     SsrThickness] view depth, or when it is behind the scene and the previous sample
	//     was not (a crossing the stride stepped over); sky pixels, the start pixel and
	//     points on the start pixel's own tangent plane (SelfPlaneTolerance) are never
	//     behind, in the march and the refinement alike, and leaving the screen misses;
	//  4. a candidate refines by SsrRefineSteps bisections of (previous t, t]; a refined
	//     pixel on the sky falls back to the sample's pixel. It hits only when the ray is at
	//     most max(SsrThickness, ray depth span of the final interval) behind the scene
	//     there; otherwise it passed behind a closer surface and the march goes on;
	//  5. a hit whose normal faces along the ray (a back face) is rejected;
	//  6. confidence = roughness fade x screen-edge fade (exact hit position) x distance fade
	//     x facing fade (FacingFade).
	// The largest t in [0, 1] at which (x0, y0) + t (dx, dy) is still on screen (0.01 px
	// inside the edges): the march samples only [0, tExit].
	float ScreenExit(float x0, float y0, float dx, float dy, float width, float height);

	// backDepth: ForwardPlusTargets::BackDepth (used when params.SsrBackDepth = 1): the march
	// counts a sample as inside a surface only up to that surface's thickness
	// (SurfaceThickness), and a refined hit is kept when the ray is at most the thickness
	// (plus the final interval's span) behind it; step 5 then reports a Rejected hit.
	std::optional<ReflectionHit> TraceReflection(const GpuScreenSpaceParams& params, const ScalarImage& depth, const ColorImage& normal,
		std::uint32_t x, std::uint32_t y, const ScalarImage* backDepth = nullptr);

	// The radiance a ray finds: the hit pixel's color minus (1 - ao) x indirect when AO
	// is on (clamped at 0), so reflected surfaces are occluded like direct views of them.
	Float3 HitRadiance(const GpuScreenSpaceParams& params, const ColorImage& color, const ColorImage& indirect, const ScalarImage* ao,
		std::uint32_t x, std::uint32_t y);

	// The radiance around an exact hit position (pixels): the four nearest texels'
	// HitRadiance, bilinearly weighted and divided by 1 + luminance (so a single bright
	// texel cannot flicker through the reflection), then renormalized.
	// With history (params.SsrHistory = 1 and both images), the four texels come instead
	// from the previous frame's finished color at the hit position moved back by the hit
	// pixel's motion vector (UV now - UV before, times the size), unmodified (it already
	// holds AO and the hit's own reflections: reflections of reflections); a position off
	// that image uses the current color as without history.
	using VelocityImage = Plane<Float2>;
	struct ReflectionHistory
	{
		const ColorImage* Color = nullptr;
		const VelocityImage* Velocity = nullptr;
	};

	Float3 FilteredHitRadiance(const GpuScreenSpaceParams& params, const ColorImage& color, const ColorImage& indirect,
		const ScalarImage* ao, float px, float py, const ReflectionHistory& history = {});

	// The reflection pass output: (FilteredHitRadiance at the hit, confidence), or 0 on a miss.
	Float4 ReflectionTexel(const GpuScreenSpaceParams& params, const ScalarImage& depth, const ColorImage& normal, const ColorImage& color,
		const ColorImage& indirect, const ScalarImage* ao, std::uint32_t x, std::uint32_t y, const ReflectionHistory& history = {},
		const ScalarImage* backDepth = nullptr);
	ColorImage Reflections(const GpuScreenSpaceParams& params, const ScalarImage& depth, const ColorImage& normal, const ColorImage& color,
		const ColorImage& indirect, const ScalarImage* ao, const ReflectionHistory& history = {}, const ScalarImage* backDepth = nullptr);

	// Fog along the camera ray `direction` (unit) out to `distance` from `camera`.
	struct FogSample
	{
		float Transmittance = 1.0f;
		Float3 Inscatter{ 0, 0, 0 }; // Radiance the fog adds at full opacity.
	};

	float FogOpticalDepth(const GpuScreenSpaceParams& params, const Float3& camera, const Float3& direction, float distance);
	float HenyeyGreenstein(float g, float cosTheta); // Normalized so that g = 0 gives 1.
	FogSample EvaluateFog(const GpuScreenSpaceParams& params, const Float3& camera, const Float3& direction, float distance);

	// The reflection inputs of one composite texel (all 0 without SSR).
	struct ReflectionSample
	{
		Float4 Reflection{ 0, 0, 0, 0 };  // ReflectionTexel: hit radiance, confidence.
		Float4 Reflectance{ 0, 0, 0, 0 }; // ForwardPlusTargets::Reflectance.
		Float4 Specular{ 0, 0, 0, 0 };	  // ForwardPlusTargets::Specular.
		// The local probe fallback (the composite's ReflectionProbes::Select + SampleProbe):
		// blended probe radiance (rgb) and its coverage (a; 0 without probes).
		Float4 Probe{ 0, 0, 0, 0 };
	};

	// One output texel: color - (1 - ao) * indirect (clamped at 0) when AO is on; then the
	// reflection hierarchy (with SSR on or probes present): the fallback F = specular +
	// coverage * (reflectance * probe - specular) replaces the specular IBL (+ ao' * (F -
	// specular), clamped), and the screen-space hit replaces F by its confidence (+
	// confidence * ao' * (reflectance * radiance - F), clamped; ao' = ao with AO on, else 1:
	// the specular IBL left after AO is replaced by equally occluded reflections; a negative
	// RejectedHit confidence counts as 0); then color * T + inscatter * (1 - T) when fog is on.
	// The sky (depth 0) is fogged at FogMaxDistance. Alpha is kept.
	Float4 CompositeTexel(const GpuScreenSpaceParams& params, const Float4& color, const Float4& indirect, float ao,
		const ReflectionSample& reflection, float depth, std::uint32_t x, std::uint32_t y);
	Float4 CompositeTexel(const GpuScreenSpaceParams& params, const Float4& color, const Float4& indirect, float ao, float depth,
		std::uint32_t x, std::uint32_t y);

	// The resolve the composite applies to the reflection pass output: a 5 x 5 tap grid with
	// a step of max(1, round(GlossyBlurRadius(roughness) / 2)) pixels (radius x Height /
	// 1080), Gaussian weights GlossyTapWeights per axis, taps kept only on the same surface
	// (view depth within 5 %, normals within ~25 degrees). Confidence is the weighted mean;
	// radiance the confidence-weighted mean. Even mirrors get the one-pixel-step footprint:
	// it smooths the row-to-row stepping of the march (scan lines on flat faces seen head
	// on); rough surfaces get soft reflections instead of sharp, grainy ones.
	inline constexpr float GlossyBlurScale = 20.0f; // Pixels at 1080p for roughness 1 (x roughness^1.5).
	inline constexpr float GlossyBlurMax = 8.0f;
	inline constexpr float GlossyTapWeights[3] = { 1.0f, 0.6065307f, 0.1353353f }; // exp(-d^2 / 2).
	float GlossyBlurRadius(const GpuScreenSpaceParams& params, float roughness);
	Float4 ResolvedReflectionTexel(const GpuScreenSpaceParams& params, const ColorImage& reflection, const ColorImage& normal,
		const ScalarImage& depth, std::uint32_t x, std::uint32_t y);

	struct ReflectionImages
	{
		const ColorImage* Reflection = nullptr;
		const ColorImage* Reflectance = nullptr;
		const ColorImage* Specular = nullptr;
		const ColorImage* Normal = nullptr; // With it the reflection is resolved (ResolvedReflectionTexel) first.
	};

	ColorImage Composite(const GpuScreenSpaceParams& params, const ColorImage& color, const ColorImage& indirect, const ScalarImage* ao,
		const ScalarImage& depth, const ReflectionImages& reflections = {});
} // namespace Swim::Render::ScreenSpace
