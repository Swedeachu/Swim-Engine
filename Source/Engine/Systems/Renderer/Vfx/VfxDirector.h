#pragma once

#include "Engine/Systems/Renderer/Features/BlockFire.h"
#include "Engine/Systems/Renderer/Features/LavaSurface.h"
#include "Engine/Systems/Renderer/Features/PyroVolumes.h"
#include "Engine/Systems/Renderer/Features/WaterRipples.h"
#include "Engine/Systems/Renderer/Particles/ParticleGraphResources.h"
#include "Engine/Systems/Renderer/Resources/GpuHandle.h"
#include "Engine/Systems/Renderer/Vfx/ParticleEnhancer.h"
#include "Engine/Systems/Renderer/Vfx/VfxQuality.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace Swim::Render
{
	class GpuLightBuffer;
	class ParticleSystem;
} // namespace Swim::Render

namespace Engine
{

	class FrameRenderer;

	// Fire, lava, explosions and enhanced particles on top of PyroVolumes, LavaSurface, the
	// GPU particle system and the light buffer. The game describes what exists (fires, lava
	// quads, particle effects) and what happens (explosions); Update, once per frame before
	// rendering, turns that into volumes, emitters and lights for the camera, and applies
	// the quality tiers in Settings (Off / Classic / Enhanced per family, VfxQuality.h).
	// Handles stay valid across tier changes; Classic and Off just draw nothing here.
	// Owner thread only.
	class VfxDirector
	{

	  public:
		using Float3 = std::array<float, 3>;
		using Handle = std::uint32_t; // 0: none.

		struct Targets
		{
			Swim::Render::ParticleSystem* Particles = nullptr; // Optional: no embers, sparks, debris or enhanced particles.
			Swim::Render::GpuLightBuffer* Lights = nullptr;	   // Optional: no light emission.
			std::shared_ptr<PyroVolumes> Pyro;				   // Optional: no volumetric fire, smoke or haze.
			std::shared_ptr<LavaSurface> Lava;				   // Optional: no lava surfaces.
			std::shared_ptr<BlockFire> Fire;				   // Optional: no fire cards.
		};

		// Adds PyroVolumes, LavaSurface and BlockFire features to the renderer and wires its
		// particles and lights.
		static std::unique_ptr<VfxDirector> Install(FrameRenderer& renderer);

		explicit VfxDirector(Targets targets);
		~VfxDirector();
		VfxDirector(const VfxDirector&) = delete;
		VfxDirector& operator=(const VfxDirector&) = delete;

		VfxSettings Settings;

		// --- Fire ---------------------------------------------------------------------

		enum class FireKind : std::uint8_t
		{
			Block,	  // A fire block: flame cards in the cell, placed by Attach.
			Campfire, // Crossed flame cards with more smoke.
			Entity,	  // Cards around a burning entity (move with SetFirePosition).
			Small,	  // A candle or torch flame: small crossed cards, a hot halo and flicker.
		};

		using FireAttachment = BlockFire::Attachment;

		struct FireDesc
		{
			FireKind Kind = FireKind::Block;
			Float3 Position{ 0.0f, 0.0f, 0.0f };		   // Centre of the flames' base (Block: the bottom centre of the cell).
			FireAttachment Attach = FireAttachment::Floor; // Block: which face of the cell it burns on.
			std::uint32_t Layer = 0;					   // First flipbook layer in the fire texture array.
			std::uint32_t Frames = 1;					   // Flipbook frames (consecutive layers, or Rows inside one layer).
			std::uint32_t Rows = 1;
			float FrameRate = 16.0f;
			float Width = 1.0f;				 // m across (Small, Campfire, Entity).
			float Height = 1.0f;			 // m tall.
			Float3 Tint{ 0.3f, 0.6f, 1.0f }; // Light and emission colour when TintMix > 0 (e.g. a blue flame).
			float TintMix = 0.0f;
			float Intensity = 1.0f;
			bool Light = true;	// Flickering light (nearest fires only).
			bool Smoke = false; // More smoke (campfires always smoke).
			bool Embers = true;
		};

		Handle CreateFire(const FireDesc& desc);
		// A fire block in the cell whose min corner is `cell`, burning on `attach`, with the
		// flipbook starting at `layer`; size scales its height.
		Handle AddFire(const Float3& cell, FireAttachment attach, std::uint32_t layer, std::uint32_t frames = 1, float size = 1.0f);
		bool SetFire(Handle fire, const FireDesc& desc);
		bool SetFirePosition(Handle fire, const Float3& position);
		bool RemoveFire(Handle fire);

