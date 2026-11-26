#include "Engine/Systems/Renderer/Vfx/VfxDirector.h"

#include "Engine/Systems/Renderer/Lights/GpuLightBuffer.h"
#include "Engine/Systems/Renderer/Particles/ParticleSystem.h"
#include "Engine/Systems/Renderer/Runtime/FrameRenderer.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace Engine
{

	namespace
	{

		using namespace Swim::Render;
		using Float3 = VfxDirector::Float3;
		using Color = std::array<float, 4>;

		constexpr float Infinity = std::numeric_limits<float>::infinity();
		constexpr Float3 FireColor{ 1.0f, 0.55f, 0.22f };

		std::array<float, 12> Translation(const Float3& p)
		{
			return { 1, 0, 0, p[0], 0, 1, 0, p[1], 0, 0, 1, p[2] };
		}

		float Distance2(const Float3& a, const Float3& b)
		{
			const float x = a[0] - b[0];
			const float y = a[1] - b[1];
			const float z = a[2] - b[2];
			return x * x + y * y + z * z;
		}

		bool Finite(const Float3& p)
		{
			return std::isfinite(p[0]) && std::isfinite(p[1]) && std::isfinite(p[2]);
		}

		float EaseOut(float t)
		{
			t = std::clamp(t, 0.0f, 1.0f);
			return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
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

		// A one-shot burst at the start of a short cycle.
		ParticleEmitterDesc Burst(std::uint32_t count, std::uint32_t seed, float delay = 0.0f)
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

		float Lifetime(const ParticleEmitterDesc& desc)
		{
			return desc.Looping ? Infinity : desc.Duration + desc.LifetimeMax + 0.1f;
		}

		float FireballDuration(const VfxDirector::ExplosionDesc& desc)
		{
			const float fiery = desc.Style == VfxDirector::ExplosionStyle::Fiery ? 1.6f : 1.0f;
			return 1.1f * fiery * std::pow(std::max(desc.Size, 0.5f) / 4.0f, 0.3f);
		}

		float ExplosionLifetime(const VfxDirector::ExplosionDesc& desc)
		{
			return std::max(desc.Smoke ? 2.2f : 1.0f, FireballDuration(desc) + 0.1f);
		}

		float SmokeAmount(const VfxDirector::ExplosionDesc& desc)
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

		std::uint64_t CellKey(std::int32_t x, std::int32_t z)
		{
			return (std::uint64_t(std::uint32_t(x)) << 32) | std::uint32_t(z);
		}

	} // namespace

	std::unique_ptr<VfxDirector> VfxDirector::Install(FrameRenderer& renderer)
	{
		Targets targets;
		targets.Particles = &renderer.GetParticles();
		targets.Lights = &renderer.GetLights();
		targets.Pyro = std::make_shared<PyroVolumes>();
		targets.Lava = std::make_shared<LavaSurface>();
		targets.Fire = std::make_shared<BlockFire>();
		renderer.AddFeature(targets.Pyro);
		renderer.AddFeature(targets.Lava);
		renderer.AddFeature(targets.Fire);
		return std::make_unique<VfxDirector>(std::move(targets));
	}

	VfxDirector::VfxDirector(Targets targetsInput) : targets(std::move(targetsInput))
	{
		applied = Settings;
	}

	VfxDirector::~VfxDirector()
	{
		for (auto& [handle, fire] : fires)
		{
			ReleaseLight(fire.Light);
		}

		for (auto& [handle, effect] : effects)
		{
			ClearEffect(effect);
		}

		for (auto& explosion : explosions)
		{
			ReleaseLight(explosion.Light);
		}

		for (const auto& emitter : transient)
		{
			ReleaseEmitter(emitter.Handle);
		}

		for (auto& cell : lavaCells)
		{
			ReleaseLight(cell.Light);

			if (cell.Embers)
			{
				ReleaseEmitter(*cell.Embers);
			}
		}

		if (targets.Fire)
		{
			targets.Fire->Fires.clear();
		}

		if (targets.Pyro)
		{
			targets.Pyro->Volumes.clear();
		}

		if (targets.Lava)
		{
			targets.Lava->Quads.clear();
		}
	}

	// --- Emitters and lights ---------------------------------------------------------------

	std::optional<ParticleEmitterHandle> VfxDirector::CreateEmitter(const ParticleEmitterDesc& desc, const Float3& position, float lifetime)
	{
		if (!targets.Particles || live >= MaxEmitters || !Finite(position))
		{
			++dropped;
			return std::nullopt;
		}

		std::optional<ParticleEmitterHandle> handle;

		try
		{
			handle = targets.Particles->TryCreateEmitter(desc, Translation(position));
		}
		catch (const std::exception&)
		{
			handle.reset();
		}

		if (!handle)
		{
			++dropped;
			return std::nullopt;
		}

		++live;
		(void)lifetime;
		return handle;
	}

	void VfxDirector::ReleaseEmitter(ParticleEmitterHandle handle)
	{
		if (targets.Particles && targets.Particles->Release(handle))
		{
			live = live > 0 ? live - 1 : 0;
		}
	}

	void VfxDirector::ReleaseLight(std::optional<GpuLightHandle>& light)
	{
		if (light && targets.Lights)
		{
			targets.Lights->Release(*light);
		}

		light.reset();
	}

	// --- Fire ------------------------------------------------------------------------------

	VfxDirector::Handle VfxDirector::CreateFire(const FireDesc& desc)
	{
		const Handle handle = next++;
		Fire& fire = fires[handle];
		fire.Desc = desc;
		rng = rng * 1664525u + 1013904223u;
		fire.Seed = float(rng >> 8) / float(1u << 24) * 100.0f;
		return handle;
	}

	bool VfxDirector::SetFire(Handle handle, const FireDesc& desc)
	{
		const auto found = fires.find(handle);

		if (found == fires.end())
		{
			return false;
		}

		found->second.Desc = desc;
		return true;
	}

	bool VfxDirector::SetFirePosition(Handle handle, const Float3& position)
	{
		const auto found = fires.find(handle);

		if (found == fires.end() || !Finite(position))
		{
			return false;
		}

		found->second.Desc.Position = position;
		return true;
	}

	bool VfxDirector::RemoveFire(Handle handle)
	{
		const auto found = fires.find(handle);

		if (found == fires.end())
		{
			return false;
		}

		ReleaseLight(found->second.Light);
		fires.erase(found);
		return true;
	}

	// --- Lava ------------------------------------------------------------------------------

	VfxDirector::Handle VfxDirector::AddFire(
		const Float3& cell, FireAttachment attach, std::uint32_t layer, std::uint32_t frames, float size)
	{
		FireDesc desc;
		desc.Kind = FireKind::Block;
		desc.Position = { cell[0] + 0.5f, cell[1], cell[2] + 0.5f };
		desc.Attach = attach;
		desc.Layer = layer;
		desc.Frames = std::max(frames, 1u);
		desc.Height = std::isfinite(size) ? std::clamp(size, 0.1f, 4.0f) : 1.0f;
		return CreateFire(desc);
	}

	void VfxDirector::SetFireTexture(Swim::Rhi::TextureView* texture)
	{
		if (targets.Fire)
		{
			targets.Fire->Texture = texture;
		}
	}

	void VfxDirector::SetWind(const WindSettings& wind)
	{
		if (targets.Fire)
		{
			targets.Fire->Wind = wind;
		}
	}

	VfxDirector::Handle VfxDirector::AddFuseSmoke(const Float3& position)
	{
		SimpleParticle smoke;
		smoke.Hint = ParticleHint::Smoke;
		smoke.Color = { 0.72f, 0.72f, 0.72f, 0.55f };
		smoke.Size = 0.22f;
		smoke.Velocity = { 0.0f, 0.7f, 0.0f };
		smoke.Spread = 0.35f;
		smoke.Lifetime = 1.3f;
		smoke.Gravity = { 0.0f, 0.4f, 0.0f };
		smoke.Drag = 0.8f;
		smoke.Rate = 14.0f;
		smoke.Area = { 0.06f, 0.0f, 0.06f };
		SimpleParticle sparks;
		sparks.Hint = ParticleHint::Spark;
		sparks.Color = { 1.0f, 0.75f, 0.35f, 1.0f };
		sparks.Size = 0.04f;
		sparks.Velocity = { 0.0f, 1.6f, 0.0f };
		sparks.Spread = 0.9f;
		sparks.Lifetime = 0.3f;
		sparks.Gravity = { 0.0f, -6.0f, 0.0f };
		sparks.Rate = 12.0f;
		sparks.Emissive = 1.0f;
		const auto start = [&](const SimpleParticle& particle)
		{
			const Handle handle = next++;
			Effect& effect = effects[handle];
			effect.Particle = particle;
			effect.Position = position;
			effect.EnhancedOnly = true;
			BuildEffect(effect);
			return handle;
		};
		const Handle handle = next++;
		fuses[handle] = { start(smoke), start(sparks) };
		return handle;
	}

	bool VfxDirector::SetFuseSmokePosition(Handle handle, const Float3& position)
	{
		const auto found = fuses.find(handle);

		if (found == fuses.end())
		{
			return false;
		}

		SetParticleEffectPosition(found->second.Smoke, position);
		SetParticleEffectPosition(found->second.Sparks, position);
		return true;
	}

	bool VfxDirector::RemoveFuseSmoke(Handle handle)
	{
		const auto found = fuses.find(handle);

		if (found == fuses.end())
		{
			return false;
		}

		RemoveParticleEffect(found->second.Smoke);
		RemoveParticleEffect(found->second.Sparks);
		fuses.erase(found);
		return true;
	}

	VfxDirector::ExplosionDesc VfxDirector::ExplosionDesc::FromPower(const Float3& position, float power, ExplosionStyle style)
	{
		power = std::isfinite(power) ? std::clamp(power, 0.25f, 32.0f) : 4.0f;
		ExplosionDesc desc;
		desc.Position = position;
		// The fireball's visible radius (1.2 Size) then matches the blast's real reach.
		desc.Size = power * 0.5f;
		desc.Intensity = std::clamp(0.45f + power * 0.12f, 0.3f, 2.5f);
		desc.Style = style;
		desc.Debris = power >= 2.0f;
		desc.Smoke = style != ExplosionStyle::Smokeless;

		if (style == ExplosionStyle::Bright)
		{
			desc.Intensity = std::min(desc.Intensity * 1.3f, 3.0f);
			desc.Debris = false;
		}

		return desc;
	}

	void VfxDirector::SetLava(std::vector<LavaSurface::Quad> quads)
	{
		lava = std::move(quads);

		if (targets.Lava)
		{
			targets.Lava->Quads = VfxEngineDraws(Settings.Lava) ? lava : std::vector<LavaSurface::Quad>{};
		}
	}

	void VfxDirector::GatherLavaCells(const Float3& camera)
	{
		std::vector<LavaCell> gathered;

		if (VfxEngineDraws(Settings.Lava))
		{
			struct Sum
			{
				Float3 Total{};
				float Count = 0.0f;
				float MaxY = -1.0e9f;
			};

			std::unordered_map<std::uint64_t, Sum> sums;
			const float cellSize = 6.0f;

			for (const auto& quad : lava)
			{
				const auto corners = quad.GetCorners();
				Float3 centre{};

				for (const auto& corner : corners)
				{
					for (int c = 0; c < 3; ++c)
					{
						centre[c] += corner[c] * 0.25f;
					}
				}

				if (!Finite(centre) || Distance2(centre, camera) > 48.0f * 48.0f)
				{
					continue;
				}

				Sum& sum = sums[CellKey(static_cast<std::int32_t>(std::floor(centre[0] / cellSize)),
					static_cast<std::int32_t>(std::floor(centre[2] / cellSize)))];

				for (int c = 0; c < 3; ++c)
				{
					sum.Total[c] += centre[c];
				}

				sum.Count += 1.0f;
				sum.MaxY = std::max(sum.MaxY, centre[1]);
			}

			for (const auto& [key, sum] : sums)
			{
				LavaCell cell;
				cell.Key = key;
				cell.Centre = { sum.Total[0] / sum.Count, sum.MaxY, sum.Total[2] / sum.Count };
				cell.Faces = sum.Count;
				gathered.push_back(cell);
			}

			std::sort(gathered.begin(), gathered.end(),
				[&](const LavaCell& x, const LavaCell& y)
				{
					return Distance2(x.Centre, camera) < Distance2(y.Centre, camera);
				});
		}

		// Keep the lights and emitters of cells that are still there.
		for (auto& old : lavaCells)
		{
			const auto found = std::find_if(gathered.begin(), gathered.end(),
				[&](const LavaCell& cell)
				{
					return cell.Key == old.Key;
				});

			if (found != gathered.end())
			{
				found->Light = old.Light;
				found->Embers = old.Embers;
				continue;
			}

			ReleaseLight(old.Light);

			if (old.Embers)
			{
				// Its embers finish their flight.
				if (targets.Particles)
				{
					targets.Particles->SetEmitting(*old.Embers, false);
				}

				transient.push_back({ *old.Embers, time + 4.0f });
			}
		}

		lavaCells = std::move(gathered);
	}

	void VfxDirector::UpdateLava(float deltaTime, const Float3& camera)
	{
		const bool enhanced = VfxEngineDraws(Settings.Lava) && !lava.empty();

		// Per nearby cell: a red-orange light on the surroundings and rising embers.
		for (std::size_t i = 0; i < lavaCells.size(); ++i)
		{
			LavaCell& cell = lavaCells[i];
			const Float3 above{ cell.Centre[0], cell.Centre[1] + 1.2f, cell.Centre[2] };

			if (enhanced && targets.Lights && Settings.Lights && i < MaxLavaLights)
			{
				LightDesc light;
				light.Type = LightType::Point;
				light.Position = above;
				const float throb = 0.92f + 0.08f * std::sin(time * 1.3f + float(cell.Key & 63u));
				light.Color = { 1.0f, 0.3f, 0.07f }; // Lights the rock red-orange.
				light.Intensity = (40.0f + 6.0f * std::min(cell.Faces, 30.0f)) * throb;
				light.Range = 12.0f;

				if (cell.Light)
				{
					targets.Lights->Update(*cell.Light, light);
				}
				else
				{
					cell.Light = targets.Lights->TryCreate(light);
				}
			}
			else
			{
				ReleaseLight(cell.Light);
			}

			if (enhanced && targets.Particles && i < 4 && !cell.Embers)
			{
				ParticleEmitterDesc embers;
				embers.Material = ParticleMaterial::Spark;
				embers.Rate = 6.0f + std::min(cell.Faces, 30.0f) * 0.5f;
				embers.Duration = 0.0f;
				embers.Looping = true;
				embers.Shape = ParticleShape::Box;
				embers.ShapeExtent = { 3.0f, 0.05f, 3.0f };
				embers.ConeAngle = 0.4f;
				embers.SpeedMin = 0.4f;
				embers.SpeedMax = 1.2f;
				embers.LifetimeMin = 1.5f;
				embers.LifetimeMax = 3.5f;
				embers.SizeMin = 0.03f;
				embers.SizeMax = 0.06f;
				embers.Gravity = { 0.0f, 0.5f, 0.0f };
				embers.Drag = 0.6f;
				embers.Capacity = 160;
				embers.Seed = static_cast<std::uint32_t>(cell.Key * 2654435761u);
				embers.ColorOverLife = Gradient({ 0.0f, 0.1f, 0.6f, 1.0f },
					{ Color{ 7.0f, 3.2f, 0.7f, 0.0f }, Color{ 7.0f, 3.2f, 0.7f, 1.0f }, Color{ 3.5f, 0.9f, 0.12f, 0.8f },
						Color{ 1.0f, 0.15f, 0.02f, 0.0f } });
				cell.Embers = CreateEmitter(embers, { cell.Centre[0], cell.Centre[1] + 0.05f, cell.Centre[2] }, Infinity);
			}
			else if (cell.Embers && (!enhanced || i >= 4))
			{
				if (targets.Particles)
				{
					targets.Particles->SetEmitting(*cell.Embers, false);
				}

				transient.push_back({ *cell.Embers, time + 4.0f });
				cell.Embers.reset();
			}
		}

		if (!enhanced || !targets.Particles)
		{
			return;
		}

		// Pops: a few per second from random upward faces near the camera.
		popClock += deltaTime;
		const float interval = 0.45f;

		while (popClock > interval)
		{
			popClock -= interval;
			std::optional<std::array<Float3, 4>> chosen;

			for (int attempt = 0; attempt < 6 && !chosen; ++attempt)
			{
				rng = rng * 1664525u + 1013904223u;
				const auto corners = lava[(rng >> 8) % lava.size()].GetCorners();
				const float ax = corners[2][0] - corners[0][0];
				const float az = corners[2][2] - corners[0][2];
				const float bx = corners[3][0] - corners[1][0];
				const float bz = corners[3][2] - corners[1][2];
				const float ny = az * bx - ax * bz;
				const Float3 centre{ 0.25f * (corners[0][0] + corners[1][0] + corners[2][0] + corners[3][0]),
					0.25f * (corners[0][1] + corners[1][1] + corners[2][1] + corners[3][1]),
					0.25f * (corners[0][2] + corners[1][2] + corners[2][2] + corners[3][2]) };

				if (std::abs(ny) > 0.1f && Distance2(centre, camera) < 28.0f * 28.0f)
				{
					chosen = corners;
				}
			}

			if (!chosen)
			{
				continue;
			}

			rng = rng * 1664525u + 1013904223u;
			const float u = float((rng >> 8) & 0xFFFF) / 65535.0f;
			const float v = float(rng >> 24) / 255.0f;
			const auto& c = *chosen;
			Float3 at{};

			for (int k = 0; k < 3; ++k)
			{
				at[k] = (c[0][k] * (1.0f - u) + c[1][k] * u) * (1.0f - v) + (c[3][k] * (1.0f - u) + c[2][k] * u) * v;
			}

			at[1] += 0.02f;
			ParticleEmitterDesc drops = Burst(7 + (rng & 7u), rng);
			drops.Material = ParticleMaterial::Spark;
			drops.Facing = ParticleFacing::Velocity;
			drops.Stretch = 0.04f;
			drops.ConeAngle = 0.5f;
			drops.SpeedMin = 1.5f;
			drops.SpeedMax = 3.2f;
			drops.LifetimeMin = 0.5f;
			drops.LifetimeMax = 0.9f;
			drops.SizeMin = 0.05f;
			drops.SizeMax = 0.09f;
			drops.Gravity = { 0.0f, -9.81f, 0.0f };
			drops.Collision = true;
			drops.GroundHeight = at[1];
			drops.ExpireOnContact = true;
			drops.ColorOverLife = Gradient({ 0.0f, 0.6f, 1.0f },
				{ Color{ 6.0f, 2.2f, 0.4f, 1.0f }, Color{ 3.0f, 0.8f, 0.1f, 1.0f }, Color{ 1.0f, 0.15f, 0.02f, 0.0f } });

			if (const auto handle = CreateEmitter(drops, at, Lifetime(drops)))
			{
				transient.push_back({ *handle, time + Lifetime(drops) });
			}
		}
	}

	// --- Explosions --------------------------------------------------------------------

	void VfxDirector::SpawnExplosion(const ExplosionDesc& input)
	{
		if (!Finite(input.Position))
		{
			return;
		}

		Explosion explosion;
		explosion.Desc = input;
		explosion.Desc.Size = std::clamp(std::isfinite(input.Size) ? input.Size : 4.0f, 0.25f, 64.0f);
		explosion.Desc.Intensity = std::clamp(std::isfinite(input.Intensity) ? input.Intensity : 1.0f, 0.0f, 4.0f);
		rng = rng * 1664525u + 1013904223u;
		explosion.Seed = float(rng >> 8) / float(1u << 24) * 100.0f;

		if (Settings.CameraShake && VfxEngineDraws(Settings.Explosions))
		{
			// Strength from size and intensity, falling to zero at 12 blast radii.
			const float distance = std::sqrt(Distance2(lastCamera, explosion.Desc.Position));
			const float reach = explosion.Desc.Size * 12.0f;
			const float strength =
				std::clamp(explosion.Desc.Intensity * std::sqrt(explosion.Desc.Size / 4.0f) * (1.0f - distance / reach), 0.0f, 1.0f);

			if (strength > 0.0f)
			{
				shake = std::min(1.0f, shake + strength);

				if (OnCameraShake)
				{
					OnCameraShake(strength, 0.35f + 0.25f * std::sqrt(explosion.Desc.Size / 4.0f));
				}
			}
		}

		if (!VfxEngineDraws(Settings.Explosions))
		{
			return;
		}

		// Under spam: the more blasts already running, the fewer layers a new one gets, and
		// the oldest make way past MaxExplosions.
		std::uint32_t recent = 0;

		for (const auto& other : explosions)
		{
			recent += other.Age < 0.6f ? 1u : 0u;
		}

		explosion.Detail = recent >= 4 ? 2u : recent >= 2 ? 1u : 0u;

		while (!explosions.empty() && explosions.size() >= std::max(MaxExplosions, 1u))
		{
			ReleaseLight(explosions.front().Light);
			explosions.erase(explosions.begin());
		}

		StartExplosionParticles(explosion);
		explosions.push_back(std::move(explosion));
	}

	void VfxDirector::StartExplosionParticles(const Explosion& explosion)
	{
		const auto& d = explosion.Desc;
		const float s = d.Size;
		const float k = s / 4.0f;
		const float intensity = std::max(d.Intensity, 0.05f);
		const float ground = d.GroundHeight && std::isfinite(*d.GroundHeight) ? *d.GroundHeight : d.Position[1] - s * 0.25f;
		const std::uint32_t seed = static_cast<std::uint32_t>(explosion.Seed * 1000.0f);
		const auto spawn = [&](ParticleEmitterDesc desc, const Float3& at)
		{
			if (const auto handle = CreateEmitter(desc, at, Lifetime(desc)))
			{
				transient.push_back({ *handle, time + Lifetime(desc) });
			}
		};

		// In order of importance: later layers are the first to go when the budget is short.

		// Flash: one huge glow for a few frames.
		{
			ParticleEmitterDesc flash = Burst(1, seed);
			flash.Material = ParticleMaterial::Glow;
			flash.SpeedMin = flash.SpeedMax = 0.0f;
			flash.LifetimeMin = flash.LifetimeMax = 0.12f;
			flash.SizeMin = flash.SizeMax = s * 3.0f;
			flash.Gravity = { 0.0f, 0.0f, 0.0f };
			flash.ColorOverLife = Gradient({ 0.0f, 1.0f },
				{ Color{ 30.0f * intensity, 22.0f * intensity, 12.0f * intensity, 1.0f }, Color{ 6.0f, 2.5f, 0.6f, 0.0f } });
			spawn(flash, d.Position);
		}

		if (explosion.Detail >= 2)
		{
			return;
		}

		// Sparks: fast, bouncing streaks.
		{
			ParticleEmitterDesc sparks = Burst(static_cast<std::uint32_t>(70.0f * intensity * std::sqrt(k)) + 10u, seed + 1u);
			sparks.Material = ParticleMaterial::Spark;
			sparks.Facing = ParticleFacing::Velocity;
			sparks.Stretch = 0.035f;
			sparks.RotationMax = 0.0f;
			sparks.Shape = ParticleShape::Sphere;
			sparks.ShapeExtent = { s * 0.2f, 0.0f, 0.0f };
			sparks.ConeAngle = 1.5f;
			sparks.SpeedMin = s * 2.0f;
			sparks.SpeedMax = s * 5.5f;
			sparks.LifetimeMin = 0.5f;
			sparks.LifetimeMax = 1.4f;
			sparks.SizeMin = 0.05f * std::sqrt(k);
			sparks.SizeMax = 0.1f * std::sqrt(k);
			sparks.Gravity = { 0.0f, -9.81f, 0.0f };
			sparks.Drag = 0.9f;
			sparks.Collision = true;
			sparks.GroundHeight = ground;
			sparks.Restitution = 0.35f;
			sparks.Friction = 0.4f;
			sparks.ColorOverLife = Gradient({ 0.0f, 0.4f, 1.0f },
				{ Color{ 9.0f, 5.0f, 1.6f, 1.0f }, Color{ 5.0f, 1.6f, 0.25f, 1.0f }, Color{ 1.2f, 0.2f, 0.02f, 0.0f } });
			sparks.SizeOverLife = Curve({ 0.0f, 1.0f }, { 1.0f, 0.4f });
			spawn(sparks, d.Position);
		}

		// Smoke puffs breaking up the fireball's edge and feeding the column.
		if (SmokeAmount(d) > 0.0f)
		{
			ParticleEmitterDesc smoke = Burst(static_cast<std::uint32_t>(18.0f * std::sqrt(k) * SmokeAmount(d)) + 4u, seed + 2u, 0.08f);
			smoke.Blend = ParticleBlendMode::AlphaBlend;
			smoke.Material = ParticleMaterial::Smoke;
			smoke.Shape = ParticleShape::Sphere;
			smoke.ShapeExtent = { s * 0.45f, 0.0f, 0.0f };
			smoke.DirectionMode = ParticleDirection::Outward;
			smoke.SpeedMin = s * 0.15f;
			smoke.SpeedMax = s * 0.45f;
			smoke.LifetimeMin = 0.7f;
			smoke.LifetimeMax = 1.3f;
			smoke.SizeMin = s * 0.5f;
			smoke.SizeMax = s * 0.9f;
			smoke.Gravity = { 0.0f, 0.5f, 0.0f };
			smoke.Drag = 0.9f;
			smoke.AngularVelocityMin = -0.3f;
			smoke.AngularVelocityMax = 0.3f;
			smoke.ColorOverLife = Gradient({ 0.0f, 0.08f, 0.6f, 1.0f },
				{ Color{ 1.2f, 0.5f, 0.15f, 0.0f }, Color{ 0.2f, 0.18f, 0.17f, 0.5f }, Color{ 0.16f, 0.15f, 0.15f, 0.3f },
					Color{ 0.15f, 0.15f, 0.15f, 0.0f } });
			smoke.SizeOverLife = Curve({ 0.0f, 1.0f }, { 1.0f, 2.6f });
			spawn(smoke, d.Position);
		}

		if (explosion.Detail >= 1)
		{
			return;
		}

		// Debris: dark chunks thrown up and out (upward ones farther), bouncing, each
		// trailing smoke (the trail layer shares the seed, so it follows the same chunks).
		if (d.Debris)
		{
			ParticleEmitterDesc debris = Burst(static_cast<std::uint32_t>(26.0f * intensity * std::sqrt(k)) + 4u, seed + 3u);
			debris.Blend = ParticleBlendMode::AlphaBlend;
			debris.Material = ParticleMaterial::Soft;
			debris.ConeAngle = 1.0f;
			debris.SpeedMin = s * 1.5f;
			debris.SpeedMax = s * 3.8f;
			debris.LifetimeMin = 1.4f;
			debris.LifetimeMax = 2.6f;
			debris.SizeMin = 0.12f * k;
			debris.SizeMax = 0.26f * k;
			debris.Gravity = { 0.0f, -18.0f, 0.0f };
			debris.Drag = 0.3f;
			debris.Collision = true;
			debris.GroundHeight = ground;
			debris.Restitution = 0.25f;
			debris.Friction = 0.6f;
			debris.AngularVelocityMin = -8.0f;
			debris.AngularVelocityMax = 8.0f;
			debris.ColorOverLife = Gradient({ 0.0f, 0.85f, 1.0f },
				{ Color{ 0.06f, 0.05f, 0.045f, 1.0f }, Color{ 0.05f, 0.045f, 0.04f, 1.0f }, Color{ 0.05f, 0.045f, 0.04f, 0.0f } });
			ParticleEmitterDesc trail = debris;
			spawn(debris, d.Position);
			trail.Material = ParticleMaterial::Smoke;
			trail.Facing = ParticleFacing::Velocity;
			trail.Stretch = 0.22f;
			trail.RotationMin = trail.RotationMax = 0.0f;
			trail.AngularVelocityMin = trail.AngularVelocityMax = 0.0f;
			trail.SizeMin *= 1.6f;
			trail.SizeMax *= 1.6f;
			trail.ColorOverLife = Gradient({ 0.0f, 0.15f, 0.7f, 1.0f },
				{ Color{ 1.5f, 0.6f, 0.2f, 0.6f }, Color{ 0.25f, 0.23f, 0.22f, 0.45f }, Color{ 0.2f, 0.2f, 0.2f, 0.2f },
					Color{ 0.2f, 0.2f, 0.2f, 0.0f } });

			if (SmokeAmount(d) >= 0.5f)
			{
				spawn(trail, d.Position);
			}
		}

		// The shockwave ring along the ground.
		{
			ParticleEmitterDesc ring = Burst(1, seed + 4u);
			ring.Material = ParticleMaterial::Ring;
			ring.Facing = ParticleFacing::Horizontal;
			ring.RotationMax = 0.0f;
			ring.SpeedMin = ring.SpeedMax = 0.0f;
			ring.LifetimeMin = ring.LifetimeMax = 0.55f;
			ring.SizeMin = ring.SizeMax = s * 5.0f;
			ring.Gravity = { 0.0f, 0.0f, 0.0f };
			ring.SizeOverLife = Curve({ 0.0f, 0.4f, 1.0f }, { 0.08f, 0.75f, 1.0f });
			ring.ColorOverLife = Gradient({ 0.0f, 0.3f, 1.0f },
				{ Color{ 4.0f * intensity, 3.0f * intensity, 2.0f * intensity, 0.9f }, Color{ 1.5f, 1.0f, 0.7f, 0.5f },
					Color{ 0.6f, 0.5f, 0.4f, 0.0f } });
			spawn(ring, { d.Position[0], ground + 0.06f, d.Position[2] });
		}

		// Embers drifting up from the blast.
		{
			const float fiery = d.Style == VfxDirector::ExplosionStyle::Fiery ? 1.4f : 1.0f;
			ParticleEmitterDesc embers = Burst(static_cast<std::uint32_t>(40.0f * fiery * intensity * std::sqrt(k)) + 6u, seed + 5u, 0.1f);
			embers.Material = ParticleMaterial::Spark;
			embers.Shape = ParticleShape::Sphere;
			embers.ShapeExtent = { s * 0.6f, 0.0f, 0.0f };
			embers.DirectionMode = ParticleDirection::Outward;
			embers.SpeedMin = 0.8f;
			embers.SpeedMax = 3.0f;
			embers.LifetimeMin = 1.5f;
			embers.LifetimeMax = 3.5f;
			embers.SizeMin = 0.03f;
			embers.SizeMax = 0.06f;
			embers.Gravity = { 0.0f, 0.8f, 0.0f };
			embers.Drag = 1.2f;
			embers.ColorOverLife = Gradient({ 0.0f, 0.5f, 1.0f },
				{ Color{ 6.0f, 2.4f, 0.5f, 1.0f }, Color{ 3.0f, 0.8f, 0.1f, 0.8f }, Color{ 0.8f, 0.1f, 0.02f, 0.0f } });
			spawn(embers, d.Position);
		}

		// Secondary sparks, from where the leaders have got to.
		{
			ParticleEmitterDesc secondary = Burst(static_cast<std::uint32_t>(40.0f * intensity * std::sqrt(k)) + 6u, seed + 6u, 0.15f);
			secondary.Material = ParticleMaterial::Spark;
			secondary.Facing = ParticleFacing::Velocity;
			secondary.Stretch = 0.03f;
			secondary.RotationMax = 0.0f;
			secondary.Shape = ParticleShape::Sphere;
			secondary.ShapeExtent = { s * 0.9f, 0.0f, 0.0f };
			secondary.SurfaceOnly = true;
			secondary.DirectionMode = ParticleDirection::Outward;
			secondary.SpeedMin = s * 0.8f;
			secondary.SpeedMax = s * 2.0f;
			secondary.LifetimeMin = 0.3f;
			secondary.LifetimeMax = 0.8f;
			secondary.SizeMin = 0.03f * std::sqrt(k);
			secondary.SizeMax = 0.06f * std::sqrt(k);
			secondary.Gravity = { 0.0f, -9.81f, 0.0f };
			secondary.Drag = 1.0f;
			secondary.ColorOverLife = Gradient({ 0.0f, 1.0f }, { Color{ 7.0f, 3.5f, 1.0f, 1.0f }, Color{ 1.5f, 0.3f, 0.03f, 0.0f } });
			spawn(secondary, d.Position);
		}
	}

	// --- Particles -----------------------------------------------------------------------

	void VfxDirector::BuildEffect(Effect& effect)
	{
		ClearEffect(effect);
		const VfxQuality quality = effect.EnhancedOnly
			? (Settings.Particles == VfxQuality::Enhanced ? VfxQuality::Enhanced : VfxQuality::Off)
			: Settings.Particles;
		effect.Built = quality;
		const EnhancedParticle enhanced = EnhanceParticle(effect.Particle, quality);
		effect.ExpiresAt = time + enhanced.Lifetime;

		for (const auto& layer : enhanced.Layers)
		{
			if (const auto handle = CreateEmitter(layer, effect.Position, enhanced.Lifetime))
			{
				effect.Emitters.push_back({ *handle, effect.ExpiresAt });
			}
		}
	}

	void VfxDirector::ClearEffect(Effect& effect)
	{
		for (const auto& emitter : effect.Emitters)
		{
			ReleaseEmitter(emitter.Handle);
		}

		effect.Emitters.clear();
		ReleaseLight(effect.Light);
	}

	VfxDirector::Handle VfxDirector::CreateParticleEffect(const SimpleParticle& particle, const Float3& position)
	{
		const Handle handle = next++;
		Effect& effect = effects[handle];
		effect.Particle = particle;
		effect.Position = position;
		BuildEffect(effect);
		return handle;
	}

	bool VfxDirector::SetParticleEffectPosition(Handle handle, const Float3& position)
	{
		const auto found = effects.find(handle);

		if (found == effects.end() || !Finite(position))
		{
			return false;
		}

		found->second.Position = position;

		if (targets.Particles)
		{
			for (const auto& emitter : found->second.Emitters)
			{
				targets.Particles->SetTransform(emitter.Handle, Translation(position));
			}
		}

		return true;
	}

	bool VfxDirector::RemoveParticleEffect(Handle handle)
	{
		const auto found = effects.find(handle);

		if (found == effects.end())
		{
			return false;
		}

		ClearEffect(found->second);
		effects.erase(found);
		return true;
	}

	void VfxDirector::SpawnParticles(const SimpleParticle& particle, const Float3& position)
	{
		if (Settings.Particles == VfxQuality::Off)
		{
			return;
		}

		const Handle handle = CreateParticleEffect(particle, position);
		Effect& effect = effects[handle];
		effect.OneShot = true;

		if (!std::isfinite(effect.ExpiresAt))
		{
			// A one-shot with continuous emission still ends: give it one lifetime.
			effect.ExpiresAt = time + std::max(particle.Lifetime, 0.1f) * 2.0f;
		}
	}

	// --- Water -----------------------------------------------------------------------------

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
			const bool bright = d.Style == ExplosionStyle::Bright;

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

				const float angle = explosion.Seed * 2.3f + float(lump) * 2.1f;
				const float offset = lump == 0 ? 0.0f : s * 0.35f;
				PyroVolumes::Volume ball;
				ball.Kind = Type::Fireball;
				ball.Position = { d.Position[0] + std::cos(angle) * offset, d.Position[1] + t * s * 0.25f + (lump == 0 ? 0.0f : s * 0.15f),
					d.Position[2] + std::sin(angle) * offset };
				ball.Radius = s * (lump == 0 ? 1.0f : 0.65f) * (0.35f + 0.85f * (1.0f - std::exp(-t * 9.0f)));
				ball.Age = std::clamp(t / duration, 0.0f, 1.0f);
				ball.Intensity = (bright ? 3.5f : 2.5f) * d.Intensity;
				ball.Density = 1.3f;
				ball.RiseSpeed = 2.5f;
				ball.Seed = explosion.Seed + float(lump) * 7.1f;
				ball.SmokeAlbedo = bright ? 0.35f : 0.2f;
				volumes.push_back(ball);
			}

			const float smoke = explosion.Detail >= 1 || (running > 1 && e + 1 < running) ? 0.0f : SmokeAmount(d);

			if (smoke > 0.0f && a > 0.15f)
			{
				// Short-lived: it swells for under a second and thins away by about 2 s.
				const float t = a - 0.15f;
				const float grow = EaseOut(t / 0.9f);
				PyroVolumes::Volume column;
				column.Kind = Type::Smoke;
				column.Position = { d.Position[0], d.Position[1] - s * 0.3f, d.Position[2] };
				column.Height = s * (0.8f + 1.6f * grow);
				column.Radius = s * (0.6f + 0.5f * grow);
				column.Age = std::clamp((a - 0.9f) / 1.2f, 0.0f, 1.0f);
				column.Density = 0.6f * smoke;
				column.RiseSpeed = 1.2f;
				column.Seed = explosion.Seed + 3.3f;
				column.SmokeAlbedo = 0.24f;
				column.SmokeGlow = { 1.0f, 0.42f, 0.12f };
				column.SmokeGlowStrength = 1.2f * std::exp(-a * 2.5f) * d.Intensity;
				volumes.push_back(column);
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
