#pragma once
#include "Engine/Systems/Renderer/Vfx/VfxDirector.h"

#include "Engine/Systems/Renderer/Lights/GpuLightBuffer.h"
#include "Engine/Systems/Renderer/Particles/ParticleSystem.h"
#include "Engine/Systems/Renderer/Runtime/FrameRenderer.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

// Helpers shared by VfxDirector.cpp and VfxDirectorUpdate.cpp.
namespace Engine::VfxDetail
{

	using namespace Swim::Render;
	using Float3 = VfxDirector::Float3;
	using Color = std::array<float, 4>;

	constexpr float Infinity = std::numeric_limits<float>::infinity();
	constexpr Float3 FireColor{ 1.0f, 0.55f, 0.22f };

	inline std::array<float, 12> Translation(const Float3& p)
	{
		return { 1, 0, 0, p[0], 0, 1, 0, p[1], 0, 0, 1, p[2] };
	}

	inline float Distance2(const Float3& a, const Float3& b)
	{
		const float x = a[0] - b[0];
		const float y = a[1] - b[1];
		const float z = a[2] - b[2];
		return x * x + y * y + z * z;
	}

	inline bool Finite(const Float3& p)
	{
		return std::isfinite(p[0]) && std::isfinite(p[1]) && std::isfinite(p[2]);
	}

	inline float EaseOut(float t)
	{
		t = std::clamp(t, 0.0f, 1.0f);
		return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
	}

	inline ParticleGradient Gradient(std::initializer_list<float> times, std::initializer_list<Color> colors)
	{
		ParticleGradient gradient;
		gradient.KeyCount = static_cast<std::uint32_t>(times.size());
		std::copy(times.begin(), times.end(), gradient.Times.begin());
		std::copy(colors.begin(), colors.end(), gradient.Colors.begin());
		return gradient;
	}

	inline ParticleCurve Curve(std::initializer_list<float> times, std::initializer_list<float> values)
	{
		ParticleCurve curve;
		curve.KeyCount = static_cast<std::uint32_t>(times.size());
		std::copy(times.begin(), times.end(), curve.Times.begin());
		std::copy(values.begin(), values.end(), curve.Values.begin());
		return curve;
	}

	// A one-shot burst at the start of a short cycle.
	inline ParticleEmitterDesc Burst(std::uint32_t count, std::uint32_t seed, float delay = 0.0f)
	{
		ParticleEmitterDesc desc;
		desc.Rate = 0.0f;
		desc.Duration = delay + 0.05f;
		desc.Looping = false;
		desc.Bursts = { { delay, std::max(count, 1u) } };
		desc.Capacity = std::max(count, 1u);
		desc.Seed = seed;
		desc.RotationMax = 6.2832f;
		return desc;
	}

	inline float Lifetime(const ParticleEmitterDesc& desc)
	{
		return desc.Looping ? Infinity : desc.Duration + desc.LifetimeMax + 0.1f;
	}

	inline float FireballDuration(const VfxDirector::ExplosionDesc& desc)
	{
		const float fiery = desc.Style == VfxDirector::ExplosionStyle::Fiery ? 1.6f : 1.0f;
		return 1.1f * fiery * std::pow(std::max(desc.Size, 0.5f) / 4.0f, 0.3f);
	}

	inline float ExplosionLifetime(const VfxDirector::ExplosionDesc& desc)
	{
		return std::max(desc.Smoke ? 2.2f : 1.0f, FireballDuration(desc) + 0.1f);
	}

	inline float SmokeAmount(const VfxDirector::ExplosionDesc& desc)
	{
		switch (desc.Style)
		{
		case VfxDirector::ExplosionStyle::Fiery:
			return 0.12f;
		case VfxDirector::ExplosionStyle::Bright:
			return 0.0f;
		case VfxDirector::ExplosionStyle::Smokeless:
			return 0.0f;
		default:
			return desc.Smoke ? 1.0f : 0.0f;
		}
	}

	// One fireball lump t seconds in: the main ball (lump 0) or a smaller one beside it.
	inline PyroVolumes::Volume FireballLump(const VfxDirector::ExplosionDesc& d, float seed, int lump, float t, float duration)
	{
		const float s = d.Size;
		const bool bright = d.Style == VfxDirector::ExplosionStyle::Bright;
		const float angle = seed * 2.3f + float(lump) * 2.1f;
		const float offset = lump == 0 ? 0.0f : s * 0.35f;
		PyroVolumes::Volume ball;
		ball.Kind = PyroVolumes::Type::Fireball;
		ball.Position = { d.Position[0] + std::cos(angle) * offset, d.Position[1] + t * s * 0.25f + (lump == 0 ? 0.0f : s * 0.15f),
			d.Position[2] + std::sin(angle) * offset };
		ball.Radius = s * (lump == 0 ? 1.0f : 0.65f) * (0.35f + 0.85f * (1.0f - std::exp(-t * 9.0f)));
		ball.Age = std::clamp(t / duration, 0.0f, 1.0f);
		ball.Intensity = (bright ? 3.5f : 2.5f) * d.Intensity;
		ball.Density = 1.3f;
		ball.RiseSpeed = 2.5f;
		ball.Seed = seed + float(lump) * 7.1f;
		ball.SmokeAlbedo = bright ? 0.35f : 0.2f;
		return ball;
	}

	// The smoke column of an explosion a seconds old (smoke: its amount).
	inline PyroVolumes::Volume SmokeColumn(const VfxDirector::ExplosionDesc& d, float seed, float a, float smoke)
	{
		const float s = d.Size;
		// Short-lived: it swells for under a second and thins away by about 2 s.
		const float t = a - 0.15f;
		const float grow = EaseOut(t / 0.9f);
		PyroVolumes::Volume column;
		column.Kind = PyroVolumes::Type::Smoke;
		column.Position = { d.Position[0], d.Position[1] - s * 0.3f, d.Position[2] };
		column.Height = s * (0.8f + 1.6f * grow);
		column.Radius = s * (0.6f + 0.5f * grow);
		column.Age = std::clamp((a - 0.9f) / 1.2f, 0.0f, 1.0f);
		column.Density = 0.6f * smoke;
		column.RiseSpeed = 1.2f;
		column.Seed = seed + 3.3f;
		column.SmokeAlbedo = 0.24f;
		column.SmokeGlow = { 1.0f, 0.42f, 0.12f };
		column.SmokeGlowStrength = 1.2f * std::exp(-a * 2.5f) * d.Intensity;
		return column;
	}

	inline std::uint64_t CellKey(std::int32_t x, std::int32_t z)
	{
		return (std::uint64_t(std::uint32_t(x)) << 32) | std::uint32_t(z);
	}

} // namespace Engine::VfxDetail
