#include "Engine/Systems/Renderer/Features/LensFlare.h"
#include "Engine/Systems/Renderer/Features/SunShafts.h"
#include "Engine/Systems/Renderer/Features/VolumetricClouds.h"
#include "Engine/Systems/Renderer/Runtime/RenderSettings.h"
#include "Engine/Systems/Renderer/Runtime/ShaderLibrary.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

namespace Engine
{

	namespace
	{

		using Float4 = std::array<float, 4>;

		Float4 Vec(const std::array<float, 3>& v, float w)
		{
			return { v[0], v[1], v[2], w };
		}

		// How far the sun is inside the screen: 1 inside, falling to 0 at `margin` outside,
		// 0 behind the camera.
		float ScreenFade(const std::array<float, 3>& projected, float margin)
		{
			if (projected[2] <= 0.0f)
			{
				return 0.0f;
			}

			const float outside = std::max({ -projected[0], projected[0] - 1.0f, -projected[1], projected[1] - 1.0f, 0.0f });
			return std::clamp(1.0f - outside / std::max(margin, 1.0e-3f), 0.0f, 1.0f);
		}

		std::uint32_t Scaled(std::uint32_t size, float scale)
		{
			return std::max(1u, static_cast<std::uint32_t>(std::lround(float(size) * std::clamp(scale, 0.25f, 1.0f))));
		}

	} // namespace

	void SunShafts::Record(RenderFeatureContext& context)
	{
		const auto& view = context.View();
		const auto sun = view.ProjectDirection(view.SunDirection);
		const float fade = ScreenFade(sun, Settings.EdgeFade) * std::clamp(view.SunDirection[1] * 8.0f + 0.2f, 0.0f, 1.0f);

		if (fade <= 0.0f || Settings.Intensity <= 0.0f)
		{
			return;
		}

		struct Constants
		{
			Float4 Forward, Right, Up, SunDirection, SunColor, SunUv, Params;
			std::uint32_t Size[4];
		} constants{};

		static_assert(sizeof(Constants) == 128);
		const std::uint32_t maskWidth = Scaled(view.Width, Settings.MaskScale);
		const std::uint32_t maskHeight = Scaled(view.Height, Settings.MaskScale);
		constants.Forward = Vec(view.Forward, view.TanHalfFovX);
		constants.Right = Vec(view.Right, view.TanHalfFovY);
		constants.Up = Vec(view.Up, std::cos(Settings.ConeDegrees * 3.14159265f / 180.0f));
		constants.SunDirection = Vec(view.SunDirection, fade);
		constants.SunColor = Vec(view.SunColor, Settings.Intensity);
		constants.SunUv = { sun[0], sun[1], std::clamp(Settings.Density, 0.0f, 2.0f), std::clamp(Settings.Decay, 0.0f, 1.0f) };
		constants.Params = { Settings.Weight, float(std::clamp(Settings.Samples, 1u, 128u)), Settings.Threshold, 0.0f };
		constants.Size[0] = view.Width;
		constants.Size[1] = view.Height;
		constants.Size[2] = maskWidth;
		constants.Size[3] = maskHeight;

		const auto mask = context.CreateTexture(Swim::Rhi::Format::RGBA16Float, maskWidth, maskHeight, "Sun shaft mask");
		context.Compute("SunShaftsMask")
			.Texture("Color", context.Color())
			.Texture("Depth", context.Depth())
			.Storage("Mask", mask)
			.Constants(constants)
			.Dispatch(maskWidth, maskHeight);
		const auto output = context.CreateColorTarget("Sun shafts");
		context.Compute("SunShaftsComposite")
			.Texture("Color", context.Color())
			.Texture("Mask", mask)
			.Sampler("LinearClamp")
			.Storage("Output", output)
			.Constants(constants)
			.Dispatch(view.Width, view.Height);
		context.SetColor(output);
	}

