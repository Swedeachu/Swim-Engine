#pragma once

#include "Engine/Systems/Renderer/PostProcess/PostProcessSettings.h"
#include "Engine/Systems/Renderer/Runtime/RenderFeature.h"

#include <array>
#include <cstdint>
#include <string_view>

// The camera as render features, each in the stage where its physics happens:
//   scene (linear HDR) -> DepthOfField (the aperture) -> CameraLens (the glass: distortion,
//   chromatic aberration, softness, vignetting, halation, filter) -> [exposure, bloom,
//   white balance, contrast, saturation, tone mapping: PostProcessSettings] -> FilmSensor
//   (display-referred: grain, sharpening).
// A CameraLook describes a camera physically (lens speed and quality, film or digital,
// ISO, white balance...); DeriveCameraLook turns it into the settings of the three
// features and the grading, so presets are data, not hand-tuned effect values.
namespace Engine
{

	// Thin-lens depth of field. Programs: DepthOfFieldPrepare, DepthOfFieldGather, DepthOfField.
	class DepthOfField final : public RenderFeature
	{

	  public:

		struct SettingsData
		{
			float FNumber = 2.8f;			// Aperture N (>= 0.7): smaller is a shallower focus.
			float FocusDistance = 0.0f;		// Metres; 0 = autofocus on what the centre of the frame sees.
			float MinFocusDistance = 0.3f;	// Autofocus never focuses closer.
			float SensorHeightMm = 24.0f;	// Full frame; the focal length follows the camera's field of view.
			float MaxBlurPixels = 16.0f;	// Largest CoC radius at 1080p (scales with the height).
			float AnamorphicSqueeze = 1.0f; // >= 1: oval bokeh, taller than wide.
			std::uint32_t Samples = 48;		// Gather taps (8..128).
		};

		SettingsData Settings;

		DepthOfField() { Enabled = false; }

		std::string_view GetName() const override { return "Depth of field"; }

		RenderFeatureStage GetStage() const override { return RenderFeatureStage::BeforePostProcess; }

		int GetOrder() const override { return 20; }

		void Record(RenderFeatureContext& context) override;

		// The focal length (metres) of a lens giving a vertical half field of view whose
		// tangent is tanHalfFovY on a sensor sensorHeightMm tall.
		static float FocalLength(float tanHalfFovY, float sensorHeightMm);

		// Signed thin-lens CoC diameter on the sensor (metres): < 0 in front of the focus.
		static float CircleOfConfusion(float focalLength, float fNumber, float focus, float distance);

	};

	// The lens glass. Programs: CameraLensHalation, CameraLens.
	class CameraLens final : public RenderFeature
	{

	  public:

		struct SettingsData
		{
			float Distortion = 0.0f;		   // k1 of the radial polynomial: > 0 barrel, < 0 pincushion.
			float DistortionK2 = 0.0f;		   // r^4 term.
			float Fisheye = 0.0f;			   // 0..1: blend toward an equidistant fisheye projection.
			float Zoom = 1.0f;				   // Extra magnification after the automatic corner fit.
			float ChromaticAberration = 0.0f;  // Lateral: relative radius difference of red and blue at the corners.
			float Softness = 0.0f;			   // Corner blur radius (pixels at 1080p).
			float Vignette = 0.0f;			   // Exponent of the cos^4 falloff (1 = an ideal thin lens).
			float Halation = 0.0f;			   // Glow of highlights through the film base.
			float HalationThreshold = 2.0f;	   // Scene luminance where it starts (soft knee).
			std::array<float, 3> HalationTint{ 1.0f, 0.35f, 0.15f };
			float HalationStretch = 1.0f;	   // >= 1: horizontal stretch (anamorphic).
			std::array<float, 3> Filter{ 1.0f, 1.0f, 1.0f }; // Lens colour filter (linear transmission).
		};

		SettingsData Settings;

		CameraLens() { Enabled = false; }

		std::string_view GetName() const override { return "Camera lens"; }

		RenderFeatureStage GetStage() const override { return RenderFeatureStage::BeforePostProcess; }

