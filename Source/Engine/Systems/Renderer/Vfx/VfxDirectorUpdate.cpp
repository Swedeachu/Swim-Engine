#include "Engine/Systems/Renderer/Vfx/VfxDirectorHelpers.h"

// The per-frame side: splashes and ripples, Update, lights, the pyro volumes and fires.
namespace Engine
{

	using namespace VfxDetail;

	void VfxDirector::SpawnSplash(const SplashDesc& desc)
	{
		if (!VfxEngineDraws(Settings.Water) || !Finite(desc.Position) || !Finite(desc.Velocity))
		{
			return;
		}

		// Distance LOD: a smooth fade between FadeStart and FadeEnd.
		const float distance = std::sqrt(Distance2(desc.Position, lastCamera));
		const float fadeEnd = std::max(Water.FadeEnd, Water.FadeStart + 0.01f);

		if (distance >= fadeEnd)
		{
			return;
		}

		const float fadeT = std::clamp((distance - Water.FadeStart) / (fadeEnd - Water.FadeStart), 0.0f, 1.0f);
		const float fade = 1.0f - fadeT * fadeT * (3.0f - 2.0f * fadeT);
		const float size = std::clamp(std::isfinite(desc.Size) ? desc.Size : 0.6f, 0.05f, 8.0f);
		const float vertical = std::abs(desc.Velocity[1]);
		const float horizontal = std::sqrt(desc.Velocity[0] * desc.Velocity[0] + desc.Velocity[2] * desc.Velocity[2]);
		// Impact energy: mostly the vertical speed for an entry, the sweep for a wake.
		const float speed = desc.Submerged ? horizontal * 0.6f + vertical * 0.3f : vertical + horizontal * 0.35f;
		const float energy = std::clamp(speed * std::sqrt(size) / 6.0f, 0.0f, 3.0f);

		if (energy < 0.02f)
		{
			return;
		}

		const int quality = static_cast<int>(Water.Quality);
		const Float3 at{ desc.Position[0], desc.Position[1], desc.Position[2] };

		// Rings: one per quality step, staggered; bigger and faster for harder hits.
		const int rings = desc.Submerged ? 1 : 1 + quality;

		for (int i = 0; i < rings; ++i)
		{
			Ripple ripple;
			ripple.Centre = at;
			ripple.Age = -0.09f * static_cast<float>(i);
			ripple.Amplitude = std::min(0.012f + 0.035f * energy, 0.12f) * (1.0f - 0.3f * static_cast<float>(i)) * Water.RippleScale;
			ripple.Speed = 0.9f + 0.5f * std::sqrt(energy) + size * 0.3f;
			ripple.Wavelength = 0.18f + size * 0.12f + 0.05f * static_cast<float>(i);
			ripple.Width = ripple.Wavelength * 0.9f;
			ripple.Lifetime = 2.2f + energy;
			ripples.push_back(ripple);
		}

		// Bound the ring list: the oldest go first.
		const std::size_t maxStored = std::max<std::size_t>(Water.MaxRipples, 1) * 3;

		if (ripples.size() > maxStored)
		{
			ripples.erase(ripples.begin(), ripples.begin() + static_cast<std::ptrdiff_t>(ripples.size() - maxStored));
		}

		if (!targets.Particles || (desc.Submerged && energy < 0.25f))
		{
			return;
		}

		const float detail = fade * std::clamp(Water.SplashScale, 0.0f, 4.0f);
		const auto count = [&](float n)
		{
			return static_cast<std::uint32_t>(std::max(n * detail, 0.0f));
		};
		const float k = std::sqrt(size);
		rng = rng * 1664525u + 1013904223u;
		const std::uint32_t seed = rng;
		const auto spawn = [&](ParticleEmitterDesc emitter)
		{
			if (emitter.Bursts.empty() || emitter.Bursts[0].Count == 0)
			{
				return;
			}

			if (const auto handle = CreateEmitter(emitter, at, Lifetime(emitter)))
			{
				transient.push_back({ *handle, time + Lifetime(emitter) });
			}
		};
		const Color water{ 0.75f, 0.85f, 0.95f, 0.85f };

		// Droplets thrown up and falling back, dying on the surface.
		{
			ParticleEmitterDesc drops = Burst(count((10.0f + 40.0f * energy) * k * (quality == 0 ? 0.4f : 1.0f)), seed);
			drops.Blend = ParticleBlendMode::AlphaBlend;
			drops.Material = ParticleMaterial::Soft;
			drops.Facing = ParticleFacing::Velocity;
			drops.Stretch = 0.02f;
			drops.Shape = ParticleShape::Disc;
			drops.ShapeExtent = { size * 0.45f, 0.0f, 0.0f };
			drops.ConeAngle = desc.Submerged ? 0.9f : 0.45f;
			drops.SpeedMin = 1.0f + 1.5f * energy;
			drops.SpeedMax = 2.0f + 4.0f * energy;
			drops.LifetimeMin = 0.4f;
			drops.LifetimeMax = 1.1f;
			drops.SizeMin = 0.03f * k;
			drops.SizeMax = 0.07f * k;
			drops.Gravity = { 0.0f, -9.81f, 0.0f };
			drops.Collision = true;
			drops.GroundHeight = at[1];
			drops.ExpireOnContact = true;
			drops.ColorOverLife = Gradient({ 0.0f, 1.0f }, { water, Color{ water[0], water[1], water[2], 0.5f } });
			spawn(drops);
		}

		if (quality >= 2 && !desc.Submerged && energy > 0.3f)
		{
			// Crown sheet: a ring of streaks rising from the rim of the hole.
			ParticleEmitterDesc crown = Burst(count(24.0f * k * std::min(energy, 2.0f)), seed + 1u);
			crown.Blend = ParticleBlendMode::AlphaBlend;
			crown.Material = ParticleMaterial::Soft;
			crown.Facing = ParticleFacing::Velocity;
			crown.Stretch = 0.06f;
			crown.Shape = ParticleShape::Disc;
			crown.ShapeExtent = { size * 0.55f, 0.0f, 0.0f };
			crown.SurfaceOnly = true;
			crown.DirectionMode = ParticleDirection::Outward;
			crown.Direction = { 0.0f, 1.0f, 0.0f };
			crown.SpeedMin = 1.5f + 1.5f * energy;
			crown.SpeedMax = 2.5f + 2.5f * energy;
			crown.LifetimeMin = 0.3f;
			crown.LifetimeMax = 0.6f;
			crown.SizeMin = 0.08f * k;
			crown.SizeMax = 0.14f * k;
			crown.Gravity = { 0.0f, -9.81f, 0.0f };
			crown.Collision = true;
			crown.GroundHeight = at[1];
			crown.ExpireOnContact = true;
			crown.ColorOverLife = Gradient({ 0.0f, 1.0f }, { Color{ 0.85f, 0.92f, 1.0f, 0.7f }, Color{ 0.85f, 0.92f, 1.0f, 0.0f } });
			crown.SizeOverLife = Curve({ 0.0f, 1.0f }, { 1.0f, 0.5f });
			spawn(crown);

			// Mist hanging over a hard hit.
			ParticleEmitterDesc mist = Burst(count(4.0f + 6.0f * energy), seed + 2u);
			mist.Blend = ParticleBlendMode::AlphaBlend;
			mist.Material = ParticleMaterial::Smoke;
			mist.Shape = ParticleShape::Disc;
			mist.ShapeExtent = { size * 0.6f, 0.0f, 0.0f };
			mist.DirectionMode = ParticleDirection::Outward;
			mist.SpeedMin = 0.2f;
			mist.SpeedMax = 0.6f + 0.4f * energy;
			mist.LifetimeMin = 0.6f;
			mist.LifetimeMax = 1.2f;
			mist.SizeMin = size * 0.6f;
			mist.SizeMax = size * 1.1f;
			mist.Gravity = { 0.0f, 0.2f, 0.0f };
			mist.Drag = 1.5f;
			mist.ColorOverLife = Gradient({ 0.0f, 0.2f, 1.0f },
				{ Color{ 0.9f, 0.95f, 1.0f, 0.0f }, Color{ 0.9f, 0.95f, 1.0f, 0.18f }, Color{ 0.9f, 0.95f, 1.0f, 0.0f } });
			mist.SizeOverLife = Curve({ 0.0f, 1.0f }, { 0.7f, 1.4f });
			spawn(mist);
		}

		if (quality >= 1)
		{
			// Foam patch spreading flat on the surface.
			ParticleEmitterDesc foam = Burst(count(desc.Submerged ? 1.0f : 2.0f + 2.0f * std::min(energy, 1.5f)), seed + 3u);
			foam.Blend = ParticleBlendMode::AlphaBlend;
			foam.Material = ParticleMaterial::Soft;
			foam.Facing = ParticleFacing::Horizontal;
			foam.Shape = ParticleShape::Disc;
			foam.ShapeExtent = { size * 0.3f, 0.0f, 0.0f };
			foam.SpeedMin = foam.SpeedMax = 0.0f;
			foam.LifetimeMin = 1.2f;
			foam.LifetimeMax = 2.0f + energy;
			foam.SizeMin = size * 1.0f;
			foam.SizeMax = size * (1.4f + 0.4f * energy);
			foam.RotationMax = 6.2832f;
			foam.Gravity = { 0.0f, 0.0f, 0.0f };
			foam.ColorOverLife = Gradient({ 0.0f, 0.15f, 1.0f },
				{ Color{ 0.95f, 0.97f, 1.0f, 0.0f }, Color{ 0.95f, 0.97f, 1.0f, 0.45f }, Color{ 0.95f, 0.97f, 1.0f, 0.0f } });
			foam.SizeOverLife = Curve({ 0.0f, 1.0f }, { 0.6f, 1.6f });
			spawn(foam);
		}
	}