	void LensFlare::Record(RenderFeatureContext& context)
	{
		const auto& view = context.View();
		const auto sun = view.ProjectDirection(view.SunDirection);
		const float fade = ScreenFade(sun, Settings.EdgeFade) * std::clamp(view.SunDirection[1] * 8.0f + 0.2f, 0.0f, 1.0f);

		if (fade <= 0.0f || Settings.Intensity <= 0.0f)
		{
			return;
		}

		struct Constants
		{
			Float4 SunUv, SunColor, Ghosts, Glare, Params;
			std::uint32_t Size[4];
		} constants{};

		constants.SunUv = { sun[0], sun[1], float(view.Width) / float(std::max(view.Height, 1u)), fade };
		constants.SunColor = Vec(view.SunColor, Settings.Intensity);
		constants.Ghosts = { float(std::min(Settings.Ghosts, 8u)), Settings.GhostSpacing, Settings.GhostIntensity, Settings.HaloRadius };
		constants.Glare = { Settings.GlareSize, Settings.Streaks, Settings.StarburstIntensity, Settings.HaloIntensity };
		constants.Params = { Settings.Threshold, Settings.OcclusionRadius, Settings.ChromaticSpread, view.Time };
		constants.Size[0] = view.Width;
		constants.Size[1] = view.Height;
		const auto output = context.CreateColorTarget("Lens flare");
		context.Compute("LensFlare")
			.Texture("Color", context.Color())
			.Texture("Depth", context.Depth())
			.Storage("Output", output)
			.Constants(constants)
			.Dispatch(view.Width, view.Height);
		context.SetColor(output);
	}

	bool VolumetricClouds::Visible() const
	{
		return (Settings.Coverage > 0.0f && Settings.Density > 0.0f && Settings.TopAltitude > Settings.BottomAltitude) ||
			Settings.CirrusCoverage > 0.0f;
	}

	void VolumetricClouds::ApplyStyle(CloudStyle style)
	{
		auto& s = Settings;

		switch (style)
		{
		case CloudStyle::Cumulus:
			s.Towering = 0.6f;
			s.Billow = 0.8f;
			s.BaseVariation = 0.3f;
			s.BaseRoundness = 0.55f;
			s.DetailErosion = 0.35f;
			s.CoverageVariation = 0.45f;
			s.Density = 0.024f;
			s.ShapeScale = 1.0f / 3000.0f;
			break;
		case CloudStyle::Stratocumulus:
			s.Towering = 0.3f;
			s.Billow = 0.6f;
			s.BaseVariation = 0.2f;
			s.BaseRoundness = 0.4f;
			s.DetailErosion = 0.3f;
			s.CoverageVariation = 0.3f;
			s.Density = 0.03f;
			s.ShapeScale = 1.0f / 2200.0f;
			break;
		case CloudStyle::Stratus:
			s.Towering = 0.05f;
			s.Billow = 0.15f;
			s.BaseVariation = 0.1f;
			s.BaseRoundness = 0.2f;
			s.DetailErosion = 0.2f;
			s.CoverageVariation = 0.2f;
			s.Density = 0.02f;
			s.ShapeScale = 1.0f / 6000.0f;
			break;
		case CloudStyle::Cumulonimbus:
			s.Towering = 1.0f;
			s.Billow = 0.9f;
			s.BaseVariation = 0.15f;
			s.BaseRoundness = 0.35f;
			s.DetailErosion = 0.3f;
			s.CoverageVariation = 0.5f;
			s.Density = 0.05f;
			s.ShapeScale = 1.0f / 4500.0f;
			break;
		}
	}

	void VolumetricClouds::SetWind(float metersPerSecond, float towardDegrees)
	{
		const float radians = towardDegrees * 3.14159265f / 180.0f;
		Settings.Wind = { std::sin(radians) * metersPerSecond, Settings.Wind[1], std::cos(radians) * metersPerSecond };
	}

