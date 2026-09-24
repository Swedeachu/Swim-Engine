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
		std::uint32_t Reserved0 = 0;
		float FogColor[3] = {};
		float FogDensity = 0.0f;
		float FogSunColor[3] = {};
		float FogHeightFalloff = 0.0f;
		float FogSunDirection[3] = { 0, -1, 0 };
		float FogBaseHeight = 0.0f;
		float FogAnisotropy = 0.0f;
		float FogStartDistance = 0.0f;
		float FogMaxDistance = 1000.0f;
		float Reserved1 = 0.0f;
	};

	static_assert(sizeof(GpuScreenSpaceParams) == 288);
	static_assert(offsetof(GpuScreenSpaceParams, AoRadius) == 176 && offsetof(GpuScreenSpaceParams, FogColor) == 224);
} // namespace Swim::Render
