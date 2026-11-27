#include "Engine/Systems/Renderer/Vfx/VfxDirector.h"
#include "Engine/Systems/Renderer/Vfx/VfxDirectorHelpers.h"

#include "Engine/Systems/Renderer/Lights/GpuLightBuffer.h"
#include "Engine/Systems/Renderer/Particles/ParticleSystem.h"
#include "Engine/Systems/Renderer/Runtime/FrameRenderer.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace Engine
{

	using namespace VfxDetail;

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

} // namespace Engine
