#pragma once
#include <array>
#include <cstdint>

namespace Swim::Render
{

	inline constexpr std::uint32_t MaxAoSlices = 4;
	inline constexpr std::uint32_t MaxAoSteps = 8;
	inline constexpr std::uint32_t MaxReflectionSteps = 256;
	inline constexpr std::uint32_t MaxReflectionRefineSteps = 8;

	// Ground-truth-based ambient occlusion (GTAO, critical-path item 76). It attenuates
	// only the indirect radiance Forward+ writes to ForwardPlusTargets::Indirect.
	struct AmbientOcclusionSettings
	{
		bool Enabled = true;
		float Radius = 0.5f;			 // World-space search radius (> 0).
		float Falloff = 0.4f;			 // Fraction of Radius over which occluders fade out, (0, 1].
		float Power = 1.0f;				 // visibility^Power, (0, 8].
		float MaxRadiusPixels = 64.0f;	 // Screen-space cap of the search, [1, 256].
		std::uint32_t SliceCount = 2;	 // Directions per pixel, 1 .. MaxAoSlices.
		std::uint32_t StepCount = 4;	 // Samples per direction and side, 1 .. MaxAoSteps.
		float BlurDepthTolerance = 0.1f; // Relative depth difference at which the 5x5 blur stops mixing, (0, 1].
		// Trace one pixel of every 2 x 2 block (rotating each frame, like the half-resolution
		// reflections); the blur resamples the traced texels on each pixel's own surface.
		bool HalfResolution = false;
	};

	// Analytic exponential height fog with a sun in-scattering lobe (item 76).
	struct FogSettings
	{
		bool Enabled = false;
		float Density = 0.02f;							// Extinction per metre at BaseHeight (>= 0).
		float HeightFalloff = 0.1f;						// Density falls by e every 1 / HeightFalloff metres upward (>= 0; 0 = uniform).
		float BaseHeight = 0.0f;						// World Y of Density.
		std::array<float, 3> Color{ 0.5f, 0.6f, 0.7f }; // Ambient in-scattered radiance (linear, >= 0).
		std::array<float, 3> SunColor{ 0, 0, 0 };		// Directional in-scattered radiance at the phase peak (>= 0).
		std::array<float, 3> SunDirection{ 0, -1, 0 };	// Direction the sunlight travels (normalized when packed).
		float Anisotropy = 0.6f;						// Henyey-Greenstein g, [-0.95, 0.95].
		float StartDistance = 0.0f;						// Fog begins this far from the camera (>= 0).
		float MaxDistance = 1000.0f;					// Distances (and the sky) are capped here (> StartDistance).
	};

	// Screen-space reflections (item 76): one mirror ray per pixel, marched through the
	// depth buffer in screen space, replacing the specular IBL (ForwardPlusTargets::
	// Specular) with the radiance found where it hits, weighted by the specular
	// reflectance (ForwardPlusTargets::Reflectance) and a confidence. Glossy
	// reflections are faded out by roughness rather than blurred; TAA resolves the
	// per-frame jitter of the march.
	// What the composite shows instead of the lit reflection (reflective pixels only; the
	// rest of the frame is dimmed to grey):
	//  Sources:  red = screen-space reflection, green = local probe, blue = global
	//            environment (the share each contributes), magenta = a screen-space ray that
	//            reached geometry from its hidden side (RejectedHit) and fell back;
	//  ProbeAge: the probe in use (hue by slot, scaled by its coverage), darkening to a quarter
	//            as its oldest face reaches 4 s (stale),
	//            the global environment in dark blue.
	enum class ReflectionDebugView : std::uint32_t
	{
		None = 0,
		Sources = 1,
		ProbeAge = 2,
	};

	struct ReflectionSettings
	{
		bool Enabled = false;
		float MaxDistance = 20.0f;	   // View-space ray length in metres (> 0).
		float Thickness = 0.3f;		   // How far behind the depth buffer a sample still hits, in metres (> 0).
		float Stride = 2.0f;		   // Pixels between march samples, [1, 64].
		std::uint32_t MaxSteps = 64;   // March samples per ray, 1 .. MaxReflectionSteps.
		std::uint32_t RefineSteps = 4; // Binary-search steps after a hit, 0 .. MaxReflectionRefineSteps.
		float MaxRoughness = 0.6f;	   // Perceptual roughness at and above which nothing is reflected, (0, 1].
		float RoughnessFade = 0.2f;	   // Confidence ramps to 0 over this roughness range below MaxRoughness, (0, MaxRoughness].
		float EdgeFade = 0.1f;		   // Hits within this fraction of the screen edge fade out, (0, 0.5].
		float DistanceFade = 0.25f;	   // Hits in the last fraction of MaxDistance fade out, (0, 1].
		// Hits read the previous frame's finished color (reflections of reflections) when the
		// renderer has one (TAA on); off: the current frame's color.
		bool History = true;
		// Surface thickness from the back-face depth (ForwardPlusTargets::BackDepth, one more
		// depth-only draw of the opaque scene) when the renderer supports it; off: Thickness.
		bool BackFaces = true;
		// Trace one pixel of every 2 x 2 block (rotating through the block each frame) and
		// resolve the full frame with a depth- and normal-aware filter in the composite: a
		// quarter of the rays. TAA gathers the four positions over four frames.
		bool HalfResolution = false;
		// Blend the reflection term (screen space over probes over the environment) with last
		// frame's at the reprojected position (FrameRenderer keeps the history; needs motion
		// vectors): settles flicker from the trace pattern, jitter and probe refreshes.
		bool Temporal = false;
		float TemporalBlend = 0.4f; // Weight of this frame's term, (0, 1]: lower is smoother and lags more.
		ReflectionDebugView Debug = ReflectionDebugView::None;
	};

	struct ScreenSpaceSettings
	{
		AmbientOcclusionSettings AmbientOcclusion;
		FogSettings Fog;
		ReflectionSettings Reflections;
	};

	// Throws std::invalid_argument for values outside the ranges above.
	void ValidateScreenSpaceSettings(const ScreenSpaceSettings& settings);

} // namespace Swim::Render