		// The fire flipbook's texture array (the game's block array, sRGB); null draws a
		// procedural flame. And the wind the flames sway in.
		void SetFireTexture(Swim::Rhi::TextureView* texture);
		void SetWind(const WindSettings& wind);

		// --- Ignited explosives -------------------------------------------------------

		// Smoke and fuse sparks rising from a point (the top of an ignited block), while
		// Particles is Enhanced.
		Handle AddFuseSmoke(const Float3& position);
		bool SetFuseSmokePosition(Handle fuse, const Float3& position);
		bool RemoveFuseSmoke(Handle fuse);

		// --- Lava ---------------------------------------------------------------------

		// The molten surfaces near the camera (block tops and sides), replaced wholesale.
		// They also shimmer and pop while Lava is Enhanced.
		void SetLava(std::vector<LavaSurface::Quad> quads);

		// --- Explosions ---------------------------------------------------------------

		enum class ExplosionStyle : std::uint8_t
		{
			Standard,  // Fireball, smoke, debris.
			Fiery,	   // A bigger, longer-burning fireball, more embers, less smoke.
			Bright,	   // A strong flash and a hot white-orange core, little smoke.
			Smokeless, // No smoke.
		};

		struct ExplosionDesc
		{
			Float3 Position{ 0.0f, 0.0f, 0.0f };
			float Size = 4.0f;				   // Blast radius (m): everything scales with it.
			float Intensity = 1.0f;			   // Brightness, debris and shake (0..4).
			std::optional<float> GroundHeight; // Debris and sparks bounce here (default: Position.y - Size * 0.25).
			bool Debris = true;
			bool Smoke = true;
			ExplosionStyle Style = ExplosionStyle::Standard;

			// From a blast power (the radius-like strength games use, e.g. 1 small, 4 a
			// standard charge, 6+ large): size and intensity scale with it.
			static ExplosionDesc FromPower(const Float3& position, float power, ExplosionStyle style = ExplosionStyle::Standard);
		};

		// The stages, by time: a 2-3 frame flash and light pulse, the shockwave ring and
		// its distortion, the fireball (burning out into smoke), sparks with secondary
		// sparks, debris thrown out and falling, embers, and the smoke column lingering.
		void SpawnExplosion(const ExplosionDesc& desc);

		// Camera shake: called once per explosion with a strength 0..1 (falling with
		// distance) and a duration in seconds. GetCameraShake is the decaying trauma (0..1)
		// summed over recent explosions, squared, for a game that polls instead.
		std::function<void(float strength, float duration)> OnCameraShake;

		float GetCameraShake() const { return shake * shake; }

		// --- Particles ----------------------------------------------------------------

		// A continuous or one-shot effect from a simple particle (EnhanceParticle). Classic
		// draws the plain sprite here too, so a game may route every particle through
		// this; Off draws nothing.
		Handle CreateParticleEffect(const SimpleParticle& particle, const Float3& position);
		bool SetParticleEffectPosition(Handle effect, const Float3& position);
		bool RemoveParticleEffect(Handle effect);
		// Fire and forget: removed when its particles are gone.
		void SpawnParticles(const SimpleParticle& particle, const Float3& position);

		// --- Water --------------------------------------------------------------------

		struct SplashDesc
		{
			Float3 Position{};		   // Where the body meets the surface (y: the surface height).
			Float3 Velocity{};		   // The body's velocity at impact (m/s); entering, moving through or leaving.
			float Size = 0.6f;		   // The body's width (m).
			bool Submerged = false;	   // Already in the water and moving through it: a wake, not an entry splash.
		};

		// An impact on a water surface: a burst scaled by the impact speed and the body's
		// size (crown sheet, droplets falling back with gravity, mist, a foam patch) and
		// expanding ripple rings for the water shader (GetRipples). Repeated calls while a
		// body swims are cheap; small or slow motion only adds a faint ring. Cost and detail
		// follow Water (quality, distance fade) and Settings.Water (Off / Classic / Enhanced).
		void SpawnSplash(const SplashDesc& desc);

		WaterSplashSettings Water;

		// The rings to draw this frame, nearest first, at most Water.MaxRipples, with the
		// distance fade in their amplitude: upload them for Water/WaterRipples.slang.
		const std::vector<GpuWaterRipple>& GetRipples() const { return rippleRecords; }

