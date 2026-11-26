#pragma once

#include <cstdint>

namespace Engine
{

	// A quality tier per effect family, for a video-settings menu:
	//  - Off: nothing is drawn (neither the engine's effect nor the game's original);
	//  - Classic: the game keeps drawing its own plain original; the engine adds nothing;
	//  - Enhanced: the engine draws its effect, replacing the original.
	enum class VfxQuality : std::uint8_t
	{
		Off,
		Classic,
		Enhanced,
	};

	struct VfxSettings
	{
		VfxQuality Fire = VfxQuality::Enhanced;		  // Fire blocks, campfires, burning entities.
		VfxQuality Lava = VfxQuality::Enhanced;		  // Molten surfaces, their shimmer and pops.
		VfxQuality Explosions = VfxQuality::Enhanced; // Explosion bursts.
		VfxQuality Particles = VfxQuality::Enhanced;  // Ordinary particles through the enhancer.
		VfxQuality Water = VfxQuality::Enhanced;	  // Splashes and ripple rings (SpawnSplash).
		bool Lights = true;							  // Fire flicker and explosion light pulses (Enhanced only).
		bool CameraShake = true;
	};

	// Whether the engine draws the effect (and the game should skip its own).
	constexpr bool VfxEngineDraws(VfxQuality quality)
	{
		return quality == VfxQuality::Enhanced;
	}

	// Whether the game should draw its own original effect.
	constexpr bool VfxGameDrawsOriginal(VfxQuality quality)
	{
		return quality == VfxQuality::Classic;
	}

} // namespace Engine
