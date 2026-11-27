#pragma once

#include "Engine/Systems/Renderer/Runtime/RenderFeature.h"

#include <array>
#include <memory>
#include <optional>

namespace Engine
{

	// Cloud looks for VolumetricClouds::ApplyStyle: each sets the shape fields (Towering,
	// Billow, base variation and roundness, erosion, density, coverage variation, shape
	// scale) and leaves the amount (Coverage), the layer altitudes, wind and lighting alone.
	enum class CloudStyle : std::uint32_t
	{
		Cumulus = 0,	   // Fair-weather heaps with rounded tops and bases, blue sky between.
		Stratocumulus = 1, // Low, lumpy rolls that join into a broken sheet.
		Stratus = 2,	   // A thin, flat, soft-edged grey layer.
		Cumulonimbus = 3,  // Storm towers filling the whole layer, dense and dark below.
	};

	// Ray-marched volumetric clouds in a horizontal layer. Their density comes from noise
	// baked once on the GPU (a tileable 128^3 Perlin-Worley / Worley volume and a 2D weather
	// map of where clouds form, how tall they grow and how high their bases sit), shaped by
	// Coverage, a rounded height profile per cloud and billowy or wispy detail erosion. Lit by
	// the sun (a cone of shadow samples, Beer-Lambert with multiple-scattering octaves, powder,
	// two-lobe phase) and the sky's ambient colors. Marched at a reduced resolution with steps
	// that grow with distance and a per-frame dither, accumulated over frames with
	// reprojection, and composited over the sky before TAA. An optional thin cirrus sheet sits
	// far above the layer. Wind carries the whole field. The clouds are also marched into the
	// environment cube (reflections, ambient light) when Settings.Environment is on.
	// Programs: VolumetricCloudsBake, VolumetricCloudsMarch, VolumetricCloudsTemporal,
	// VolumetricCloudsComposite, VolumetricCloudsEnvironment.
	class VolumetricClouds final : public RenderFeature
	{

	  public:

		struct SettingsData
		{
			float Coverage = 0.45f;			 // 0 (clear) .. 1 (overcast): about the fraction of the sky covered.
			float CoverageVariation = 0.45f; // 0 (the same everywhere) .. 1 (clear patches and overcast patches).
			float Density = 0.024f;			 // Extinction per meter inside a full-density cloud.
			float BottomAltitude = 450.0f;	 // World Y (meters) of the layer: the lowest cloud bases.
			float TopAltitude = 1300.0f;	 // The highest tops; TopAltitude - BottomAltitude is the thickness.
			float ShapeScale = 1.0f / 3000.0f; // 1 / the size (m) the shape volume repeats over (about four cloud bodies across).
			float DetailScale = 1.0f / 500.0f; // 1 / the size (m) the eroding detail repeats over.
			float DetailErosion = 0.35f;	   // 0..1: how much the detail eats into the edges.
			float WeatherScale = 1.0f / 12000.0f; // Frequency (1/m) of the map of where clouds form and how tall they grow.
			float Towering = 0.6f;	   // 0: flat sheets (a fifth of the layer) .. 1: the tallest clouds fill the layer.
			float Billow = 0.7f;	   // 0: wispy, torn edges .. 1: round cauliflower billows.
			float BaseVariation = 0.3f; // 0: every base at BottomAltitude .. 1: bases spread over the lower part of each cloud.
			float BaseRoundness = 0.5f; // 0: flat-cut bases .. 1: deeply rounded undersides.
			float DetailWind = 1.4f;	// How much faster than the shapes the detail drifts (they evolve).
			std::array<float, 3> Wind{ 12.0f, 0.0f, 4.0f }; // Meters per second (see SetWind).
			float MaxDistance = 18000.0f;					// Farthest ray distance (m).
			float HorizonFade = 0.35f;						// Fading starts at this fraction of MaxDistance.
			float SunIntensity = 1.0f;						// Scales the sky's sun radiance.
			float AmbientStrength = 1.6f;					// Scales the sky colors used as ambient light.
			float Anisotropy = 0.55f;						// Forward-scattering g of the phase function.
			float SilverLining = 0.25f;						// Weight of the back-scattering lobe.
			float Powder = 0.5f;							// Dark-edge "powder" effect strength (0..1).
			// A thin, high sheet of streaky ice cloud above the layer (one texture lookup per sky
			// pixel; 0 turns it off).
			float CirrusCoverage = 0.0f;   // 0..1.
			float CirrusAltitude = 8000.0f; // World Y (meters).
			float CirrusScale = 1.0f / 6000.0f; // 1 / the size (m) of its streaks.
			float CirrusOpacity = 0.4f;			// Opacity of its densest streaks (0..1).
			std::uint32_t Steps = 48;			// Primary march steps through the layer's thickness.
			std::uint32_t ShadowSteps = 4;		// Sun shadow steps per sample.
			float ShadowStepLength = 70.0f;		// Meters.
			float ResolutionScale = 0.5f;		// March resolution relative to the viewport (0.25..1).
			bool Temporal = true;				// Accumulate the march over frames (much less noise).
			bool Environment = true;			// Also draw the clouds into the environment cube.
			std::uint32_t EnvironmentSteps = 64; // March steps per environment texel.
		};

