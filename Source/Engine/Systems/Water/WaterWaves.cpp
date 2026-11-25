#include "Engine/Systems/Water/WaterWaves.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Engine
{

	namespace
	{

		constexpr float Pi = 3.14159265358979f;
		constexpr float Gravity = 9.81f;

		// Deterministic 0..1 values per seed and index (PCG-style mix).
		float Random01(std::uint32_t seed, std::uint32_t index)
		{
			std::uint32_t state = seed * 747796405u + index * 2891336453u + 1u;
			state = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
			state = (state >> 22u) ^ state;
			return float(state >> 8) * (1.0f / 16777216.0f);
		}

		float Fade(float distance, float start, float end)
		{
			if (end <= start)
			{
				return distance < start ? 1.0f : 0.0f;
			}

			const float t = std::clamp((distance - start) / (end - start), 0.0f, 1.0f);
			return 1.0f - t * t * (3.0f - 2.0f * t);
		}

		bool Finite(float value)
		{
			return std::isfinite(value);
		}

	} // namespace

	WaterWaveSet BuildWaterWaves(const WaterWaveSettings& settings)
	{
		WaterWaveSet set;
		set.Height = settings.Height;
		set.Count = std::clamp(settings.Count, 1u, WaterWaveSet::MaxWaves);
		const float wind = settings.WindDirectionDegrees * Pi / 180.0f;
		const float steepness = std::clamp(settings.Steepness, 0.0f, 1.0f);
		const float shortening = std::clamp(settings.Shortening, 0.2f, 0.95f);
		float wavelength = std::max(settings.Wavelength, 0.05f);
		float amplitude = std::max(settings.Amplitude, 0.0f);

		for (std::uint32_t i = 0; i < set.Count; ++i)
		{
			auto& wave = set.Waves[i];
			const float angle = wind + (i == 0 ? 0.0f : settings.Spread * (2.0f * Random01(settings.Seed, i) - 1.0f));
			wave.Direction = { std::cos(angle), std::sin(angle) };
			wave.Number = 2.0f * Pi / wavelength;
			wave.Frequency = std::sqrt(Gravity * wave.Number) * settings.TimeScale;
			wave.Amplitude = amplitude;
			wave.Horizontal = steepness / (wave.Number * float(set.Count));
			wave.Phase = 2.0f * Pi * Random01(settings.Seed ^ 0x9E3779B9u, i);
			wave.Detail = set.Count > 1 ? float(i) / float(set.Count - 1) : 0.0f;
			wavelength *= shortening;
			amplitude *= shortening;
		}

		return set;
	}

	glm::vec3 EvaluateWaterWaves(const WaterWaveSet& set, glm::vec2 rest, double time, const float* weights)
	{
		glm::vec3 position(rest.x, set.Height, rest.y);

		for (std::uint32_t i = 0; i < set.Count; ++i)
		{
			const auto& wave = set.Waves[i];
			const float weight = weights ? weights[i] : 1.0f;
			// The phase is reduced in double precision so long-running clocks stay smooth.
			const double theta = double(wave.Number) * double(glm::dot(wave.Direction, rest)) - double(wave.Frequency) * time + wave.Phase;
			const float angle = static_cast<float>(std::fmod(theta, 2.0 * 3.14159265358979));
			const float c = std::cos(angle);
			position.x += wave.Direction.x * wave.Horizontal * c * weight;
			position.z += wave.Direction.y * wave.Horizontal * c * weight;
			position.y += wave.Amplitude * std::sin(angle) * weight;
		}

		return position;
	}

	WaterSurfaceSample SampleWaterSurface(const WaterWaveSet& set, float x, float z, double time)
	{
		const glm::vec2 target(x, z);
		glm::vec2 rest = target;

		for (int i = 0; i < 4; ++i)
		{
			const glm::vec3 moved = EvaluateWaterWaves(set, rest, time);
			rest += target - glm::vec2(moved.x, moved.z);
		}

		WaterSurfaceSample sample;
		glm::vec3 tangentX(1.0f, 0.0f, 0.0f);
		glm::vec3 tangentZ(0.0f, 0.0f, 1.0f);
		sample.Height = set.Height;

		for (std::uint32_t i = 0; i < set.Count; ++i)
		{
			const auto& wave = set.Waves[i];
			const double theta = double(wave.Number) * double(glm::dot(wave.Direction, rest)) - double(wave.Frequency) * time + wave.Phase;
			const float angle = static_cast<float>(std::fmod(theta, 2.0 * 3.14159265358979));
			const float s = std::sin(angle);
			const float c = std::cos(angle);
			const glm::vec2 d = wave.Direction;
			sample.Height += wave.Amplitude * s;
			// d(position)/d(rest x) and d(rest z).
			const float hk = wave.Horizontal * wave.Number;
			const float ak = wave.Amplitude * wave.Number;
			tangentX += glm::vec3(-d.x * d.x * hk * s, d.x * ak * c, -d.x * d.y * hk * s);
			tangentZ += glm::vec3(-d.x * d.y * hk * s, d.y * ak * c, -d.y * d.y * hk * s);
			sample.Velocity += glm::vec3(d.x * wave.Horizontal * wave.Frequency * s, -wave.Amplitude * wave.Frequency * c,
				d.y * wave.Horizontal * wave.Frequency * s);
		}

		const glm::vec3 normal = glm::cross(tangentZ, tangentX);
		const float length = glm::length(normal);
		sample.Normal = length > 1.0e-6f ? normal / length : glm::vec3(0.0f, 1.0f, 0.0f);
		return sample;
	}

	FluidSurfaceFunction MakeFluidSurface(std::shared_ptr<const WaterWaveSet> set)
	{
		return [set = std::move(set)](float x, float z, double time)
		{
			const auto sample = SampleWaterSurface(*set, x, z, time);
			return FluidSample{ sample.Height, sample.Velocity };
		};
	}

	void ValidateWaterLodSettings(const WaterLodSettings& s)
	{
		const float distances[] = { s.DetailStart, s.DetailEnd, s.AmplitudeStart, s.AmplitudeEnd, s.NormalStart, s.NormalEnd, s.ReflectionStart,
			s.ReflectionEnd, s.FarFieldExtent };

		for (const float d : distances)
		{
			if (!Finite(d) || d < 0.0f)
			{
				throw std::invalid_argument("water LOD distances must be finite and non-negative");
			}
		}

		if (s.DetailEnd < s.DetailStart || s.AmplitudeEnd < s.AmplitudeStart || s.NormalEnd < s.NormalStart ||
			s.ReflectionEnd < s.ReflectionStart)
		{
			throw std::invalid_argument("water LOD fades must end at or after their start");
		}

		if (!Finite(s.FarAmplitude) || s.FarAmplitude < 0.0f || s.FarAmplitude > 1.0f || !Finite(s.FarRoughness) || s.FarRoughness < 0.0f ||
			s.FarRoughness > 1.0f)
		{
			throw std::invalid_argument("water LOD FarAmplitude and FarRoughness must be in [0, 1]");
		}
	}

	WaterLodFactors EvaluateWaterLod(const WaterLodSettings& s, float distance)
	{
		if (!s.Enabled)
		{
			return {};
		}

		WaterLodFactors lod;
		lod.Detail = Fade(distance, s.DetailStart, s.DetailEnd);
		lod.Amplitude = s.FarAmplitude + (1.0f - s.FarAmplitude) * Fade(distance, s.AmplitudeStart, s.AmplitudeEnd);
		lod.Normal = Fade(distance, s.NormalStart, s.NormalEnd);
		lod.Reflection = Fade(distance, s.ReflectionStart, s.ReflectionEnd);
		return lod;
	}

	void WaterWaveLodWeights(const WaterWaveSet& set, const WaterLodFactors& lod, float* weights)
	{
		for (std::uint32_t i = 0; i < set.Count; ++i)
		{
			weights[i] = lod.Amplitude * (1.0f + (lod.Detail - 1.0f) * set.Waves[i].Detail);
		}
	}

} // namespace Engine
