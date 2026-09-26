#include "Engine/Systems/Renderer/Features/LensFlare.h"
#include "Engine/Systems/Renderer/Features/SunShafts.h"
#include "Engine/Systems/Renderer/Features/VolumetricClouds.h"
#include "Engine/Systems/Renderer/Runtime/RenderSettings.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

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

	void VolumetricClouds::Record(RenderFeatureContext& context)
	{
		const auto& view = context.View();
		for (int c = 0; c < 3; ++c)
		{
			windOffset[c] = std::fmod(windOffset[c] + Settings.Wind[c] * view.DeltaTime, 1.0e6f);
		}
		if (Settings.Coverage <= 0.0f || Settings.Density <= 0.0f || !(Settings.TopAltitude > Settings.BottomAltitude))
		{
			return;
		}
		const auto& settings = context.Settings();
		const float skyScale = settings.Sky.Intensity * settings.EnvironmentIntensity;

		struct Params
		{
			Float4 Forward, Right, Up, Camera, SunDirection, SunColor, Layer, Wind, AmbientTop, AmbientBottom, Lighting;
			std::uint32_t Size[4];
			std::uint32_t Output[4];
		} params{};

		const std::uint32_t width = Scaled(view.Width, Settings.ResolutionScale);
		const std::uint32_t height = Scaled(view.Height, Settings.ResolutionScale);
		params.Forward = Vec(view.Forward, view.TanHalfFovX);
		params.Right = Vec(view.Right, view.TanHalfFovY);
		params.Up = Vec(view.Up, view.Time);
		params.Camera = Vec(view.Position, std::clamp(Settings.Coverage, 0.0f, 1.0f));
		params.SunDirection = Vec(view.SunDirection, Settings.Density);
		params.SunColor = { view.SunColor[0] * Settings.SunIntensity, view.SunColor[1] * Settings.SunIntensity,
			view.SunColor[2] * Settings.SunIntensity, Settings.AmbientStrength };
		params.Layer = { Settings.BottomAltitude, Settings.TopAltitude, Settings.ShapeScale, Settings.DetailScale };
		params.Wind = Vec(windOffset, Settings.MaxDistance);
		params.AmbientTop = { settings.Sky.ZenithColor[0] * skyScale, settings.Sky.ZenithColor[1] * skyScale,
			settings.Sky.ZenithColor[2] * skyScale, std::clamp(Settings.Anisotropy, -0.95f, 0.95f) };
		params.AmbientBottom = { settings.Sky.HorizonColor[0] * skyScale, settings.Sky.HorizonColor[1] * skyScale,
			settings.Sky.HorizonColor[2] * skyScale, std::clamp(Settings.Powder, 0.0f, 1.0f) };
		params.Lighting = { Settings.ShadowStepLength, std::clamp(Settings.SilverLining, 0.0f, 1.0f), Settings.DetailErosion,
			std::clamp(Settings.HorizonFade, 0.0f, 1.0f) };
		params.Size[0] = width;
		params.Size[1] = height;
		params.Size[2] = std::clamp(Settings.Steps, 4u, 256u);
		params.Size[3] = view.Frame;
		params.Output[0] = view.Width;
		params.Output[1] = view.Height;
		params.Output[2] = std::clamp(Settings.ShadowSteps, 1u, 16u);

		auto& graph = context.Graph();
		const auto buffer = graph.CreateUpload(std::as_bytes(std::span(&params, 1)), "Cloud params", Swim::Rhi::BufferUsage::Storage, 16);
		const auto clouds = context.CreateTexture(Swim::Rhi::Format::RGBA16Float, width, height, "Clouds");
		context.Compute("VolumetricCloudsMarch").Buffer("Params", buffer).Storage("Clouds", clouds).Dispatch(width, height);
		const auto output = context.CreateColorTarget("Clouds composite");
		context.Compute("VolumetricCloudsComposite")
			.Buffer("Params", buffer)
			.Texture("Color", context.Color())
			.Texture("Depth", context.Depth())
			.Texture("Clouds", clouds)
			.Sampler("LinearClamp")
			.Storage("Output", output)
			.Dispatch(view.Width, view.Height);
		context.SetColor(output);
	}
} // namespace Engine