		// --- Frame --------------------------------------------------------------------

		// Advances explosions and lights and rebuilds the volume list for this camera.
		void Update(float deltaTime, const Float3& camera);

		struct Stats
		{
			std::uint32_t Fires = 0;
			std::uint32_t Explosions = 0;
			std::uint32_t Effects = 0;
			std::uint32_t Emitters = 0;
			std::uint32_t Lights = 0;
			std::uint32_t Volumes = 0;
			std::uint32_t DroppedEmitters = 0; // Emitters the particle system had no room for, cumulative.
			std::uint32_t Ripples = 0;
		};

		Stats GetStats() const;

		// Budgets.
		std::uint32_t MaxFireLights = 24;
		std::uint32_t MaxLavaLights = 6;
		std::uint32_t MaxExplosions = 12; // Running at once; the oldest end early past it.
		std::uint32_t MaxEmitters = 96; // Emitters this director may hold at once.
		float FireDistance = 160.0f;	// Fires farther away are not drawn.

	  private:
		struct Fire
		{
			FireDesc Desc;
			float Seed = 0.0f;
			std::optional<Swim::Render::GpuLightHandle> Light;
		};

		struct Emitter
		{
			Swim::Render::ParticleEmitterHandle Handle;
			float ExpiresAt = 0.0f; // Infinite for continuous emitters.
		};

		struct Explosion
		{
			ExplosionDesc Desc;
			float Age = 0.0f;
			float Seed = 0.0f;
			std::uint32_t Detail = 0; // 0: every layer; 1: flash, sparks and smoke; 2: flash only (spam).
			std::optional<Swim::Render::GpuLightHandle> Light;
		};

		struct Effect
		{
			SimpleParticle Particle;
			Float3 Position{};
			VfxQuality Built = VfxQuality::Off;
			std::vector<Emitter> Emitters;
			float ExpiresAt = 0.0f;
			bool OneShot = false;
			bool EnhancedOnly = false;
			std::optional<Swim::Render::GpuLightHandle> Light;
		};

		std::optional<Swim::Render::ParticleEmitterHandle> CreateEmitter(
			const Swim::Render::ParticleEmitterDesc& desc, const Float3& position, float lifetime);
		void ReleaseEmitter(Swim::Render::ParticleEmitterHandle handle);
		void ReleaseLight(std::optional<Swim::Render::GpuLightHandle>& light);
		void BuildEffect(Effect& effect);
		void ClearEffect(Effect& effect);
		void StartExplosionParticles(const Explosion& explosion);
		void UpdateLights(const Float3& camera);
		void UpdateLava(float deltaTime, const Float3& camera);
		void BuildVolumes(const Float3& camera);
		void BuildFires(const Float3& camera);
		void GatherLavaCells(const Float3& camera);
		void UpdateRipples(float deltaTime, const Float3& camera);

		struct Ripple
		{
			Float3 Centre{};
			float Age = 0.0f; // Negative: not started yet.
			float Amplitude = 0.0f;
			float Speed = 0.0f;
			float Wavelength = 0.0f;
			float Width = 0.0f;
			float Lifetime = 0.0f;
		};

		struct LavaCell
		{
			std::uint64_t Key = 0;
			Float3 Centre{}; // Mean of its faces, at the highest surface.
			float Faces = 0.0f;
			std::optional<Swim::Render::GpuLightHandle> Light;
			std::optional<Swim::Render::ParticleEmitterHandle> Embers;
		};

		struct Fuse
		{
			Handle Smoke = 0;
			Handle Sparks = 0;
		};

		Targets targets;
		std::unordered_map<Handle, Fire> fires;
		std::unordered_map<Handle, Effect> effects;
		std::vector<Explosion> explosions;
		std::vector<Emitter> transient; // Explosion and lava-pop emitters, released when spent.
		std::vector<LavaSurface::Quad> lava;
		std::vector<LavaCell> lavaCells; // Nearest first.
		std::unordered_map<Handle, Fuse> fuses;
		Handle next = 1;
		float time = 0.0f;
		float shake = 0.0f;
		float popClock = 0.0f;
		std::vector<Ripple> ripples;
		std::vector<GpuWaterRipple> rippleRecords;
		Float3 lastCamera{ 0.0f, 0.0f, 0.0f };
		std::uint32_t live = 0; // Emitters held.
		std::uint32_t dropped = 0;
		std::uint32_t rng = 0x9E3779B9u;
		VfxSettings applied;
	};

} // namespace Engine
