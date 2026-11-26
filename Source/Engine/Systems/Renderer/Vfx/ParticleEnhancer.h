#pragma once

#include "Engine/Systems/Renderer/Particles/ParticleSettings.h"
#include "Engine/Systems/Renderer/Vfx/VfxQuality.h"

#include <array>
#include <cstdint>
#include <vector>

namespace Engine
{

	// What a simple particle depicts, which picks the enhanced look.
	enum class ParticleHint : std::uint8_t
	{
		Generic, // Soft volumetric puffs.
		Flame,	 // Flickering fire tongues with a glow.
		Smoke,	 // Lit, billowing smoke (alpha blended).
		Spark,	 // Hot streaks with a glow.
		Magic,	 // Glowing motes with trails.
		Dust,	 // Soft, dim, alpha-blended motes.
		Liquid,	 // Small soft drops (alpha blended).
	};

	// A plain 2D particle as a game describes it: one texture or colour, a size, a velocity.
	struct SimpleParticle
	{
		ParticleHint Hint = ParticleHint::Generic;
		std::array<float, 4> Color{ 1.0f, 1.0f, 1.0f, 1.0f }; // Linear, straight alpha (the sprite's average colour or its tint).
		std::uint32_t TextureIndex = 0;						  // Bindless sprite for Classic (0: a soft disc).
		std::uint32_t SamplerIndex = 0;
		std::array<float, 4> UvRect{ 0.0f, 0.0f, 1.0f, 1.0f };
		float Size = 0.1f;								   // Billboard edge (m).
		float SizeVariance = 0.25f;						   // +- fraction.
		std::array<float, 3> Velocity{ 0.0f, 0.5f, 0.0f }; // Mean spawn velocity (m/s).
		float Spread = 0.4f;							   // Cone half angle around it (radians).
		float SpeedVariance = 0.3f;						   // +- fraction.
		std::array<float, 3> Area{ 0.0f, 0.0f, 0.0f };	   // Spawn box half extents (m).
		float Lifetime = 1.0f;
		float LifetimeVariance = 0.3f;
		std::array<float, 3> Gravity{ 0.0f, 0.0f, 0.0f };
		float Drag = 0.0f;
		float Rate = 0.0f;		 // Continuous emission per second.
		std::uint32_t Count = 0; // Burst at the start.
		float Duration = 0.0f;	 // Emission seconds (0 with Rate: forever; 0 without: the burst only).
		float Emissive = 0.0f;	 // Extra glow (0..4); flames, sparks and magic glow anyway.
		std::uint32_t Seed = 0;
	};

	// The emitter layers that draw one simple particle (all share spawn timing and seed, so
	// glow and trail layers sit on the main particles), and how long they last.
	struct EnhancedParticle
	{
		std::vector<Swim::Render::ParticleEmitterDesc> Layers;
		float Lifetime = 0.0f; // Seconds until every particle is gone; infinite while emitting forever.
		bool Glows = false;	   // Bright enough to deserve a light.
	};

	// Off: no layers. Classic: the plain sprite (texture * colour, fading out). Enhanced: the
	// source colour kept, with a procedural volumetric shape (ParticleMaterial), a glow
	// halo, velocity-stretched trails for fast or hot particles, HDR emission for bloom and
	// top-lit shading for smoke and dust.
	EnhancedParticle EnhanceParticle(const SimpleParticle& particle, VfxQuality quality);

} // namespace Engine