	VolumetricClouds::Params VolumetricClouds::MakeParams(const RenderFeatureContext& context, float radianceScale) const
	{
		const auto& view = context.View();
		const auto& settings = context.Settings();
		const float skyScale = settings.Sky.Intensity * settings.EnvironmentIntensity * radianceScale;
		const float sunScale = Settings.SunIntensity * radianceScale;
		Params params{};
		params.Forward = Vec(view.Forward, view.TanHalfFovX);
		params.Right = Vec(view.Right, view.TanHalfFovY);
		params.Up = Vec(view.Up, view.Time);
		params.Camera = Vec(view.Position, std::clamp(Settings.Coverage, 0.0f, 1.0f));
		params.SunDirection = Vec(view.SunDirection, Settings.Density);
		params.SunColor = { view.SunColor[0] * sunScale, view.SunColor[1] * sunScale, view.SunColor[2] * sunScale, Settings.AmbientStrength };
		params.Layer = { Settings.BottomAltitude, Settings.TopAltitude, Settings.ShapeScale, Settings.DetailScale };
		params.Wind = Vec(windOffset, Settings.MaxDistance);
		params.AmbientTop = { settings.Sky.ZenithColor[0] * skyScale, settings.Sky.ZenithColor[1] * skyScale,
			settings.Sky.ZenithColor[2] * skyScale, std::clamp(Settings.Anisotropy, -0.95f, 0.95f) };
		params.AmbientBottom = { settings.Sky.HorizonColor[0] * skyScale, settings.Sky.HorizonColor[1] * skyScale,
			settings.Sky.HorizonColor[2] * skyScale, std::clamp(Settings.Powder, 0.0f, 1.0f) };
		params.Lighting = { Settings.ShadowStepLength, std::clamp(Settings.SilverLining, 0.0f, 1.0f), Settings.DetailErosion,
			std::clamp(Settings.HorizonFade, 0.0f, 1.0f) };
		params.Shape = { Settings.WeatherScale, std::clamp(Settings.Towering, 0.0f, 1.0f), Settings.DetailWind,
			std::clamp(Settings.CoverageVariation, 0.0f, 1.0f) };
		params.Form = { std::clamp(Settings.Billow, 0.0f, 1.0f), std::clamp(Settings.BaseVariation, 0.0f, 1.0f),
			std::clamp(Settings.BaseRoundness, 0.0f, 1.0f), std::atan2(Settings.Wind[0], Settings.Wind[2]) };
		params.Cirrus = { std::clamp(Settings.CirrusCoverage, 0.0f, 1.0f), Settings.CirrusAltitude, Settings.CirrusScale,
			std::clamp(Settings.CirrusOpacity, 0.0f, 1.0f) };
		params.PreviousViewProjection = view.PreviousViewProjection;
		params.Output[2] = std::clamp(Settings.ShadowSteps, 1u, 16u);
		return params;
	}

