#pragma once
#include "Engine/Systems/Renderer/Lights/GpuLightRecord.h"
#include "Engine/Systems/Renderer/Lights/LightDesc.h"
#include "Engine/Systems/Renderer/Materials/StandardPbr.h"

#include <array>
#include <span>
#include <string>

namespace Swim::Render::Lights
{
	// The CPU definition of punctual lighting (critical-path item 63). Shaders/Slang/
	// Lights/GpuLightRecords.slang mirrors every function; the native light smoke
	// compares the two over thousands of lights.
	using Float3 = std::array<float, 3>;

	// Smallest distance^2 used for the inverse-square falloff (1 cm).
	inline constexpr float MinDistanceSquared = 1.0e-4f;
	// Smallest cos(inner) - cos(outer) a spot cone may have (glTF recommendation).
	inline constexpr float MinSpotConeDelta = 1.0e-3f;

	// Empty when the desc is valid; otherwise the reason. Rules: finite values;
	// intensity and color >= 0; a non-zero direction; local lights have Range > 0;
	// spots have 0 <= inner < outer <= pi / 2.
	std::string ValidateLight(const LightDesc& desc);
	// Throws std::invalid_argument with ValidateLight's reason.
	GpuLightRecord EncodeLight(const LightDesc& desc);

	// Inverse-square falloff with a smooth window reaching exactly 0 at Range:
	// saturate(1 - (d^2 / r^2)^2)^2 / max(d^2, MinDistanceSquared).
	float RangeAttenuation(float distanceSquared, float inverseRangeSquared);
	// glTF spot falloff for a unit vector toward the light: saturate(dot(axis, -L) *
	// scale + offset)^2. 1 for point and directional lights.
	float SpotAttenuation(const GpuLightRecord& light, const Float3& toLight);

	struct LightSample
	{
		Float3 Direction{ 0, 0, 1 }; // Unit vector from the shaded point toward the light.
		Float3 Radiance{ 0, 0, 0 };	 // Incident radiance scale (color * intensity * attenuation).
	};

	LightSample EvaluateLight(const GpuLightRecord& light, const Float3& position);

	// A sphere containing every point a local light can reach (the whole range
	// sphere for point lights; the tighter of the range sphere and the cone's own
	// bounding sphere for spots). Clustered light assignment tests these.
	struct LightSphere
	{
		Float3 Center{ 0, 0, 0 };
		float Radius = 0.0f;
	};

	LightSphere LightBoundingSphere(const GpuLightRecord& light);

	// Brute-force forward lighting: the sum over every light in the buffer of
	// StandardPbr::EvaluateBrdf times its radiance. The reference clustered
	// lighting (items 64-66) must reproduce.
	Float3 ShadeAllLights(std::span<const GpuLightRecord> rows, const GpuLightHeader& header, const StandardPbr::Surface& surface,
		const Float3& normal, const Float3& view, const Float3& position);
} // namespace Swim::Render::Lights
