#include "Engine/Systems/Renderer/Vfx/ParticleEnhancer.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Engine
{

	namespace
	{

		using namespace Swim::Render;
		using Color = std::array<float, 4>;

		float Clean(float value, float fallback, float low, float high)
		{
			return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
		}

		Color Scaled(const Color& color, float rgb, float alpha)
		{
			return { color[0] * rgb, color[1] * rgb, color[2] * rgb, std::clamp(color[3] * alpha, 0.0f, 1.0f) };
		}

		ParticleGradient Gradient(std::initializer_list<float> times, std::initializer_list<Color> colors)
		{
			ParticleGradient gradient;
			gradient.KeyCount = static_cast<std::uint32_t>(times.size());
			std::copy(times.begin(), times.end(), gradient.Times.begin());
			std::copy(colors.begin(), colors.end(), gradient.Colors.begin());
			return gradient;
		}

		ParticleCurve Curve(std::initializer_list<float> times, std::initializer_list<float> values)
		{
			ParticleCurve curve;
			curve.KeyCount = static_cast<std::uint32_t>(times.size());
			std::copy(times.begin(), times.end(), curve.Times.begin());
			std::copy(values.begin(), values.end(), curve.Values.begin());
			return curve;
		}

		// The spawn and motion every layer shares (same seed: the same particles).
		ParticleEmitterDesc Base(const SimpleParticle& p, bool sorted)
		{
			ParticleEmitterDesc desc;
			const float lifetime = Clean(p.Lifetime, 1.0f, 0.02f, 120.0f);
			const float lifeVariance = Clean(p.LifetimeVariance, 0.3f, 0.0f, 0.95f);
			const float size = Clean(p.Size, 0.1f, 0.005f, 100.0f);
			const float sizeVariance = Clean(p.SizeVariance, 0.25f, 0.0f, 0.95f);
			const float speedVariance = Clean(p.SpeedVariance, 0.3f, 0.0f, 1.0f);
			const float rate = Clean(p.Rate, 0.0f, 0.0f, 10000.0f);
			const float duration = Clean(p.Duration, 0.0f, 0.0f, 3600.0f);
			const std::uint32_t count = std::min(p.Count, 4096u);
			float speed = 0.0f;

			for (int c = 0; c < 3; ++c)
			{
				const float v = std::isfinite(p.Velocity[c]) ? p.Velocity[c] : 0.0f;
				speed += v * v;
				desc.Gravity[c] = Clean(p.Gravity[c], 0.0f, -1000.0f, 1000.0f);
				desc.ShapeExtent[c] = Clean(p.Area[c], 0.0f, 0.0f, 1000.0f);
			}

			speed = std::sqrt(speed);

			if (speed > 1.0e-5f)
			{
				for (int c = 0; c < 3; ++c)
				{
					desc.Direction[c] = p.Velocity[c] / speed;
				}
			}

			desc.Shape = desc.ShapeExtent[0] + desc.ShapeExtent[1] + desc.ShapeExtent[2] > 0.0f ? ParticleShape::Box : ParticleShape::Point;
			desc.ConeAngle = Clean(p.Spread, 0.4f, 0.0f, 3.14159f);
			desc.SpeedMin = speed * (1.0f - speedVariance);
			desc.SpeedMax = speed * (1.0f + speedVariance);
			desc.LifetimeMin = lifetime * (1.0f - lifeVariance);
			desc.LifetimeMax = lifetime * (1.0f + lifeVariance);
			desc.SizeMin = size * (1.0f - sizeVariance);
			desc.SizeMax = size * (1.0f + sizeVariance);
			desc.RotationMin = 0.0f;
			desc.RotationMax = 6.2832f;
			desc.Drag = Clean(p.Drag, 0.0f, 0.0f, 100.0f);
			desc.Rate = rate;
			desc.Seed = p.Seed;

			if (rate > 0.0f && duration == 0.0f)
			{
				desc.Duration = 0.0f;
				desc.Looping = true;
			}
			else
			{
				desc.Duration = std::max(duration, 0.05f);
				desc.Looping = false;
			}

			if (count > 0)
			{
				desc.Bursts.push_back({ 0.0f, count });
			}

			// Enough slots for the live population, kept within the sorted limit.
			const float live = rate * desc.LifetimeMax + float(count);
			const std::uint32_t capacity = static_cast<std::uint32_t>(std::ceil(live * 1.25f)) + 8u;
			desc.Capacity = std::clamp(capacity, 8u, sorted ? MaxSortedParticles : 16384u);
			desc.Blend = sorted ? ParticleBlendMode::AlphaBlend : ParticleBlendMode::Additive;
			return desc;
		}

		bool Sorted(ParticleHint hint)
		{
			return hint == ParticleHint::Smoke || hint == ParticleHint::Dust || hint == ParticleHint::Liquid;
		}

		float Lifetime(const ParticleEmitterDesc& desc)
		{
			return desc.Looping ? std::numeric_limits<float>::infinity() : desc.Duration + desc.LifetimeMax;
		}

	} // namespace

	EnhancedParticle EnhanceParticle(const SimpleParticle& particle, VfxQuality quality)
	{
		EnhancedParticle out;

		if (quality == VfxQuality::Off)
		{
			return out;
		}

		Color color = particle.Color;

		for (int c = 0; c < 4; ++c)
		{
			color[c] = Clean(color[c], 1.0f, 0.0f, c == 3 ? 1.0f : 64.0f);
		}

		const ParticleHint hint = particle.Hint;

		if (quality == VfxQuality::Classic)
		{
			ParticleEmitterDesc desc = Base(particle, Sorted(hint));
			desc.Material = ParticleMaterial::Sprite;
			desc.TextureIndex = particle.TextureIndex;
			desc.SamplerIndex = particle.TextureIndex != 0 ? std::max(particle.SamplerIndex, 1u) : 0u;
			desc.UvRect = particle.UvRect;
			desc.RotationMax = 0.0f;
			desc.ColorOverLife = Gradient({ 0.0f, 0.8f, 1.0f }, { color, color, Scaled(color, 1.0f, 0.0f) });
			out.Lifetime = Lifetime(desc);
			out.Layers.push_back(std::move(desc));
			return out;
		}

		const float emissive = Clean(particle.Emissive, 0.0f, 0.0f, 4.0f);
		ParticleEmitterDesc main = Base(particle, Sorted(hint));
		const float speed = 0.5f * (main.SpeedMin + main.SpeedMax);
		bool glow = emissive > 0.0f;
		bool trail = false;

		switch (hint)
		{
		case ParticleHint::Flame:
		{
			main.Material = ParticleMaterial::Fire;
			main.Facing = ParticleFacing::UprightY;
			main.RotationMax = 0.0f;
			main.AspectMin = 1.2f;
			main.AspectMax = 1.8f;
			const float boost = 2.5f + emissive;
			main.ColorOverLife = Gradient({ 0.0f, 0.15f, 0.6f, 1.0f },
				{ Scaled(color, boost * 1.4f, 0.0f), Scaled(color, boost * 1.4f, 1.0f), Scaled(color, boost * 0.7f, 0.8f),
					Scaled(color, boost * 0.25f, 0.0f) });
			main.SizeOverLife = Curve({ 0.0f, 0.3f, 1.0f }, { 0.6f, 1.0f, 0.4f });
			glow = true;
			break;
		}
		case ParticleHint::Smoke:
			main.Material = ParticleMaterial::Smoke;
			main.ColorOverLife = Gradient({ 0.0f, 0.15f, 0.7f, 1.0f },
				{ Scaled(color, 1.0f, 0.0f), Scaled(color, 1.0f, 0.85f), Scaled(color, 0.9f, 0.5f), Scaled(color, 0.8f, 0.0f) });
			main.SizeOverLife = Curve({ 0.0f, 1.0f }, { 0.7f, 2.2f });
			main.AngularVelocityMin = -0.4f;
			main.AngularVelocityMax = 0.4f;
			break;
		case ParticleHint::Spark:
			main.Material = ParticleMaterial::Spark;
			main.Facing = ParticleFacing::Velocity;
			main.Stretch = 0.06f;
			main.RotationMax = 0.0f;
			main.AspectMin = main.AspectMax = 1.5f;
			main.ColorOverLife = Gradient({ 0.0f, 0.7f, 1.0f },
				{ Scaled(color, 4.0f + emissive, 1.0f), Scaled(color, 2.0f + emissive, 0.9f), Scaled(color, 0.6f, 0.0f) });
			main.SizeOverLife = Curve({ 0.0f, 1.0f }, { 1.0f, 0.5f });
			glow = true;
			break;
		case ParticleHint::Magic:
			main.Material = ParticleMaterial::Spark;
			main.ColorOverLife = Gradient({ 0.0f, 0.2f, 0.8f, 1.0f },
				{ Scaled(color, 3.0f + emissive, 0.0f), Scaled(color, 3.0f + emissive, 1.0f), Scaled(color, 2.0f, 0.7f),
					Scaled(color, 1.0f, 0.0f) });
			main.SizeOverLife = Curve({ 0.0f, 0.5f, 1.0f }, { 0.5f, 1.0f, 0.2f });
			glow = true;
			trail = true;
			break;
		case ParticleHint::Dust:
		case ParticleHint::Liquid:
			main.Material = ParticleMaterial::Soft;
			main.ColorOverLife = Gradient({ 0.0f, 0.1f, 0.75f, 1.0f },
				{ Scaled(color, 1.0f, 0.0f), Scaled(color, 1.0f, hint == ParticleHint::Dust ? 0.6f : 0.9f), Scaled(color, 1.0f, 0.5f),
					Scaled(color, 1.0f, 0.0f) });
			break;
		default:
			main.Material = ParticleMaterial::Soft;
			main.ColorOverLife = Gradient({ 0.0f, 0.1f, 0.75f, 1.0f },
				{ Scaled(color, 1.0f + emissive, 0.0f), Scaled(color, 1.0f + emissive, 1.0f), Scaled(color, 1.0f + emissive * 0.7f, 0.6f),
					Scaled(color, 1.0f, 0.0f) });
			main.SizeOverLife = Curve({ 0.0f, 1.0f }, { 0.8f, 1.3f });
			break;
		}

		trail = trail || (speed > 2.5f && !Sorted(hint) && hint != ParticleHint::Flame);

		// Glow and trail layers copy the spawn (same seed and timing), so they sit on the main
		// particles; both are additive.
		const ParticleEmitterDesc spawn = main;
		out.Layers.push_back(std::move(main));

		if (glow)
		{
			ParticleEmitterDesc halo = spawn;
			halo.Blend = ParticleBlendMode::Additive;
			halo.Material = ParticleMaterial::Glow;
			halo.Facing = ParticleFacing::Camera;
			halo.Stretch = 0.0f;
			halo.AspectMin = halo.AspectMax = 1.0f;
			halo.SizeMin *= 3.5f;
			halo.SizeMax *= 3.5f;
			halo.ColorOverLife = Gradient({ 0.0f, 0.2f, 1.0f },
				{ Scaled(color, 1.2f + emissive, 0.0f), Scaled(color, 1.2f + emissive, 0.22f), Scaled(color, 0.6f, 0.0f) });
			out.Layers.push_back(std::move(halo));
			out.Glows = true;
		}

		if (trail)
		{
			ParticleEmitterDesc streak = spawn;
			streak.Blend = ParticleBlendMode::Additive;
			streak.Material = ParticleMaterial::Spark;
			streak.Facing = ParticleFacing::Velocity;
			streak.Stretch = 0.12f;
			streak.RotationMin = streak.RotationMax = 0.0f;
			streak.SizeMin *= 0.5f;
			streak.SizeMax *= 0.5f;
			streak.ColorOverLife = Gradient(
				{ 0.0f, 0.8f, 1.0f }, { Scaled(color, 1.5f + emissive, 0.5f), Scaled(color, 1.0f, 0.3f), Scaled(color, 0.5f, 0.0f) });
			out.Layers.push_back(std::move(streak));
		}

		out.Lifetime = Lifetime(spawn);
		return out;
	}

} // namespace Engine