	void VfxDirector::UpdateRipples(float deltaTime, const Float3& camera)
	{
		rippleRecords.clear();

		if (!VfxEngineDraws(Settings.Water))
		{
			ripples.clear();
			return;
		}

		std::erase_if(ripples,
			[&](Ripple& ripple)
			{
				ripple.Age += deltaTime;
				return ripple.Age > ripple.Lifetime;
			});

		const float fadeEnd = std::max(Water.FadeEnd, Water.FadeStart + 0.01f);
		std::vector<std::pair<float, const Ripple*>> visible;
		visible.reserve(ripples.size());

		for (const auto& ripple : ripples)
		{
			const float d2 = Distance2(ripple.Centre, camera);

			if (ripple.Age >= 0.0f && d2 < fadeEnd * fadeEnd)
			{
				visible.emplace_back(d2, &ripple);
			}
		}

		const std::size_t keep = std::min<std::size_t>(visible.size(), Water.MaxRipples);
		std::partial_sort(visible.begin(), visible.begin() + keep, visible.end(),
			[](const auto& a, const auto& b)
			{
				return a.first < b.first;
			});

		for (std::size_t i = 0; i < keep; ++i)
		{
			const Ripple& r = *visible[i].second;
			const float t = std::clamp((std::sqrt(visible[i].first) - Water.FadeStart) / (fadeEnd - Water.FadeStart), 0.0f, 1.0f);
			const float fade = 1.0f - t * t * (3.0f - 2.0f * t);
			// Fade in over the first frames and out over the last half second.
			const float life = std::min(std::clamp(r.Age / 0.05f, 0.0f, 1.0f), std::clamp((r.Lifetime - r.Age) / 0.5f, 0.0f, 1.0f));
			GpuWaterRipple record;
			record.Centre = { r.Centre[0], r.Centre[1], r.Centre[2], r.Age };
			record.Wave = { r.Amplitude * fade * life, r.Speed, r.Wavelength, r.Width };
			rippleRecords.push_back(record);
		}
	}

