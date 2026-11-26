#pragma once

#include <array>
#include <cmath>
#include <cstdint>

namespace Engine
{

	// How glinting surfaces shimmer.
	enum class GlintMode : std::uint8_t
	{
		Off,
		Procedural, // Sweeping sheen bands, rim sheen and sparkles (GlintEmission).
		Texture,	// A given glint texture scrolled in two layers at different angles, added (the classic look).
	};

	// A sheen for enchanted-looking item and armour surfaces. Procedural: two sets of soft
	// bands sweeping across the surface at different angles and speeds, shifting between two
	// colours, a view-dependent rim sheen, and twinkling sparkles. Texture: two scrolling
	// layers of a glint texture. Evaluated in the surface's own shader (Features/Glint.slang:
	// GlintEvaluate / GlintEmission) and added to its emission, so it follows the surface's
	// UVs and shape. The renderer applies it to materials with MaterialDesc::Glint and to UI
	// images with UiImage::Glint (RenderSettings::Glint); custom shaders pack it themselves.
	struct GlintSettings
	{
		GlintMode Mode = GlintMode::Procedural;
		std::array<float, 3> ColorA{ 0.55f, 0.3f, 1.0f }; // Linear.
		std::array<float, 3> ColorB{ 0.3f, 0.6f, 1.0f };
		float Intensity = 0.3f;	   // Emission scale (both modes).
		float Sparkle = 0.5f;	   // Sparkle brightness (0: none).
		float Speed = 0.35f;	   // Band sweeps per second.
		float BandScale = 1.6f;	   // Bands per UV unit.
		float BandWidth = 0.12f;   // Band softness (fraction of the spacing).
		float Rim = 0.35f;		   // View-dependent edge sheen.
		float SparkleDensity = 18; // Sparkle cells per UV unit.
		float TextureScale = 0.5f; // Texture: glint texture repeats per UV unit.
		float TextureSpeed = 1.0f; // Texture: scroll rate multiplier.
	};

	// GlintParams in Glint.slang (std430, 80 bytes).
	struct GpuGlint
	{
		std::array<float, 4> ColorA{};		   // rgb; w: intensity.
		std::array<float, 4> ColorB{};		   // rgb; w: sparkle.
		std::array<float, 4> Motion{};		   // x: speed; y: band scale; z: band width; w: time (s).
		std::array<float, 4> Extra{};		   // x: rim; y: sparkle density; z: texture scale; w: texture speed.
		std::array<std::uint32_t, 4> Source{}; // x: mode (0 off, 1 procedural, 2 texture); y: bindless texture; z: bindless sampler.
	};

	static_assert(sizeof(GpuGlint) == 80);

	// texture/sampler: bindless indices of the glint texture and a repeating sampler. Texture
	// mode without a texture (0) falls back to Procedural.
	inline GpuGlint PackGlint(const GlintSettings& g, float time, std::uint32_t texture = 0, std::uint32_t sampler = 0)
	{
		GpuGlint out;

		for (int c = 0; c < 3; ++c)
		{
			out.ColorA[c] = g.ColorA[c];
			out.ColorB[c] = g.ColorB[c];
		}

		out.ColorA[3] = g.Intensity;
		out.ColorB[3] = g.Sparkle;
		out.Motion = { g.Speed, g.BandScale, g.BandWidth, std::isfinite(time) ? std::fmod(time, 3600.0f) : 0.0f };
		out.Extra = { g.Rim, g.SparkleDensity, g.TextureScale, g.TextureSpeed };
		const bool textured = g.Mode == GlintMode::Texture && texture != 0;
		const std::uint32_t mode = g.Mode == GlintMode::Off ? 0u : textured ? 2u : 1u;
		out.Source = { mode, textured ? texture : 0u, textured ? sampler : 0u, 0u };
		return out;
	}

} // namespace Engine
