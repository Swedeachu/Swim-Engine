#pragma once

#include "Engine/Systems/Renderer/Runtime/RenderFeature.h"

#include <array>
#include <cstdint>
#include <vector>

namespace Engine
{

	// Lightning strikes: branching bolts generated on the CPU when struck (midpoint
	// displacement of the main channel, recursive side branches) and drawn in the Forward+
	// transparent pass as additive camera-facing ribbons, hot enough for bloom to carry the
	// glow. Each strike flickers through its return strokes and fades; GetFlash() reports the
	// combined brightness so the game can light the sky, a flash light and cue thunder.
	// Program: Lightning.
	class Lightning final : public RenderFeature
	{

	  public:

		struct StrikeDesc
		{
			std::array<float, 3> Start{ 0.0f, 400.0f, 0.0f }; // Usually in the clouds.
			std::array<float, 3> End{ 0.0f, 0.0f, 0.0f };	  // Ground or sea.
			float Width = 0.5f;								  // m: core width of the main channel.
			float Jaggedness = 0.16f;						  // Sideways offset per subdivision, as a share of the segment.
			float Branching = 0.5f;							  // 0..1: how many side branches grow.
			float Duration = 0.9f;							  // s until the bolt has faded.
			std::uint32_t ReturnStrokes = 3;				  // Flashes along the same channel (1..6).
			float Intensity = 1.0f;
			std::uint32_t Seed = 1;
		};

		struct SettingsData
		{
			std::array<float, 3> Color{ 0.78f, 0.84f, 1.0f };
			float Intensity = 600.0f;	 // Core radiance.
			float GlowWidth = 7.0f;		 // Glow ribbon width relative to the core.
			float GlowIntensity = 0.04f; // Glow radiance relative to the core.
			float CoreSharpness = 2.5f;
		};

		SettingsData Settings;

		// Starts a strike; it is drawn from the next frame on.
		void Strike(const StrikeDesc& desc);

		// The combined brightness envelope (about 0..1 per strike at its peak) of the last
		// rendered frame.
		float GetFlash() const { return flash; }

		std::size_t GetActiveStrikes() const { return bolts.size(); }

		std::string_view GetName() const override { return "Lightning"; }

		std::uint32_t GetGeometryPasses() const override;

		RenderGeometryResources PrepareGeometry(RenderFeatureContext& context) override;

		void DrawGeometry(RenderFeatureDrawContext& context) override;

	  private:

		struct Segment
		{
			std::array<float, 3> A;
			std::array<float, 3> B;
			float Width;
			std::uint32_t Level; // 0 = main channel, then branch depth.
		};

		struct Bolt
		{
			StrikeDesc Desc;
			std::vector<Segment> Segments;
			std::vector<float> Strokes; // Seconds after the strike.
			float Age = 0.0f;
		};

		static float Envelope(const Bolt& bolt, std::uint32_t level);

		std::vector<Bolt> bolts;
		std::vector<std::array<float, 4>> upload;
		Swim::Render::GraphBuffer segments{};
		std::uint32_t segmentCount = 0;
		float flash = 0.0f;

	};

} // namespace Engine
