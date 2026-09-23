#pragma once
#include <array>
#include <cstdint>

namespace Swim::Render
{
	// Punctual light types (glTF KHR_lights_punctual semantics). Directional lights
	// affect everything and stay outside clustered light lists; point and spot
	// lights are "local": bounded by Range, and what clustered assignment bins.
	enum class LightType : std::uint32_t
	{
		Directional = 0,
		Point = 1,
		Spot = 2,
	};

	enum class LightFlags : std::uint32_t
	{
		None = 0,
		CastsShadows = 1u << 0,
	};

	constexpr LightFlags operator|(LightFlags a, LightFlags b)
	{
		return static_cast<LightFlags>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
	}

	// One light as the scene describes it. World space, +Y up, linear color.
	struct LightDesc
	{
		LightType Type = LightType::Point;
		std::array<float, 3> Position{ 0, 0, 0 };	// Point/spot.
		std::array<float, 3> Direction{ 0, -1, 0 }; // Where the light points (spot axis, directional travel); normalized on encode.
		std::array<float, 3> Color{ 1, 1, 1 };		// Linear.
		// Candela (lm/sr) for point/spot lights, lux (lm/m^2) for directional lights.
		float Intensity = 1.0f;
		float Range = 10.0f;			  // Point/spot: the distance where the light reaches zero (> 0).
		float InnerConeAngle = 0.0f;	  // Spot, radians, 0 <= inner < outer.
		float OuterConeAngle = 0.785398f; // Spot, radians, <= pi / 2.
		// Index into the (future) shadow system's records; NoShadow when unshadowed.
		std::uint32_t ShadowIndex = 0xffffffffu;
		LightFlags Flags = LightFlags::None;
	};

	inline bool IsLocalLight(LightType type)
	{
		return type != LightType::Directional;
	}
} // namespace Swim::Render