	std::optional<VolumetricClouds::Noise> VolumetricClouds::ImportNoise(RenderFeatureContext& context)
	{
		auto& graph = context.Graph();

		if (noiseGraph == graph.GetId() && noiseFrame == context.View().Frame)
		{
			return noise;
		}

		noiseGraph = graph.GetId();
		noiseFrame = context.View().Frame;
		noise.reset();
		auto* device = context.Device();

		if (!device)
		{
			return noise;
		}

		if (!shapeTexture)
		{
			Swim::Rhi::TextureDesc desc;
			desc.PixelFormat = Swim::Rhi::Format::RGBA16Float;
			desc.Usage = Swim::Rhi::TextureUsage::Sampled | Swim::Rhi::TextureUsage::Storage;
			desc.Extent = { AtlasWidth, AtlasHeight, 1 };
			desc.DebugName = "Cloud shape noise";
			shapeTexture = device->CreateTexture(desc);
			desc.Extent = { WeatherSize, WeatherSize, 1 };
			desc.DebugName = "Cloud weather map";
			weatherTexture = device->CreateTexture(desc);
			baked = false;
		}

		if (!shapeTexture || !weatherTexture)
		{
			return noise;
		}

		// A reloaded bake program (shader hot reload) bakes the noise again.
		const auto& load = context.GetServices().LoadCompute;
		const void* bakeProgram = load ? load("VolumetricCloudsBake").Pipeline.get() : nullptr;
		baked = baked && bakeProgram == bakedWith;
		const auto state = baked ? Swim::Rhi::ResourceState::ShaderRead : Swim::Rhi::ResourceState::Undefined;
		Noise imported{ graph.ImportTexture(*shapeTexture, state), graph.ImportTexture(*weatherTexture, state) };

		if (!baked)
		{
			context.Compute("VolumetricCloudsBake")
				.Storage("ShapeAtlas", imported.Shape)
				.Storage("WeatherMap", imported.Weather)
				.Dispatch(AtlasWidth, AtlasHeight + WeatherSize);
			baked = true;
			bakedWith = bakeProgram;
			graph.OnAbandoned(
				[this]
				{
					baked = false;
				});
		}

		graph.Export(imported.Shape, Swim::Rhi::ResourceState::ShaderRead);
		graph.Export(imported.Weather, Swim::Rhi::ResourceState::ShaderRead);
		noise = imported;
		return noise;
	}

	void VolumetricClouds::Record(RenderFeatureContext& context)
	{
		const auto& view = context.View();

		for (int c = 0; c < 3; ++c)
		{
			windOffset[c] = std::fmod(windOffset[c] + Settings.Wind[c] * view.DeltaTime, 1.0e6f);
		}

		const auto textures = Visible() ? ImportNoise(context) : std::nullopt;

		if (!textures)
		{
			return;
		}

		const std::uint32_t width = Scaled(view.Width, Settings.ResolutionScale);
		const std::uint32_t height = Scaled(view.Height, Settings.ResolutionScale);
		auto params = MakeParams(context, 1.0f);
		params.Size[0] = width;
		params.Size[1] = height;
		params.Size[2] = std::clamp(Settings.Steps, 4u, 256u);
		params.Size[3] = view.Temporal ? view.Frame : 0u;
		params.Output[0] = view.Width;
		params.Output[1] = view.Height;

		auto& graph = context.Graph();
		auto* device = context.Device();
		const bool temporal = Settings.Temporal && view.Temporal && device;

		if (temporal && (!history[0] || historyWidth != width || historyHeight != height))
		{
			Swim::Rhi::TextureDesc desc;
			desc.PixelFormat = Swim::Rhi::Format::RGBA16Float;
			desc.Usage = Swim::Rhi::TextureUsage::Sampled | Swim::Rhi::TextureUsage::Storage;
			desc.Extent = { width, height, 1 };
			desc.DebugName = "Cloud history";
			history[0] = device->CreateTexture(desc);
			history[1] = device->CreateTexture(desc);
			historyWidth = width;
			historyHeight = height;
			historyFrame = ~0u;
			historyWritten = { false, false };
		}

		const bool accumulate = temporal && history[0] && history[1];
		const bool continued = accumulate && !view.Cut && historyFrame != ~0u && historyFrame + 1u == view.Frame;
		params.Output[3] = continued ? 1u : 0u;
		const auto buffer = graph.CreateUpload(std::as_bytes(std::span(&params, 1)), "Cloud params", Swim::Rhi::BufferUsage::Storage, 16);
		const auto clouds = context.CreateTexture(Swim::Rhi::Format::RGBA16Float, width, height, "Clouds");
		context.Compute("VolumetricCloudsMarch")
			.Buffer("Params", buffer)
			.Storage("Clouds", clouds)
			.Texture("ShapeAtlas", textures->Shape)
			.Texture("WeatherMap", textures->Weather)
			.Sampler("LinearClamp")
			.Sampler("LinearRepeat", "LinearRepeat")
			.Texture("Depth", context.Depth())
			.Dispatch(width, height);
		auto resolved = clouds;

		if (accumulate)
		{
			const auto state = [](bool written) { return written ? Swim::Rhi::ResourceState::ShaderRead : Swim::Rhi::ResourceState::Undefined; };
			const auto next = graph.ImportTexture(*history[historyIndex ^ 1u], state(historyWritten[historyIndex ^ 1u]));
			// Without a history the pass only copies; any other texture stands in for it.
			const auto previous = continued ? graph.ImportTexture(*history[historyIndex], state(true)) : textures->Weather;
			context.Compute("VolumetricCloudsTemporal")
				.Buffer("Params", buffer)
				.Texture("Clouds", clouds)
				.Texture("History", previous)
				.Sampler("LinearClamp")
				.Storage("Accumulated", next)
				.Dispatch(width, height);

			if (continued)
			{
				graph.Export(previous, Swim::Rhi::ResourceState::ShaderRead);
			}

			graph.Export(next, Swim::Rhi::ResourceState::ShaderRead);
			graph.OnAbandoned(
				[this, w = historyWritten, i = historyIndex, f = historyFrame]
				{
					historyWritten = w;
					historyIndex = i;
					historyFrame = f;
				});
			historyWritten[historyIndex ^ 1u] = true;
			historyIndex ^= 1u;
			historyFrame = view.Frame;
			resolved = next;
		}

		const auto output = context.CreateColorTarget("Clouds composite");
		context.Compute("VolumetricCloudsComposite")
			.Buffer("Params", buffer)
			.Texture("Color", context.Color())
			.Texture("Depth", context.Depth())
			.Texture("Clouds", resolved)
			.Sampler("LinearClamp")
			.Storage("Output", output)
			.Dispatch(view.Width, view.Height);
		context.SetColor(output);
	}