		int GetOrder() const override { return 30; }

		void Record(RenderFeatureContext& context) override;

		// Whether any setting changes the image (else Record adds nothing).
		bool Active() const;

		// The output radius (1 = half-diagonal) -> source radius mapping of CameraLens.slang
		// before the zoom, and the zoom that maps the corners onto the frame's corners.
		static float SourceRadius(float r, float k1, float k2, float fisheye);

		static float FitZoom(float k1, float k2, float fisheye);

	};

	// The sensor or film. Program: FilmSensor.
	class FilmSensor final : public RenderFeature
	{

	  public:

		struct SettingsData
		{
			float Sharpen = 0.0f;	 // 0..1, contrast-adaptive.
			float Grain = 0.0f;		 // Noise amplitude in display values (mid-tones).
			float GrainSize = 1.5f;	 // Pixels at 1080p.
			float GrainColor = 0.2f; // 0: luminance grain, 1: independent per channel.
		};

		SettingsData Settings;

		FilmSensor() { Enabled = false; }

		std::string_view GetName() const override { return "Film sensor"; }

		RenderFeatureStage GetStage() const override { return RenderFeatureStage::AfterPostProcess; }

		int GetOrder() const override { return 10; }

		void Record(RenderFeatureContext& context) override;

		bool Active() const { return Settings.Sharpen > 0.0f || Settings.Grain > 0.0f; }

	};

	enum class CameraPreset : std::uint32_t
	{
		Off = 0,			  // Every camera effect off, neutral grading.
		CleanModern = 1,	  // A modern mirrorless camera and a sharp prime.
		Cinematic35mm = 2,	  // A 35 mm film camera with vintage-coated cine primes.
		Anamorphic = 3,		  // 2x anamorphic cine lens on film.
		Vintage = 4,		  // An old consumer camera: soft, warm, grainy, heavy vignette.
		NeutralPhotoreal = 5, // A calibrated digital reference: only the optics that are always there.
	};

	inline constexpr std::uint32_t CameraPresetCount = 6;
	std::string_view CameraPresetName(CameraPreset preset);

	// A camera described physically. Every effect value is derived from it.
	struct CameraLook
	{
		bool Enabled = false;
		float FNumber = 2.8f;			// Lens speed: wider apertures vignette more and focus shallower.
		bool DepthOfField = false;
		float LensAge = 0.0f;			// 0: a modern computed design; 1: an uncorrected old one (aberrations, softness).
		float Distortion = 0.0f;		// Residual k1 of the design.
		float AnamorphicSqueeze = 1.0f; // 1: spherical; 2: 2x anamorphic.
		bool Film = false;				// Film: halation and chroma grain; digital: clean luminance noise.
		float Iso = 100.0f;				// Grain grows with sqrt(ISO).
		float WhiteBalanceKelvin = 6500.0f; // The scene is lit at 6500 K; a lower setting renders it warmer.
		float Contrast = 1.0f;			// Stock or picture profile contrast.
		float Saturation = 1.0f;
		std::array<float, 3> Filter{ 1.0f, 1.0f, 1.0f };
		float Sharpening = 0.0f;		// In-camera sharpening.
	};

	CameraLook CameraPresetLook(CameraPreset preset);

	struct CameraLookSettings
	{
		bool DepthOfFieldEnabled = false;
		DepthOfField::SettingsData Focus;
		bool LensEnabled = false;
		CameraLens::SettingsData Optics;
		bool SensorEnabled = false;
		FilmSensor::SettingsData Film;
		float Temperature = 0.0f; // ColorGradingSettings units.
		float Contrast = 1.0f;
		float Saturation = 1.0f;
	};

	CameraLookSettings DeriveCameraLook(const CameraLook& look);

	// Applies derived settings to the features (any may be null) and the grading.
	void ApplyCameraLook(const CameraLookSettings& settings, DepthOfField* dof, CameraLens* lens, FilmSensor* sensor,
		Swim::Render::ColorGradingSettings& grading);

} // namespace Engine
