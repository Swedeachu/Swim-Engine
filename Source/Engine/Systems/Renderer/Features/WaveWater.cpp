#include "Engine/Systems/Renderer/Features/WaveWater.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <span>

namespace Engine
{

	namespace
	{

		using Float4 = std::array<float, 4>;

		struct WaveGpu
		{
			Float4 DirectionNumber, AmplitudePhase;
		};

		struct LodGpu
		{
			Float4 Fade0, Fade1, Fade2;
		};

		struct Params
		{
			Float4 Grid, GridShape, Bounds, Edge;
			LodGpu Lod;
			Float4 Absorption, Scattering, Viewport;
			WaveGpu Waves[WaterWaveSet::MaxWaves];
		};

		static_assert(sizeof(Params) == 16 * (4 + 3 + 3 + 2 * WaterWaveSet::MaxWaves));

		float InverseRange(float start, float end)
		{
			return end > start ? 1.0f / (end - start) : 1.0e9f;
		}

		bool SameWaves(const WaterWaveSettings& a, const WaterWaveSettings& b)
		{
			return std::memcmp(&a, &b, sizeof(a)) == 0;
		}

	} // namespace

	std::uint32_t WaveWater::GetGeometryPasses() const
	{
		return RenderGeometryPassBit(RenderGeometryPass::Transparent);
	}

	std::shared_ptr<const WaterWaveSet> WaveWater::GetWaves()
	{
		if (!waves || !hasBuilt || !SameWaves(built, Settings.Waves))
		{
			waves = std::make_shared<const WaterWaveSet>(BuildWaterWaves(Settings.Waves));
			built = Settings.Waves;
			hasBuilt = true;
		}

		return waves;
	}

	RenderGeometryResources WaveWater::PrepareGeometry(RenderFeatureContext& context)
	{
		vertices = 0;
		ValidateWaterLodSettings(Settings.Lod);
		const auto set = GetWaves();
		const auto& view = context.View();
		const auto& lod = Settings.Lod;
		const std::uint32_t quads = std::clamp(Settings.GridResolution, 16u, 1024u);
		const float extent = std::max(Settings.Extent, 1.0f);
		const float spacing = std::max(Settings.NearSpacing, 0.01f);
		// offset(u) = scale * (exp(rate * |u|) - 1): spacing grows in proportion to distance,
		// from NearSpacing under the camera to Extent at the edge (as OceanSurface).
		const float scale = spacing * float(quads) * 0.5f;
		float low = 1.0e-3f;
		float high = 30.0f;

		for (int i = 0; i < 40; ++i)
		{
			const float rate = 0.5f * (low + high);
			(scale * std::expm1(rate) / rate < extent ? low : high) = rate;
		}

		const float rate = 0.5f * (low + high);
		const double time = Clock ? Clock() : double(view.Time);
		const bool farField = lod.Enabled && lod.FarField && lod.FarFieldExtent > extent;

		Params p{};
		p.Grid = { view.Position[0], view.Position[2], set->Height, rate };
		p.GridShape = { float(quads), scale / rate, farField ? lod.FarFieldExtent : 0.0f, static_cast<float>(std::fmod(time, 4096.0)) };
		p.Bounds = { Settings.BoundsMin[0], Settings.BoundsMin[1], Settings.BoundsMax[0], Settings.BoundsMax[1] };
		p.Edge = { std::max(Settings.EdgeFade, 0.0f), extent, float(set->Count),
			2.0f * view.TanHalfFovY / float(std::max(view.Height, 1u)) };
		p.Lod.Fade0 = { lod.DetailStart, InverseRange(lod.DetailStart, lod.DetailEnd), lod.AmplitudeStart,
			InverseRange(lod.AmplitudeStart, lod.AmplitudeEnd) };
		p.Lod.Fade1 = { lod.FarAmplitude, lod.NormalStart, InverseRange(lod.NormalStart, lod.NormalEnd), lod.FarRoughness };
		p.Lod.Fade2 = { lod.ReflectionStart, InverseRange(lod.ReflectionStart, lod.ReflectionEnd), lod.Enabled ? 1.0f : 0.0f,
			lod.Debug ? 1.0f : 0.0f };
		p.Absorption = { Settings.Absorption[0], Settings.Absorption[1], Settings.Absorption[2], Settings.Roughness };
		p.Scattering = { Settings.Scattering[0], Settings.Scattering[1], Settings.Scattering[2], Settings.Refraction };
		p.Viewport = { float(view.Width), float(view.Height), std::max(Settings.Ripples, 0.0f), float(std::min(Settings.ReflectionSteps, 64u)) };

		for (std::uint32_t i = 0; i < set->Count; ++i)
		{
			const auto& wave = set->Waves[i];
			// The phase at this time, reduced in double precision (the shader adds k * d.x).
			const double phase = std::fmod(double(wave.Phase) - double(wave.Frequency) * time, 2.0 * 3.14159265358979);
			p.Waves[i].DirectionNumber = { wave.Direction.x, wave.Direction.y, wave.Number, wave.Horizontal };
			p.Waves[i].AmplitudePhase = { wave.Amplitude, static_cast<float>(phase), wave.Detail, 0.0f };
		}

		params = context.Graph().CreateUpload(std::as_bytes(std::span(&p, 1)), "Wave water params", Swim::Rhi::BufferUsage::Storage, 16);
		vertices = quads * quads * 6 + (farField ? 24u : 0u);
		RenderGeometryResources resources;
		resources.Buffers = { params };
		resources.SceneCopy = true;
		return resources;
	}

	void WaveWater::DrawGeometry(RenderFeatureDrawContext& context)
	{
		const auto color = context.SceneColor();
		const auto depth = context.SceneDepth();

		if (vertices == 0 || context.Pass() != RenderGeometryPass::Transparent || !color || !depth)
		{
			return;
		}

		context.Program("WaveWater")
			.Buffer("Params", params)
			.Texture("SceneColor", *color)
			.Texture("SceneDepth", *depth)
			.Sampler("SceneSampler", "LinearClamp")
			.Draw(vertices);
	}

} // namespace Engine