	// --- Frame -----------------------------------------------------------------------------

	void VfxDirector::Update(float deltaTime, const Float3& camera)
	{
		deltaTime = std::isfinite(deltaTime) ? std::clamp(deltaTime, 0.0f, 0.25f) : 0.0f;
		time += deltaTime;

		if (Finite(camera))
		{
			lastCamera = camera;
		}

		// Tier changes.
		if (applied.Particles != Settings.Particles)
		{
			for (auto& [handle, effect] : effects)
			{
				if (!effect.OneShot)
				{
					BuildEffect(effect);
				}
			}
		}

		if (applied.Lava != Settings.Lava && targets.Lava)
		{
			targets.Lava->Quads = VfxEngineDraws(Settings.Lava) ? lava : std::vector<LavaSurface::Quad>{};
		}

		if (applied.Explosions != Settings.Explosions && !VfxEngineDraws(Settings.Explosions))
		{
			for (auto& explosion : explosions)
			{
				ReleaseLight(explosion.Light);
			}

			explosions.clear();
		}

		applied = Settings;

		// Spent emitters, explosions and one-shot effects.
		std::erase_if(transient,
			[&](const Emitter& emitter)
			{
				if (time < emitter.ExpiresAt)
				{
					return false;
				}

				ReleaseEmitter(emitter.Handle);
				return true;
			});

		for (auto& explosion : explosions)
		{
			explosion.Age += deltaTime;
		}

		std::erase_if(explosions,
			[&](Explosion& explosion)
			{
				if (explosion.Age < ExplosionLifetime(explosion.Desc))
				{
					return false;
				}

				ReleaseLight(explosion.Light);
				return true;
			});

		std::erase_if(effects,
			[&](auto& entry)
			{
				Effect& effect = entry.second;

				if (!effect.OneShot || time < effect.ExpiresAt)
				{
					return false;
				}

				ClearEffect(effect);
				return true;
			});

		shake = std::max(0.0f, shake - deltaTime * 1.4f);
		GatherLavaCells(lastCamera);
		UpdateLava(deltaTime, lastCamera);
		UpdateLights(lastCamera);
		BuildVolumes(lastCamera);
		BuildFires(lastCamera);
		UpdateRipples(deltaTime, lastCamera);
	}