	bool VolumetricClouds::ContributesToEnvironment() const
	{
		return Settings.Environment && Visible();
	}

	std::optional<Swim::Render::GraphTexture> VolumetricClouds::RecordEnvironment(RenderFeatureContext& context, std::uint32_t faceSize)
	{
		const auto textures = ContributesToEnvironment() && faceSize != 0 ? ImportNoise(context) : std::nullopt;

		if (!textures)
		{
			return std::nullopt;
		}

		// The cube is in sky radiance units and lighting multiplies it by
		// EnvironmentIntensity, which the clouds' own lighting already includes.
		const float intensity = context.Settings().EnvironmentIntensity;
		auto params = MakeParams(context, intensity > 1.0e-4f ? 1.0f / intensity : 0.0f);
		params.Forward[3] = context.Settings().EnvironmentRotation;
		params.Size[0] = faceSize;
		params.Size[1] = faceSize * 6u;
		params.Size[2] = std::clamp(std::max(Settings.EnvironmentSteps, Settings.Steps), 4u, 256u);
		params.Size[3] = 0;

		auto& graph = context.Graph();
		const auto buffer =
			graph.CreateUpload(std::as_bytes(std::span(&params, 1)), "Environment cloud params", Swim::Rhi::BufferUsage::Storage, 16);
		const auto clouds = context.CreateTexture(Swim::Rhi::Format::RGBA16Float, faceSize, faceSize * 6u, "Environment clouds");
		context.Compute("VolumetricCloudsEnvironment")
			.Buffer("Params", buffer)
			.Storage("Clouds", clouds)
			.Texture("ShapeAtlas", textures->Shape)
			.Texture("WeatherMap", textures->Weather)
			.Sampler("LinearClamp")
			.Sampler("LinearRepeat", "LinearRepeat")
			.Dispatch(faceSize, faceSize * 6u);
		return clouds;
	}

} // namespace Engine
