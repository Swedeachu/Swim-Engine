#pragma once

#include <array>

namespace Engine
{

	// A global wind field: a steady horizontal flow, gusts that roll downwind as broad
	// waves, and small turbulent eddies across the flow. The same model runs on the CPU
	// (WindVelocity) and in shaders (Features/Wind.slang: WindVelocity, WindSway), so
	// precipitation, vegetation, cloth or particles driven by one WindSettings agree.
	struct WindSettings
	{
		std::array<float, 2> Direction{ 1.0f, 0.0f }; // Horizontal (x, z) the wind blows toward; normalized on use.
		float Speed = 2.0f;							  // Metres per second.
		float Gustiness = 0.5f;						  // 0..1: gust swing relative to Speed.
		float GustScale = 40.0f;					  // Metres between gust fronts.
		float Turbulence = 0.3f;					  // 0..1: eddies across the flow, relative to Speed.
	};

	// The shader layout (WindParams in Wind.slang): Flow = (direction x, direction z, speed,
	// gustiness), Gust = (gust scale, turbulence, time, 0).
	struct GpuWind
	{
		std::array<float, 4> Flow{};
		std::array<float, 4> Gust{};
	};

	static_assert(sizeof(GpuWind) == 32);

	GpuWind PackWind(const WindSettings& wind, float time);

	// 0..1 gust strength at a horizontal position and time (0.5 on average).
	float WindGust(const WindSettings& wind, float x, float z, float time);

	// World velocity (metres per second) of the air at a position and time. Horizontal only.
	std::array<float, 3> WindVelocity(const WindSettings& wind, const std::array<float, 3>& position, float time);

} // namespace Engine
