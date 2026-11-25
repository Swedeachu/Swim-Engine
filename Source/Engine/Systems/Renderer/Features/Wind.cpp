#include "Engine/Systems/Renderer/Features/Wind.h"

#include <algorithm>
#include <cmath>

namespace Engine
{

	namespace
	{

		constexpr float TwoPi = 6.28318530718f;

		std::array<float, 2> Normalized(const std::array<float, 2>& d)
		{
			const float length = std::sqrt(d[0] * d[0] + d[1] * d[1]);
			return length > 1.0e-5f ? std::array<float, 2>{ d[0] / length, d[1] / length } : std::array<float, 2>{ 1.0f, 0.0f };
		}

	} // namespace

	GpuWind PackWind(const WindSettings& wind, float time)
	{
		const auto d = Normalized(wind.Direction);
		return { { d[0], d[1], std::max(wind.Speed, 0.0f), std::clamp(wind.Gustiness, 0.0f, 1.0f) },
			{ std::max(wind.GustScale, 1.0f), std::clamp(wind.Turbulence, 0.0f, 1.0f), time, 0.0f } };
	}

	// Mirrored by WindGust in Wind.slang.
	float WindGust(const WindSettings& wind, float x, float z, float time)
	{
		const auto d = Normalized(wind.Direction);
		const float scale = std::max(wind.GustScale, 1.0f);
		const float along = (x * d[0] + z * d[1]) / scale;
		const float across = (z * d[0] - x * d[1]) / scale;
		const float travel = time * std::max(wind.Speed, 0.5f) / scale;
		const float wave =
			std::sin((along - travel) * TwoPi) * 0.6f + std::sin((along * 2.3f + across * 0.7f - travel * 1.9f) * TwoPi) * 0.4f;
		return 0.5f + 0.5f * wave;
	}

	std::array<float, 3> WindVelocity(const WindSettings& wind, const std::array<float, 3>& position, float time)
	{
		const auto d = Normalized(wind.Direction);
		const float speed = std::max(wind.Speed, 0.0f);
		const float gust = WindGust(wind, position[0], position[2], time);
		const float flow = speed * (1.0f + std::clamp(wind.Gustiness, 0.0f, 1.0f) * (gust * 2.0f - 1.0f));
		const float eddy = std::sin(time * 1.7f + (position[0] - position[2]) * 0.31f) * std::sin(time * 0.6f + position[2] * 0.17f);
		const float cross = eddy * std::clamp(wind.Turbulence, 0.0f, 1.0f) * speed * 0.35f;
		return { d[0] * flow - d[1] * cross, 0.0f, d[1] * flow + d[0] * cross };
	}

} // namespace Engine
