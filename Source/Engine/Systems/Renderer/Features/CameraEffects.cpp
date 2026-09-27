#include "Engine/Systems/Renderer/Features/CameraEffects.h"

#include <algorithm>
#include <cmath>

namespace Engine
{
	namespace
	{
		std::uint32_t Half(std::uint32_t size)
		{
			return std::max(1u, (size + 1u) / 2u);
		}

		float Clamp01(float value)
		{
			return std::clamp(std::isfinite(value) ? value : 0.0f, 0.0f, 1.0f);
		}

		// The chromaticity x of the CIE daylight locus at a correlated colour temperature.
		float DaylightX(float kelvin)
		{
			const float t = std::clamp(kelvin, 4000.0f, 25000.0f);
			return t <= 7000.0f ? -4.6070e9f / (t * t * t) + 2.9678e6f / (t * t) + 0.09911e3f / t + 0.244063f
								: -2.0064e9f / (t * t * t) + 1.9018e6f / (t * t) + 0.24748e3f / t + 0.237040f;
		}
	} // namespace

	// --- Depth of field -------------------------------------------------------------------

	float DepthOfField::FocalLength(float tanHalfFovY, float sensorHeightMm)
	{
		return 0.5f * sensorHeightMm * 1.0e-3f / std::max(tanHalfFovY, 1.0e-4f);
	}

	float DepthOfField::CircleOfConfusion(float focalLength, float fNumber, float focus, float distance)
	{
		const float aperture = focalLength / std::max(fNumber, 0.7f);
		return aperture * focalLength * (distance - focus) / (std::max(distance, 1.0e-4f) * std::max(focus - focalLength, 1.0e-4f));
	}

	void DepthOfField::Record(RenderFeatureContext& context)
	{
		const auto& view = context.View();
		if (view.Width == 0 || view.Height == 0 || !(Settings.MaxBlurPixels > 0.0f))
		{
			return;
		}
		struct Constants
		{
			float Lens[4];
			float Blur[4];
			std::uint32_t Size[4];
			std::uint32_t Params[4];
		} constants{};

		static_assert(sizeof(Constants) == 64);
		const float sensor = std::clamp(Settings.SensorHeightMm, 1.0f, 100.0f);
		const float focal = FocalLength(view.TanHalfFovY, sensor);
		const float scale = float(view.Height) / 1080.0f;
		constants.Lens[0] = focal;
		constants.Lens[1] = focal / std::max(Settings.FNumber, 0.7f);
		constants.Lens[2] = std::max(Settings.FocusDistance, 0.0f);
		constants.Lens[3] = float(view.Height) / (sensor * 1.0e-3f);
		constants.Blur[0] = std::clamp(Settings.MaxBlurPixels, 0.0f, 64.0f) * scale;
		constants.Blur[1] = std::max(view.Projection[11], 1.0e-4f);
		constants.Blur[2] = std::clamp(Settings.AnamorphicSqueeze, 1.0f, 3.0f);
		constants.Blur[3] = std::max(Settings.MinFocusDistance, focal * 1.5f);
		const std::uint32_t halfWidth = Half(view.Width);
		const std::uint32_t halfHeight = Half(view.Height);
		constants.Size[0] = view.Width;
		constants.Size[1] = view.Height;
		constants.Size[2] = halfWidth;
		constants.Size[3] = halfHeight;
		constants.Params[0] = std::clamp(Settings.Samples, 8u, 128u);
		constants.Params[1] = view.Frame;

		const auto half = context.CreateTexture(Swim::Rhi::Format::RGBA16Float, halfWidth, halfHeight, "Depth of field half");
		context.Compute("DepthOfFieldPrepare")
			.Texture("Color", context.Color())
			.Texture("Depth", context.Depth())
			.Sampler("LinearClamp")
			.Storage("Half", half)
			.Constants(constants)
			.Dispatch(halfWidth, halfHeight);
		const auto blurred = context.CreateTexture(Swim::Rhi::Format::RGBA16Float, halfWidth, halfHeight, "Depth of field blur");
		context.Compute("DepthOfFieldGather")
			.Texture("Half", half)
			.Sampler("LinearClamp")
			.Storage("Blurred", blurred)
			.Constants(constants)
			.Dispatch(halfWidth, halfHeight);
		const auto output = context.CreateColorTarget("Depth of field");
		context.Compute("DepthOfField")
			.Texture("Color", context.Color())
			.Texture("Half", half)
			.Texture("Blurred", blurred)
			.Sampler("LinearClamp")
			.Storage("Output", output)
			.Constants(constants)
			.Dispatch(view.Width, view.Height);
		context.SetColor(output);
	}