	void VfxDirector::UpdateLights(const Float3& camera)
	{
		const bool fireLights = targets.Lights && Settings.Lights && VfxEngineDraws(Settings.Fire);
		std::vector<std::pair<float, Fire*>> lit;

		for (auto& [handle, fire] : fires)
		{
			if (fireLights && fire.Desc.Light && Finite(fire.Desc.Position))
			{
				lit.emplace_back(Distance2(fire.Desc.Position, camera), &fire);
			}
			else
			{
				ReleaseLight(fire.Light);
			}
		}

		const std::size_t keep = std::min<std::size_t>(lit.size(), MaxFireLights);
		std::partial_sort(lit.begin(), lit.begin() + keep, lit.end(),
			[](const auto& a, const auto& b)
			{
				return a.first < b.first;
			});

		for (std::size_t i = keep; i < lit.size(); ++i)
		{
			ReleaseLight(lit[i].second->Light);
		}

		for (std::size_t i = 0; i < keep; ++i)
		{
			Fire& fire = *lit[i].second;
			const auto& d = fire.Desc;
			// The flicker the cards burn with, and a slow drift between deeper and paler orange.
			const float flicker = BlockFire::Flicker(std::fmod(time, 3600.0f), std::fmod(fire.Seed, 64.0f), 1.0f);
			const float drift = 0.5f + 0.5f * std::sin(time * 2.3f + fire.Seed * 5.0f);
			const float scale = d.Kind == FireKind::Campfire ? 1.4f : d.Kind == FireKind::Small ? 0.45f : 1.0f;
			LightDesc light;
			light.Type = LightType::Point;
			light.Position = { d.Position[0], d.Position[1] + d.Height * 0.5f, d.Position[2] };

			for (int c = 0; c < 3; ++c)
			{
				light.Color[c] = FireColor[c] + (d.Tint[c] - FireColor[c]) * std::clamp(d.TintMix, 0.0f, 1.0f);
			}

			light.Color[1] *= 0.9f + 0.15f * drift;
			light.Color[2] *= 0.75f + 0.4f * drift * flicker;
			light.Intensity = 30.0f * scale * std::max(d.Intensity, 0.0f) * std::max(d.Width, 0.2f) * flicker;
			light.Range = (d.Kind == FireKind::Small ? 5.0f : 9.0f) + d.Height * 2.0f;

			if (fire.Light)
			{
				targets.Lights->Update(*fire.Light, light);
			}
			else
			{
				fire.Light = targets.Lights->TryCreate(light);
			}
		}

		// Explosion pulses: a flash, then the fireball's glow dying away (the newest four).
		for (std::size_t i = 0; i < explosions.size(); ++i)
		{
			auto& explosion = explosions[i];
			const auto& d = explosion.Desc;
			const float a = explosion.Age;
			const float k = d.Size / 2.0f;
			const float strength = d.Intensity * k * k * (4000.0f * std::exp(-a * 14.0f) + 500.0f * std::exp(-a * 2.2f));

			if (!targets.Lights || !Settings.Lights || a > 2.0f || i + 4 < explosions.size())
			{
				ReleaseLight(explosion.Light);
				continue;
			}

			LightDesc light;
			light.Type = LightType::Point;
			light.Position = { d.Position[0], d.Position[1] + d.Size * 0.3f * std::min(a, 1.0f), d.Position[2] };
			light.Color = { 1.0f, 0.62f + 0.3f * std::exp(-a * 10.0f), 0.3f + 0.5f * std::exp(-a * 12.0f) };
			light.Intensity = strength;
			light.Range = d.Size * 6.0f;

			if (explosion.Light)
			{
				targets.Lights->Update(*explosion.Light, light);
			}
			else
			{
				explosion.Light = targets.Lights->TryCreate(light);
			}
		}
	}

