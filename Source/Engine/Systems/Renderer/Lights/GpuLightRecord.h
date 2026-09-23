#pragma once
#include <cstddef>
#include <cstdint>

namespace Swim::Render
{
	// One light as shaders read it (std430, 64 bytes; Shaders/Slang/Lights/
	// GpuLightRecords.slang mirrors it). EncodeLight fills it from a LightDesc.
	struct GpuLightRecord
	{
		float Position[3] = { 0, 0, 0 };
		float Range = 0.0f;				   // Point/spot; 0 for directional lights.
		float Direction[3] = { 0, -1, 0 }; // Unit; where the light points.
		std::uint32_t Type = 0;			   // LightType.
		float Color[3] = { 0, 0, 0 };	   // Linear color times intensity (radiometric scale).
		float InverseRangeSquared = 0.0f;  // 1 / Range^2 (0 for directional lights).
		// Spot angular attenuation, glTF: saturate(dot(Direction, -L) * SpotScale + SpotOffset)^2.
		// Point and directional lights use scale 0, offset 1 (no falloff).
		float SpotScale = 0.0f;
		float SpotOffset = 1.0f;
		std::uint32_t ShadowIndex = 0xffffffffu; // GpuLightNoShadow when unshadowed.
		std::uint32_t Flags = 0;				 // LightFlags.
	};

	static_assert(sizeof(GpuLightRecord) == 64);
	static_assert(offsetof(GpuLightRecord, Direction) == 16);
	static_assert(offsetof(GpuLightRecord, Color) == 32);
	static_assert(offsetof(GpuLightRecord, SpotScale) == 48);

	inline constexpr std::uint32_t GpuLightNoShadow = 0xffffffffu;

	// The light buffer's header (a separate 32-byte storage buffer). Directional
	// lights occupy rows [0, DirectionalCount); local (point/spot) lights occupy rows
	// [FirstLocalRow, FirstLocalRow + LocalCount). Both ranges are dense.
	struct GpuLightHeader
	{
		std::uint32_t DirectionalCount = 0;
		std::uint32_t LocalCount = 0;
		std::uint32_t FirstLocalRow = 0; // == directional capacity.
		std::uint32_t LocalCapacity = 0;
		std::uint32_t Reserved[4] = {};
	};

	static_assert(sizeof(GpuLightHeader) == 32);
} // namespace Swim::Render