	// --- Lens -----------------------------------------------------------------------------

	float CameraLens::SourceRadius(float r, float k1, float k2, float fisheye)
	{
		float source = r * (1.0f + k1 * r * r + k2 * r * r * r * r);
		const float f = Clamp01(fisheye);
		const float maxAngle = f * 1.35f;
		if (maxAngle > 1.0e-3f)
		{
			source = source + (std::tan(std::min(r, 1.1f) * maxAngle) / std::tan(maxAngle) - source) * f;
		}
		return source;
	}

	float CameraLens::FitZoom(float k1, float k2, float fisheye)
	{
		// The corner (r = 1) must sample the source's corner: zoom = source(1).
		return std::max(SourceRadius(1.0f, k1, k2, fisheye), 0.05f);
	}

	bool CameraLens::Active() const
	{
		const auto& s = Settings;
		return s.Distortion != 0.0f || s.DistortionK2 != 0.0f || s.Fisheye > 0.0f || s.Zoom != 1.0f || s.ChromaticAberration != 0.0f ||
			   s.Softness > 0.0f || s.Vignette > 0.0f || s.Halation > 0.0f || s.Filter[0] != 1.0f || s.Filter[1] != 1.0f ||
			   s.Filter[2] != 1.0f;
	}

	void CameraLens::Record(RenderFeatureContext& context)
	{
		const auto& view = context.View();
		if (view.Width == 0 || view.Height == 0 || !Active())
		{
			return;
		}
		struct Constants
		{
			float Distortion[4];
			float Optics[4];
			float Halation[4];
			float Filter[4];
			float View[4];
			std::uint32_t Size[4];
		} constants{};

		static_assert(sizeof(Constants) == 96);
		const auto& s = Settings;
		const float k1 = std::clamp(s.Distortion, -0.5f, 0.5f);
		const float k2 = std::clamp(s.DistortionK2, -0.5f, 0.5f);
		const float fisheye = Clamp01(s.Fisheye);
		constants.Distortion[0] = k1;
		constants.Distortion[1] = k2;
		constants.Distortion[2] = fisheye;
		constants.Distortion[3] = FitZoom(k1, k2, fisheye) * std::clamp(s.Zoom, 0.25f, 4.0f);
		constants.Optics[0] = std::clamp(s.ChromaticAberration, -0.05f, 0.05f);
		constants.Optics[1] = std::clamp(s.Softness, 0.0f, 8.0f);
		constants.Optics[2] = std::clamp(s.Vignette, 0.0f, 4.0f);
		const float halation = std::clamp(s.Halation, 0.0f, 4.0f);
		for (int c = 0; c < 3; ++c)
		{
			constants.Halation[c] = std::max(s.HalationTint[c], 0.0f) * halation;
			constants.Filter[c] = std::max(s.Filter[c], 0.0f);
		}
		constants.Halation[3] = std::max(s.HalationThreshold, 1.0e-3f);
		constants.Filter[3] = std::clamp(s.HalationStretch, 1.0f, 4.0f);
		constants.View[0] = view.TanHalfFovX;
		constants.View[1] = view.TanHalfFovY;
		constants.View[2] = float(view.Width) / float(view.Height);
		const std::uint32_t glowWidth = halation > 0.0f ? std::max(1u, view.Width / 4u) : 1u;
		const std::uint32_t glowHeight = halation > 0.0f ? std::max(1u, view.Height / 4u) : 1u;
		constants.Size[0] = view.Width;
		constants.Size[1] = view.Height;
		constants.Size[2] = glowWidth;
		constants.Size[3] = glowHeight;

		// Without halation the glow is one (black-weighted) texel: the composite still binds it.
		const auto glow = context.CreateTexture(Swim::Rhi::Format::RGBA16Float, glowWidth, glowHeight, "Lens halation");
		context.Compute("CameraLensHalation")
			.Texture("Color", context.Color())
			.Sampler("LinearClamp")
			.Storage("Glow", glow)
			.Constants(constants)
			.Dispatch(glowWidth, glowHeight);
		const auto output = context.CreateColorTarget("Camera lens");
		context.Compute("CameraLens")
			.Texture("Color", context.Color())
			.Texture("Glow", glow)
			.Sampler("LinearClamp")
			.Storage("Output", output)
			.Constants(constants)
			.Dispatch(view.Width, view.Height);
		context.SetColor(output);
	}