	void VfxDirector::BuildVolumes(const Float3& camera)
	{
		if (!targets.Pyro)
		{
			return;
		}

		auto& volumes = targets.Pyro->Volumes;
		volumes.clear();
		using Type = PyroVolumes::Type;

		// Raymarched volumes are the costliest layer: under spam only the newest few keep one
		// fireball each, without the distortion shell or smoke.
		const std::size_t running = explosions.size();
		const std::size_t volumeBudget = running > 4 ? 3 : running > 2 ? 4 : 8;
		const std::size_t firstVolume = running > volumeBudget ? running - volumeBudget : 0;
		const int lumps = running > 2 ? 1 : 3;

		for (std::size_t e = firstVolume; e < explosions.size(); ++e)
		{
			const auto& explosion = explosions[e];
			const auto& d = explosion.Desc;
			const float a = explosion.Age;
			const float s = d.Size;
			const float duration = FireballDuration(d);

			if (a < 0.5f && running <= 3)
			{
				PyroVolumes::Volume wave;
				wave.Kind = Type::Shockwave;
				wave.Position = d.Position;
				wave.Radius = s * (0.3f + 2.7f * EaseOut(a / 0.5f));
				wave.Age = a / 0.5f;
				wave.Distortion = 1.5f * d.Intensity;
				volumes.push_back(wave);
			}

			// The fireball: a main ball and two lumps, swelling fast, rising, burning out and
			// fading (the shader fades the last 40% of Age).
			for (int lump = 0; lump < lumps && a < duration; ++lump)
			{
				const float t = a - (lump == 0 ? 0.0f : 0.03f * float(lump));

				if (t <= 0.0f)
				{
					continue;
				}

				volumes.push_back(FireballLump(d, explosion.Seed, lump, t, duration));
			}

			const float smoke = explosion.Detail >= 1 || (running > 1 && e + 1 < running) ? 0.0f : SmokeAmount(d);

			if (smoke > 0.0f && a > 0.15f)
			{
				volumes.push_back(SmokeColumn(d, explosion.Seed, a, smoke));
			}
		}

		// Heat shimmer over the nearest lava.
		for (std::size_t i = 0; i < lavaCells.size() && i < 6 && volumes.size() < PyroVolumes::MaxVolumes; ++i)
		{
			PyroVolumes::Volume haze;
			haze.Kind = Type::Haze;
			haze.Position = { lavaCells[i].Centre[0], lavaCells[i].Centre[1] + 0.9f, lavaCells[i].Centre[2] };
			haze.Radius = 4.5f;
			haze.Height = 1.3f;
			haze.Distortion = 0.6f;
			volumes.push_back(haze);
		}

		(void)camera;
	}