		SettingsData Settings;

		// Sets the shape fields of a style (see CloudStyle).
		void ApplyStyle(CloudStyle style);

		// Wind from a speed (m/s) and the compass direction it blows toward (degrees, 0 = +Z,
		// 90 = +X), keeping the small vertical churn.
		void SetWind(float metersPerSecond, float towardDegrees);

		std::string_view GetName() const override { return "Volumetric clouds"; }

		RenderFeatureStage GetStage() const override { return RenderFeatureStage::BeforeTemporal; }

		void Record(RenderFeatureContext& context) override;

		bool ContributesToEnvironment() const override;

		std::optional<Swim::Render::GraphTexture> RecordEnvironment(RenderFeatureContext& context, std::uint32_t faceSize) override;

	  private:

		struct Params
		{
			std::array<float, 4> Forward, Right, Up, Camera, SunDirection, SunColor, Layer, Wind, AmbientTop, AmbientBottom, Lighting, Shape,
				Form, Cirrus;
			std::array<float, 16> PreviousViewProjection;
			std::uint32_t Size[4];
			std::uint32_t Output[4];
		};

		bool Visible() const;

		Params MakeParams(const RenderFeatureContext& context, float radianceScale) const;

		struct Noise
		{
			Swim::Render::GraphTexture Shape;
			Swim::Render::GraphTexture Weather;
		};

		// The baked noise imported into this frame's graph (baked on first use); empty without
		// a device.
		std::optional<Noise> ImportNoise(RenderFeatureContext& context);

		// Keep in step with VolumetricClouds.slang.
		static constexpr std::uint32_t AtlasWidth = 2080;
		static constexpr std::uint32_t AtlasHeight = 1040;
		static constexpr std::uint32_t WeatherSize = 256;

		std::unique_ptr<Swim::Rhi::Texture> shapeTexture;
		std::unique_ptr<Swim::Rhi::Texture> weatherTexture;
		bool baked = false;
		const void* bakedWith = nullptr;
		std::uint64_t noiseGraph = 0; // RenderGraph::GetId of the graph noise was imported into.
		std::uint32_t noiseFrame = ~0u;
		std::optional<Noise> noise;

		// The accumulated march (ping-pong), at the march resolution.
		std::array<std::unique_ptr<Swim::Rhi::Texture>, 2> history;
		std::uint32_t historyWidth = 0;
		std::uint32_t historyHeight = 0;
		std::uint32_t historyIndex = 0;
		std::array<bool, 2> historyWritten{ false, false };
		std::uint32_t historyFrame = ~0u; // The frame that last wrote it (a gap drops it).

		std::array<float, 3> windOffset{ 0, 0, 0 };

	};

} // namespace Engine
