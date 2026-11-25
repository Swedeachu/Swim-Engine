#pragma once

#include "Engine/Systems/Renderer/Runtime/RenderFeature.h"
#include "Engine/Systems/Water/OceanHeightField.h"
#include "Engine/Systems/Water/WaterWaves.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>

namespace Engine
{

	// One frame of an OceanSimulation on the GPU. The handles belong to the render graph of
	// the frame that produced them. Sample a cascade at uv = world xz / TileSizes[i] with a
	// repeating linear sampler and sum the cascades (Water/WaterCommon.slang has helpers).
	struct OceanMaps
	{
		static constexpr std::uint32_t MaxCascades = 3;

		// RGBA16Float: xyz displacement in metres (y up), w whitecap foam 0..1.
		std::array<Swim::Render::GraphTexture, MaxCascades> Displacement{};
		// RGBA16Float: d(height)/dx, d(height)/dz, d(dx)/dx, d(dz)/dz (normals: OceanNormal).
		std::array<Swim::Render::GraphTexture, MaxCascades> Derivatives{};
		// Last frame's displacement (motion vectors); this frame's when HasPrevious is false.
		std::array<Swim::Render::GraphTexture, MaxCascades> PreviousDisplacement{};
		std::array<float, MaxCascades> TileSizes{}; // Metres.
		// 1 for simulated cascades, 0 for unused ones (whose handles repeat cascade 0's so a
		// shader can bind every slot).
		std::array<float, MaxCascades> Weights{};
		std::uint32_t Resolution = 0;
		bool HasPrevious = false;
		bool Valid = false;
	};

	// A GPU wind-driven ocean after Tessendorf ("Simulating Ocean Water"): a JONSWAP wave
	// spectrum with directional spreading, evolved by the deep-water dispersion relation and
	// turned into height, choppy horizontal displacement, slopes and whitecap foam by inverse
	// FFTs, for up to three cascades (tile sizes) that are summed to cover swell and ripples
	// without visible repetition. Everything runs in compute passes; only the foam history
	// persists between frames.
	//
	// CPU heights (SampleSurface, MakeOceanFluidSurface): while something samples the sea, the
	// largest cascades' displacement is copied back every few frames without waiting for the
	// GPU and lands a few frames late (moved along the surface velocity to the current time).
	//
	// The simulation draws nothing. OceanSurface renders it; any feature can sample it by
	// calling Update from its own PrepareGeometry or Record (block-based water, wet decals,
	// floating objects in a shader). Programs: OceanSpectrum, OceanFft, OceanAssemble.
	class OceanSimulation
	{

	  public:

		struct SettingsData
		{
			float WindSpeed = 10.0f;			// m/s at 10 m above the sea: sets the dominant wavelength and height.
			float WindDirectionDegrees = 30.0f; // Direction the wind blows toward, from +X toward +Z.
			float Fetch = 120000.0f;			// m of open water under the wind: short = young, steep sea; long = developed.
			float Amplitude = 1.0f;				// Wave height multiplier.
			float Choppiness = 1.0f;			// Horizontal displacement: sharper crests, flatter troughs (0 = rounded).
			float Spreading = 0.3f;				// 0 = waves across a wide fan of directions, 1 = long-crested swell.
			float Detail = 1.0f;				// Scales the finer cascades (0 = only the largest waves).
			float SmallWaveCutoff = 0.02f;		// m: waves much shorter than this are damped.
			float TimeScale = 1.0f;
			float FoamThreshold = 0.35f; // Crests whose surface compresses past this break into whitecaps (higher = more foam).
			float FoamInjection = 1.5f;	 // How quickly a breaking crest turns white.
			float FoamDecay = 0.5f;		 // Per second: how fast foam fades.
			std::array<float, OceanMaps::MaxCascades> TileSizes{ 250.0f, 37.0f, 9.0f }; // m per cascade tile, largest first.
			std::uint32_t Cascades = 3;		// 1..3.
			std::uint32_t Resolution = 256; // Texels per side: 64, 128, 256 or 512.
			bool Paused = false;
			std::uint32_t ReadbackCascades = 2; // Cascades (largest first) the CPU heights include, 1..3.
			std::uint32_t ReadbackStep = 2;		// CPU grid: every Nth texel (1, 2 or 4).
			std::uint32_t ReadbackInterval = 2; // Frames between readbacks.
		};

		SettingsData Settings;

		// Records this frame's simulation into the context's graph the first time it is called
		// in a frame and returns the maps; later calls in the same frame return the same maps.
		// Call it before any pass that reads them (PrepareGeometry, or Record at the earliest
		// stage that samples them).
		const OceanMaps& Update(RenderFeatureContext& context);

		// Seconds of simulated time (wrapped to the spectrum's repeat period).
		float GetTime() const { return static_cast<float>(time); }

		// The displaced surface over world (x, z): its height (seaLevel + the summed cascades'
		// height at the rest point that the choppy offset moves onto x, z) and an approximate
		// flow velocity. From any thread, never waits for the GPU; flat at seaLevel until the
		// first readback lands. Sampling starts the readbacks, which stop about two seconds
		// (120 frames) after the last sample.
		FluidSample SampleSurface(float x, float z, float seaLevel = 0.0f) const;

