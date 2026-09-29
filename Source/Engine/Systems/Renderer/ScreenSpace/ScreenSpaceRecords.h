#pragma once
#include <cstddef>
#include <cstdint>

namespace Swim::Render
{

	// Per-frame parameters of every screen-space program (Shaders/Slang/ScreenSpace/
	// ScreenSpaceRecords.slang), uploaded once per Record. Matrices are row-major.
	struct GpuScreenSpaceParams
	{
		float InverseProjection[16] = {}; // Clip -> view (unjittered).
		float ViewRows[12] = {};		  // World -> view, rows 0-2.
		float InverseViewRows[12] = {};	  // View -> world, rows 0-2.
		float Jitter[2] = {};			  // NDC jitter the inputs were rendered with.
		std::uint32_t Width = 0;
		std::uint32_t Height = 0;
		float AoRadius = 0.5f;
		float AoFalloff = 0.4f;
		float AoPower = 1.0f;
		float AoMaxRadiusPixels = 64.0f;
		float AoRadiusToPixels = 0.0f; // Projection[0][0] * Width / 2: pixels per unit at view depth 1.
		float AoBlurDepthTolerance = 0.1f;
		std::uint32_t AoSliceCount = 2;
		std::uint32_t AoStepCount = 4;
		std::uint32_t NoiseFrame = 0; // Rotates the noise per frame (0 .. 63).
		std::uint32_t AoEnabled = 0;
		std::uint32_t FogEnabled = 0;
		std::uint32_t SsrEnabled = 0;
		float FogColor[3] = {};
		float FogDensity = 0.0f;
		float FogSunColor[3] = {};
		float FogHeightFalloff = 0.0f;
		float FogSunDirection[3] = { 0, -1, 0 };
		float FogBaseHeight = 0.0f;
		float FogAnisotropy = 0.0f;
		float FogStartDistance = 0.0f;
		float FogMaxDistance = 1000.0f;
		std::uint32_t AoHalf = 0; // 1: the raw AO is half size (AmbientOcclusionSettings::HalfResolution).
		// Screen-space reflections (item 76).
		float Projection[16] = {}; // View -> clip (unjittered), to project the rays.
		float SsrMaxDistance = 20.0f;
		float SsrThickness = 0.3f;
		float SsrStride = 2.0f;
		float SsrMaxRoughness = 0.6f;
		float SsrRoughnessFade = 0.2f;
		float SsrEdgeFade = 0.1f;
		float SsrDistanceFade = 0.25f;
		float SsrNearZ = -0.1f; // View z of the near plane (< 0): rays are clipped to it.
		std::uint32_t SsrMaxSteps = 64;
		std::uint32_t SsrRefineSteps = 4;
		std::uint32_t SsrHistory = 0; // 1: hits read the previous frame's color (reflections of reflections).
		std::uint32_t SsrBackDepth = 0; // 1: surface thickness from the back-face depth (ScreenSpace::SurfaceThickness).
		// The reflection fallback (composite): local reflection probes, then the global IBL.
		std::uint32_t ProbeCount = 0;	   // Active GpuReflectionProbeRecords.
		std::uint32_t ProbeMipCount = 1;   // Prefiltered probe atlas mips.
		std::uint32_t ReflectionDebug = 0; // ReflectionDebugView.
		std::uint32_t SsrHalf = 0; // 1: the reflection texture is half size (ReflectionSettings::HalfResolution).
		// The temporal reflection filter: bit 0 writes this frame's reflection term, bit 1
		// blends last frame's in (ScreenSpaceFrame::ReflectionTemporal).
		std::uint32_t ReflectionTemporal = 0;
		float ReflectionTemporalBlend = 0.3f; // Weight of this frame's term, (0, 1].
		std::uint32_t Reserved5 = 0;
		std::uint32_t Reserved6 = 0;
	};

	static_assert(sizeof(GpuScreenSpaceParams) == 432);
	static_assert(offsetof(GpuScreenSpaceParams, AoRadius) == 176 && offsetof(GpuScreenSpaceParams, FogColor) == 224);
	static_assert(offsetof(GpuScreenSpaceParams, Projection) == 288 && offsetof(GpuScreenSpaceParams, SsrMaxSteps) == 384);

} // namespace Swim::Render
