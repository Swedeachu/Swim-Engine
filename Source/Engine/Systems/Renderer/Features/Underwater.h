#pragma once

#include "Engine/Systems/Renderer/Runtime/RenderFeature.h"

#include <array>
#include <cstdint>
#include <memory>

namespace Engine
{

	class FrameRenderer;

	// How the world looks from inside water, as two render features driven by one settings
	// block (the game sets Submersion and SurfaceHeight from its own water):
	//  - the medium (before TAA): Beer-Lambert extinction per colour channel (red light
	//    goes first) and in-scattering over the path through water, darker with depth below
	//    the surface; total internal reflection outside Snell's window; caustics on geometry
	//    below the surface, projected along the refracted sun; volumetric light shafts;
	//  - the lens (before exposure): a slight barrel bulge, an animated refraction wobble
	//    and radial chromatic aberration.
	// Both cost nothing while Submersion is 0. Programs: UnderwaterMedium, UnderwaterLens.
	//
	//   auto water = std::make_unique<Engine::Underwater>();
	//   water->Attach(renderer);
	//   water->Settings.Submersion = cameraInWater ? 1.0f : 0.0f;
	//   water->Settings.SurfaceHeight = surfaceY;
	class Underwater
	{

	  public:

		struct SettingsData
		{
			float Submersion = 0.0f;	// 0 dry .. 1 under water; blend over a few frames when crossing the surface.
			float SurfaceHeight = 0.0f; // World height of the water surface above the camera.

			// Medium.
			bool Medium = true;
			std::array<float, 3> Absorption{ 0.42f, 0.085f, 0.055f };  // Extinction per metre, per channel.
			float Density = 1.0f;										 // Scales Absorption (murky water > 1).
			std::array<float, 3> ScatterColor{ 0.012f, 0.07f, 0.085f }; // Radiance of distant water just below the surface.
			float DepthDarkening = 0.035f;								 // Per metre of camera depth below the surface.
			float MaxDistance = 256.0f;									 // m; the sky counts as this far.
			float TotalInternalReflection = 0.6f;						 // 0..1: the surface mirrors the water outside Snell's window.
			float Caustics = 1.0f;										 // Intensity (0 = off).
			float CausticScale = 0.45f;									 // Pattern cells per metre.
			bool ChromaticCaustics = false;								 // Split the caustics into colour fringes.
			std::uint32_t ShaftSteps = 12;								 // Ray-march steps of the light shafts (0 = off).
			float ShaftIntensity = 1.0f;
			float ShaftLength = 32.0f; // m along the view ray.
			float SunIntensity = 1.0f; // Scales the frame's sun for caustics and shafts (night, overcast).

			// Lens.
			bool Lens = true;
			float LensBulge = 0.05f;		   // 0..0.3: magnification at the centre.
			float Wobble = 0.0035f;			   // Refraction wobble amplitude (fraction of the screen).
			float WobbleFrequency = 22.0f;	   // Ripples across the screen.
			float WobbleSpeed = 1.6f;		   // Radians per second.
			float ChromaticAberration = 0.01f; // Radial colour split at the edges (fraction of the screen).
		};

		SettingsData Settings;

		Underwater();

		~Underwater();

		Underwater(const Underwater&) = delete;

		Underwater& operator=(const Underwater&) = delete;

		// Adds both features to the renderer (and removes them from a previous one).
		void Attach(FrameRenderer& renderer);

		void Detach();

	  private:

		class MediumFeature;
		class LensFeature;

		std::shared_ptr<MediumFeature> medium;
		std::shared_ptr<LensFeature> lens;
		FrameRenderer* renderer = nullptr;

	};

} // namespace Engine