		// Whether SampleSurface was called recently (the readbacks are running).
		bool IsSampled() const { return sampledRecently.load(std::memory_order_relaxed); }

		// Whether CPU heights have arrived from the GPU.
		bool HasSurfaceData() const { return field.HasData(); }

	  private:

		struct Readback
		{
			std::unique_ptr<Swim::Rhi::Buffer> Buffer;
			std::uint64_t Id = 0;
			std::uint32_t Frame = 0;
			std::uint32_t Cascades = 0;
			std::uint32_t Resolution = 0;
			std::array<float, OceanMaps::MaxCascades> TileSizes{};
			double Time = 0.0;
			bool Pending = false;
		};

		void CollectReadbacks(std::uint32_t frameNumber);

		void RequestReadback(RenderFeatureContext& context, std::uint32_t frameNumber);

		OceanHeightField field;
		std::array<Readback, 3> readbacks{};
		std::uint64_t nextReadbackId = 1;
		std::uint32_t lastReadbackFrame = 0;
		std::uint32_t lastSampledFrame = 0;
		std::uint32_t frameCount = 0;
		mutable std::atomic<bool> sampled{ false };
		std::atomic<bool> sampledRecently{ false };
		std::atomic<double> currentTime{ 0.0 };

		std::array<std::array<std::unique_ptr<Swim::Rhi::Texture>, 2>, OceanMaps::MaxCascades> history{};
		std::array<std::array<bool, 2>, OceanMaps::MaxCascades> written{};
		std::uint32_t latest = 0;
		std::uint32_t resolution = 0;
		std::uint32_t cascades = 0;
		bool historyValid = false;
		const Swim::Render::RenderGraph* graph = nullptr;
		std::uint32_t frame = ~0u;
		double time = 0.0;
		OceanMaps maps;

	};

	// A FluidVolumeDesc::Surface on the simulation's CPU heights (OceanSimulation::SampleSurface);
	// seaLevel is the drawn sea's OceanSurface::Settings.Height.
	FluidSurfaceFunction MakeOceanFluidSurface(std::shared_ptr<const OceanSimulation> simulation, float seaLevel = 0.0f);

	// Draws an OceanSimulation as an endless sea: a camera-centred grid whose quads grow with
	// distance (dense under the camera, kilometres to the edge), displaced by the cascades
	// and shaded in the Forward+ opaque pass, so it takes the frame's lights, shadows, sky
	// reflections, screen-space reflections, TAA and motion vectors. Shading: a dark water
	// body, Fresnel reflection through the standard material at low roughness (raised where
	// distance hides the finer cascades), subsurface light through backlit crests, whitecap
	// foam broken into streaks, and exponential fog toward the horizon. Program: OceanSurface.
	class OceanSurface final : public RenderFeature
	{

	  public:

		struct SettingsData
		{
			float Height = 0.0f;				// World height of the mean sea level.
			float Extent = 2500.0f;				// m from the grid centre to its edge.
			float NearSpacing = 0.35f;			// m between vertices under the camera.
			std::uint32_t GridResolution = 256; // Quads per side (16..1024).
			std::array<float, 3> DeepColor{ 0.004f, 0.016f, 0.026f }; // Albedo of the water body (linear).
			float Roughness = 0.06f;
			std::array<float, 3> ScatterColor{ 0.03f, 0.2f, 0.17f }; // Light scattered inside the water.
			float SunScatter = 4.0f;								   // Sun light through thin backlit crests.
			float AmbientScatter = 0.6f;							   // Sky light scattered back out of the body.
			float CrestScatterHeight = 0.4f;						   // Per metre of crest height above the mean.
			std::array<float, 3> AmbientColor{ 0.25f, 0.35f, 0.45f }; // Sky radiance reaching the water body.
			std::array<float, 3> FoamColor{ 0.8f, 0.84f, 0.86f };
			float Foam = 1.0f;
			std::array<float, 3> FogColor{ 0.5f, 0.6f, 0.7f }; // Radiance the sea fades into toward the horizon.
			float FogDensity = 0.0f;						   // Per metre (0 = no fog).
			// Continuous LOD: waves lower and the finer cascades flatten with distance, lost
			// detail becomes roughness, and a flat far-field ring continues past Extent.
			// Reflection steps do not apply (the frame's reflections shade the ocean).
			WaterLodSettings Lod{ .DetailStart = 0.0f, .DetailEnd = 300.0f, .AmplitudeStart = 400.0f, .AmplitudeEnd = 2000.0f,
				.FarAmplitude = 0.2f, .NormalStart = 0.0f, .NormalEnd = 1500.0f, .FarRoughness = 0.12f, .FarFieldExtent = 20000.0f };
		};

		SettingsData Settings;

		std::shared_ptr<OceanSimulation> Simulation = std::make_shared<OceanSimulation>();

		std::string_view GetName() const override { return "Ocean surface"; }

		std::uint32_t GetGeometryPasses() const override;

		RenderGeometryResources PrepareGeometry(RenderFeatureContext& context) override;

		void DrawGeometry(RenderFeatureDrawContext& context) override;

	  private:

		OceanMaps maps;
		Swim::Render::GraphBuffer params{};
		std::uint32_t quads = 0;
		std::uint32_t farVertices = 0;

	};

} // namespace Engine
