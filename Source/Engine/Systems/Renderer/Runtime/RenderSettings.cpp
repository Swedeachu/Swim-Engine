#include "Engine/Systems/Renderer/Runtime/RenderSettings.h"

#include <algorithm>
#include <cmath>

namespace Engine
{

	namespace
	{

		std::uint32_t PowerOfTwoFloor(std::uint32_t value)
		{
			std::uint32_t result = 1;

			while (result * 2 <= value)
			{
				result *= 2;
			}

			return result;
		}

	} // namespace

	void RenderSettings::Sanitize()
	{
		const auto clampFinite = [](float& value, float low, float high, float fallback)
		{
			value = std::isfinite(value) ? std::clamp(value, low, high) : fallback;
		};
		clampFinite(Sharpening, 0.0f, 1.0f, 0.0f);
		clampFinite(Blur.Strength, 0.0f, 1.0f, 0.0f);
		Blur.RegionCount = std::min<std::uint32_t>(Blur.RegionCount, static_cast<std::uint32_t>(Blur.Regions.size()));

		for (auto& region : Blur.Regions)
		{
			for (auto& edge : region)
			{
				clampFinite(edge, -1.0e7f, 1.0e7f, 0.0f);
			}
		}
		MsaaSamples = MsaaSamples >= 16 ? 16u : MsaaSamples >= 8 ? 8u : MsaaSamples >= 4 ? 4u : MsaaSamples >= 2 ? 2u : 1u;
		clampFinite(Sky.GroundFalloff, 1.0f, 256.0f, 2.0f);

		if (SkyBackgroundGround)
		{
			for (auto& channel : *SkyBackgroundGround)
			{
				clampFinite(channel, 0.0f, 1000.0f, 0.0f);
			}
		}

		for (auto& a : Ambient)
		{
			clampFinite(a, 0.0f, 100.0f, 0.0f);
		}

		clampFinite(EnvironmentIntensity, 0.0f, 100.0f, 1.0f);
		clampFinite(EnvironmentRotation, -100.0f, 100.0f, 0.0f);
		clampFinite(EnvironmentRefreshSeconds, 0.0f, 3600.0f, 0.5f);
		EnvironmentResolution = std::clamp(PowerOfTwoFloor(std::max(EnvironmentResolution, 16u)), 16u, 1024u);
		ReflectionProbes.Resolution = std::clamp(PowerOfTwoFloor(std::max(ReflectionProbes.Resolution, 16u)), 16u, 512u);
		ReflectionProbes.MaxProbes = std::clamp(ReflectionProbes.MaxProbes, 1u, Swim::Render::MaxReflectionProbes);
		ReflectionProbes.FacesPerFrame = std::min(ReflectionProbes.FacesPerFrame, Swim::Render::MaxFacesPerFrame);
		ReflectionProbes.PrefilterSamples = std::clamp(ReflectionProbes.PrefilterSamples, 1u, 256u);
		ReflectionProbes.FiltersPerFrame = std::clamp(ReflectionProbes.FiltersPerFrame, 1u, Swim::Render::MaxReflectionProbes);
		clampFinite(ReflectionProbes.MoveThreshold, 0.0f, 100.0f, 0.05f);
		clampFinite(ReflectionProbes.MoverRange, 0.0f, 1000.0f, 15.0f);
		clampFinite(ReflectionProbes.CullDistance, 0.0f, 100000.0f, 100.0f);
		auto& planar = PlanarReflections;
		planar.MaxPlanes = std::clamp(planar.MaxPlanes, 1u, Swim::Render::MaxPlanarReflections);
		planar.AtlasResolution = std::clamp(PowerOfTwoFloor(std::max(planar.AtlasResolution, 64u)), 64u, 2048u);
		planar.MinResolution = std::clamp(planar.MinResolution, 8u, planar.AtlasResolution);
		planar.CapturesPerFrame = std::min(planar.CapturesPerFrame, Swim::Render::MaxPlanarReflections);
		clampFinite(planar.ResolutionScale, 0.05f, 4.0f, 1.0f);
		clampFinite(planar.MinScreenFraction, 0.0f, 1.0f, 0.03f);
		clampFinite(planar.SsrFallbackScreenFraction, planar.MinScreenFraction, 1.0f, 0.1f);
		planar.Supersample = std::clamp(planar.Supersample, 1u, 4u);
		planar.MaxRenderResolution = std::clamp(planar.MaxRenderResolution, planar.AtlasResolution, 4096u);
		clampFinite(planar.PlaneAngleTolerance, 0.0f, 0.5f, 0.035f);
		clampFinite(planar.PlaneDistanceTolerance, 0.0f, 10.0f, 0.05f);
		clampFinite(planar.MotionTolerance, 0.0f, 1000.0f, 0.25f);
		clampFinite(planar.MaxAgeSeconds, 0.0f, 3600.0f, 0.0f);
		clampFinite(planar.SphereMinCosine, 0.5f, 0.999f, 0.75f);
		clampFinite(planar.RoughnessFadeStart, 0.0f, 1.0f, 0.15f);
		clampFinite(planar.RoughnessFadeEnd, planar.RoughnessFadeStart, 1.0f, 0.35f);
		clampFinite(planar.PortalMargin, 0.0f, 2.0f, 0.1f);
		clampFinite(planar.CullDistance, 1.0f, 100000.0f, 150.0f);
		clampFinite(planar.MoverRange, 0.0f, 100000.0f, 25.0f);
		ClusterTileSize = std::clamp(ClusterTileSize, 8u, 256u);
		ClusterSlices = std::clamp(ClusterSlices, 1u, 64u);
		clampFinite(ClusterFar, 1.0f, 100000.0f, 200.0f);
		MaxLightsPerCluster = std::clamp(MaxLightsPerCluster, 1u, 1024u);
		Shadow.AtlasSize = std::clamp(PowerOfTwoFloor(std::max(Shadow.AtlasSize, 256u)), 256u, 8192u);
		Shadow.MinTile = std::clamp(PowerOfTwoFloor(std::max(Shadow.MinTile, 16u)), 16u, Shadow.AtlasSize);
		Shadow.CascadeResolution = std::clamp(PowerOfTwoFloor(std::max(Shadow.CascadeResolution, 64u)), 64u, Shadow.AtlasSize);
		Shadow.SpotResolution = std::clamp(PowerOfTwoFloor(std::max(Shadow.SpotResolution, 64u)), 64u, Shadow.AtlasSize);
		Shadow.PointResolution = std::clamp(PowerOfTwoFloor(std::max(Shadow.PointResolution, 64u)), 64u, Shadow.AtlasSize);
		Shadow.Cascades.Count = std::clamp(Shadow.Cascades.Count, 1u, 4u);
		clampFinite(Shadow.Cascades.MaxDistance, 1.0f, 10000.0f, 70.0f);
		clampFinite(Shadow.Cascades.SplitLambda, 0.0f, 1.0f, 0.8f);
		clampFinite(Shadow.CascadeBlend, 0.0f, 0.5f, 0.2f);
		Temporal.JitterPhases = std::min(Temporal.JitterPhases, Swim::Render::MaxJitterPhases);
		clampFinite(Temporal.Feedback, 0.01f, 1.0f, 0.1f);
		clampFinite(Temporal.ClipGamma, 0.25f, 8.0f, 1.25f);
		clampFinite(Temporal.ReactiveFeedback, 0.01f, 1.0f, 0.8f);
		clampFinite(Temporal.ReactiveClipGamma, 0.25f, 8.0f, 0.5f);
		clampFinite(Post.Exposure.Compensation, -10.0f, 10.0f, 0.0f);
		clampFinite(Post.Bloom.Intensity, 0.0f, 1.0f, 0.04f);
		clampFinite(Post.Bloom.Threshold, 0.0f, 100.0f, 1.0f);
		clampFinite(Post.Grading.Saturation, 0.0f, 4.0f, 1.0f);
		clampFinite(Post.Grading.Contrast, 0.05f, 4.0f, 1.0f);
		clampFinite(Post.Grading.Temperature, -100.0f, 100.0f, 0.0f);
		clampFinite(ScreenSpace.AmbientOcclusion.Radius, 0.01f, 10.0f, 0.5f);
		clampFinite(ScreenSpace.AmbientOcclusion.Power, 0.1f, 8.0f, 1.0f);
		clampFinite(ScreenSpace.Fog.Density, 0.0f, 10.0f, 0.02f);
		clampFinite(ScreenSpace.Fog.HeightFalloff, 0.0f, 10.0f, 0.1f);
		clampFinite(ScreenSpace.Fog.EndDistance, 0.0f, 1.0e6f, 1000.0f);
		clampFinite(ScreenSpace.Fog.Exponent, 0.01f, 64.0f, 3.0f);
		clampFinite(ScreenSpace.Fog.MaxOpacity, 0.0f, 1.0f, 1.0f);
	}

} // namespace Engine