	void VfxDirector::BuildFires(const Float3& camera)
	{
		if (!targets.Fire)
		{
			return;
		}

		auto& cards = targets.Fire->Fires;
		cards.clear();

		if (!VfxEngineDraws(Settings.Fire))
		{
			return;
		}

		std::vector<std::pair<float, const Fire*>> near;
		const float reach2 = FireDistance * FireDistance;

		for (const auto& [handle, fire] : fires)
		{
			const float d2 = Distance2(fire.Desc.Position, camera);

			if (d2 < reach2 && Finite(fire.Desc.Position))
			{
				near.emplace_back(d2, &fire);
			}
		}

		std::sort(near.begin(), near.end(),
			[](const auto& a, const auto& b)
			{
				return a.first < b.first;
			});

		const bool particles = Settings.Particles != VfxQuality::Off;
		cards.reserve(near.size());

		for (const auto& [d2, fire] : near)
		{
			const auto& d = fire->Desc;
			BlockFire::Fire card;
			card.Layer = d.Layer;
			card.Frames = d.Frames;
			card.Rows = d.Rows;
			card.FrameRate = d.FrameRate;
			card.Intensity = d.Intensity;
			card.Seed = fire->Seed;
			card.Height = d.Height;
			card.Width = d.Width;

			for (int c = 0; c < 3; ++c)
			{
				card.Tint[c] = 1.0f + (d.Tint[c] - 1.0f) * std::clamp(d.TintMix, 0.0f, 1.0f);
			}

			switch (d.Kind)
			{
			case FireKind::Block:
				card.Kind = BlockFire::Shape::Block;
				card.Attach = d.Attach;
				card.Position = { d.Position[0] - 0.5f, d.Position[1], d.Position[2] - 0.5f };
				card.Embers = d.Embers ? 4u : 0u;
				card.Sparks = 1;
				card.Smoke = d.Smoke ? 3u : 1u;
				break;
			case FireKind::Campfire:
				card.Kind = BlockFire::Shape::Small;
				card.Position = d.Position;
				card.Width = d.Width * 0.9f;
				card.Height = d.Height * 0.9f;
				card.Embers = d.Embers ? 6u : 0u;
				card.Sparks = 2;
				card.Smoke = 3;
				break;
			case FireKind::Entity:
				card.Kind = BlockFire::Shape::Entity;
				card.Position = d.Position;
				card.Width = d.Width * 1.1f;
				card.Height = d.Height * 1.1f;
				card.Embers = d.Embers ? 5u : 0u;
				card.Sparks = 1;
				card.Smoke = d.Smoke ? 2u : 1u;
				break;
			default:
				card.Kind = BlockFire::Shape::Small;
				card.Position = d.Position;
				card.Embers = d.Embers ? 1u : 0u;
				card.Sparks = 0;
				card.Smoke = 0;
				break;
			}

			if (!particles)
			{
				card.Embers = card.Sparks = card.Smoke = 0;
			}

			cards.push_back(card);
		}
	}

	VfxDirector::Stats VfxDirector::GetStats() const
	{
		Stats stats;
		stats.Fires = static_cast<std::uint32_t>(fires.size());
		stats.Explosions = static_cast<std::uint32_t>(explosions.size());
		stats.Effects = static_cast<std::uint32_t>(effects.size());
		stats.Emitters = live;
		stats.Volumes = targets.Pyro ? static_cast<std::uint32_t>(targets.Pyro->Volumes.size()) : 0u;
		stats.DroppedEmitters = dropped;
		stats.Ripples = static_cast<std::uint32_t>(rippleRecords.size());

		for (const auto& [handle, fire] : fires)
		{
			stats.Lights += fire.Light ? 1u : 0u;
		}

		for (const auto& explosion : explosions)
		{
			stats.Lights += explosion.Light ? 1u : 0u;
		}

		return stats;
	}

} // namespace Engine
