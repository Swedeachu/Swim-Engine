#include "Engine/Systems/Renderer/Features/WaterQuality.h"

#include "Engine/Systems/Renderer/Features/Ocean.h"
#include "Engine/Systems/Renderer/Features/Underwater.h"

namespace Engine
{

	WaterQualityPreset GetWaterQualityPreset(WaterQuality quality)
	{
		WaterQualityPreset preset;

		switch (quality)
		{
		case WaterQuality::Off:
			preset = { false, 64, 1, 64, false, false, false, 0, false, false };
			break;
		case WaterQuality::Low:
			preset = { true, 64, 2, 128, true, false, false, 0, false, false };
			break;
		case WaterQuality::Medium:
			preset = { true, 128, 3, 192, true, true, false, 0, true, false };
			break;
		case WaterQuality::High:
			preset = { true, 256, 3, 256, true, true, false, 12, true, true };
			break;
		case WaterQuality::Ultra:
			preset = { true, 512, 3, 384, true, true, true, 24, true, true };
			break;
		}

		return preset;
	}

	void ApplyWaterQuality(WaterQuality quality, OceanSimulation* simulation, OceanSurface* surface, Underwater* underwater)
	{
		const auto preset = GetWaterQualityPreset(quality);

		if (simulation)
		{
			simulation->Settings.Resolution = preset.OceanResolution;
			simulation->Settings.Cascades = preset.OceanCascades;
		}

		if (surface)
		{
			surface->Enabled = preset.Enabled;
			surface->Settings.GridResolution = preset.SurfaceGrid;
		}

		if (underwater)
		{
			auto& settings = underwater->Settings;
			settings.Medium = preset.UnderwaterMedium;
			settings.ChromaticCaustics = preset.ChromaticCaustics;
			settings.ShaftSteps = preset.ShaftSteps;
			settings.Lens = preset.UnderwaterLens;
		}
	}

} // namespace Engine