	// --- Sensor ---------------------------------------------------------------------------

	void FilmSensor::Record(RenderFeatureContext& context)
	{
		const auto& view = context.View();
		if (view.Width == 0 || view.Height == 0 || !Active())
		{
			return;
		}
		struct Constants
		{
			float Params[4];
			std::uint32_t Size[4];
		} constants{};

		static_assert(sizeof(Constants) == 32);
		constants.Params[0] = Clamp01(Settings.Sharpen);
		constants.Params[1] = std::clamp(Settings.Grain, 0.0f, 0.5f);
		constants.Params[2] = std::clamp(Settings.GrainSize, 0.5f, 8.0f) * float(view.Height) / 1080.0f;
		constants.Params[3] = Clamp01(Settings.GrainColor);
		constants.Size[0] = view.Width;
		constants.Size[1] = view.Height;
		constants.Size[2] = view.Frame;
		const auto output = context.CreateColorTarget("Film sensor");
		context.Compute("FilmSensor").Texture("Color", context.Color()).Storage("Output", output).Constants(constants).Dispatch(view.Width, view.Height);
		context.SetColor(output);
	}

	// --- Looks ----------------------------------------------------------------------------

	std::string_view CameraPresetName(CameraPreset preset)
	{
		switch (preset)
		{
		case CameraPreset::Off: return "Off";
		case CameraPreset::CleanModern: return "Clean modern";
		case CameraPreset::Cinematic35mm: return "Cinematic 35mm";
		case CameraPreset::Anamorphic: return "Anamorphic";
		case CameraPreset::Vintage: return "Vintage";
		case CameraPreset::NeutralPhotoreal: return "Neutral photoreal";
		}
		return "Unknown";
	}

	CameraLook CameraPresetLook(CameraPreset preset)
	{
		CameraLook look;
		look.Enabled = preset != CameraPreset::Off;
		switch (preset)
		{
		case CameraPreset::Off: break;
		case CameraPreset::CleanModern:
			look.FNumber = 2.8f;
			look.DepthOfField = true;
			look.LensAge = 0.05f;
			look.Iso = 200.0f;
			look.Contrast = 1.03f;
			look.Saturation = 1.04f;
			look.Sharpening = 0.25f;
			break;
		case CameraPreset::Cinematic35mm:
			look.FNumber = 2.0f;
			look.DepthOfField = true;
			look.LensAge = 0.35f;
			look.Distortion = 0.015f;
			look.Film = true;
			look.Iso = 250.0f;
			look.WhiteBalanceKelvin = 6900.0f;
			look.Contrast = 1.06f;
			look.Saturation = 0.95f;
			break;
		case CameraPreset::Anamorphic:
			look.FNumber = 2.8f;
			look.DepthOfField = true;
			look.LensAge = 0.4f;
			look.Distortion = 0.04f;
			look.AnamorphicSqueeze = 2.0f;
			look.Film = true;
			look.Iso = 500.0f;
			look.WhiteBalanceKelvin = 6100.0f;
			look.Contrast = 1.05f;
			look.Saturation = 0.94f;
			break;
		case CameraPreset::Vintage:
			look.FNumber = 3.5f;
			look.LensAge = 1.0f;
			look.Distortion = 0.05f;
			look.Film = true;
			look.Iso = 400.0f;
			look.WhiteBalanceKelvin = 8000.0f;
			look.Contrast = 0.92f;
			look.Saturation = 0.82f;
			look.Filter = { 1.0f, 0.97f, 0.9f };
			break;
		case CameraPreset::NeutralPhotoreal:
			look.FNumber = 5.6f;
			look.DepthOfField = true;
			look.Iso = 100.0f;
			look.Sharpening = 0.1f;
			break;
		}
		return look;
	}

