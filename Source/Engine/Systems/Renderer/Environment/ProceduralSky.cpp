#include "Engine/Systems/Renderer/Environment/ProceduralSky.h"

#include <algorithm>
#include <cmath>

namespace Swim::Render::Environment
{
	Float3 ProceduralSky::Evaluate(const Float3& direction) const
	{
		const auto d = Normalize(direction);
		const float up = std::clamp(d[1], -1.0f, 1.0f);
		const float blend = 1.0f - (1.0f - std::abs(up)) * (1.0f - std::abs(up));
		const auto& end = up >= 0.0f ? ZenithColor : GroundColor;
		const auto sun = Normalize(SunDirection);
		const float lobe = std::pow(std::max(Dot(d, sun), 0.0f), SunSharpness);
		Float3 result;
		for (int c = 0; c < 3; ++c)
		{
			result[c] = (HorizonColor[c] + (end[c] - HorizonColor[c]) * blend + SunColor[c] * lobe) * Intensity;
		}
		return result;
	}

	ProceduralSky ProceduralSky::Uniform(float radiance)
	{
		ProceduralSky sky;
		sky.ZenithColor = sky.HorizonColor = sky.GroundColor = { radiance, radiance, radiance };
		sky.SunColor = { 0, 0, 0 };
		return sky;
	}

	ProceduralSkyConstants MakeProceduralSkyConstants(const ProceduralSky& sky, std::uint32_t face, std::uint32_t size)
	{
		ProceduralSkyConstants constants{};
		const auto sun = Normalize(sky.SunDirection);
		for (int c = 0; c < 3; ++c)
		{
			constants.Zenith[c] = sky.ZenithColor[c];
			constants.Horizon[c] = sky.HorizonColor[c];
			constants.Ground[c] = sky.GroundColor[c];
			constants.SunDirection[c] = sun[c];
			constants.SunColor[c] = sky.SunColor[c];
		}
		constants.Zenith[3] = sky.Intensity;
		constants.SunDirection[3] = sky.SunSharpness;
		constants.Face = face;
		constants.Size = size;
		return constants;
	}
} // namespace Swim::Render::Environment