	CameraLookSettings DeriveCameraLook(const CameraLook& look)
	{
		CameraLookSettings out;
		if (!look.Enabled)
		{
			return out; // Everything off, neutral grading.
		}
		const float age = Clamp01(look.LensAge);
		const float squeeze = std::clamp(look.AnamorphicSqueeze, 1.0f, 3.0f);
		const float anamorphic = squeeze - 1.0f;
		const float speed = Clamp01((4.0f - look.FNumber) / 3.0f); // 1 at f/1, 0 from f/4.

		out.DepthOfFieldEnabled = look.DepthOfField;
		out.Focus.FNumber = std::max(look.FNumber, 0.7f);
		out.Focus.AnamorphicSqueeze = squeeze;

		// Optics: modern designs keep a little natural falloff, colour fringing and corner
		// softness; age and speed add to it (fast and old lenses vignette and fringe more);
		// anamorphic front elements add fringing, softness and a horizontal halation streak.
		out.LensEnabled = true;
		out.Optics.Distortion = look.Distortion;
		out.Optics.Vignette = 0.15f + 0.35f * age + 0.25f * speed;
		out.Optics.ChromaticAberration = 0.0005f + 0.004f * age + 0.002f * anamorphic;
		out.Optics.Softness = 0.25f + 2.5f * age + 1.0f * anamorphic;
		out.Optics.Halation = look.Film ? 0.06f + 0.06f * age : 0.0f;
		out.Optics.HalationStretch = 1.0f + 1.5f * anamorphic;
		out.Optics.Filter = look.Filter;

		// Sensor: grain amplitude grows with sqrt(ISO) (shot noise); film grain is coarser
		// and partly chromatic, digital noise fine and mostly luminance.
		const float iso = std::sqrt(std::max(look.Iso, 25.0f) / 100.0f);
		out.SensorEnabled = true;
		out.Film.Grain = (look.Film ? 0.011f : 0.0035f) * iso;
		out.Film.GrainSize = look.Film ? 1.6f : 1.0f;
		out.Film.GrainColor = look.Film ? 0.35f : 0.1f;
		out.Film.Sharpen = Clamp01(look.Sharpening);

		// White balance: the camera neutralizes the daylight-locus white of its Kelvin
		// setting while the scene is lit at 6500 K (D65); the grading's temperature axis
		// moves the white point's chromaticity x by 0.05 (warm) or 0.1 (cool) per 65 units.
		const float shift = DaylightX(6500.0f) - DaylightX(look.WhiteBalanceKelvin);
		out.Temperature = std::clamp(shift / (shift >= 0.0f ? 0.05f : 0.1f) * 65.0f, -100.0f, 100.0f);
		out.Contrast = std::max(look.Contrast, 0.01f);
		out.Saturation = std::max(look.Saturation, 0.0f);
		return out;
	}

	void ApplyCameraLook(const CameraLookSettings& settings, DepthOfField* dof, CameraLens* lens, FilmSensor* sensor,
		Swim::Render::ColorGradingSettings& grading)
	{
		if (dof)
		{
			dof->Enabled = settings.DepthOfFieldEnabled;
			dof->Settings = settings.Focus;
		}
		if (lens)
		{
			lens->Enabled = settings.LensEnabled;
			lens->Settings = settings.Optics;
		}
		if (sensor)
		{
			sensor->Enabled = settings.SensorEnabled;
			sensor->Settings = settings.Film;
		}
		grading.Temperature = settings.Temperature;
		grading.Contrast = settings.Contrast;
		grading.Saturation = settings.Saturation;
	}
} // namespace Engine
