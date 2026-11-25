#include "Engine/Systems/Renderer/Features/Weather.h"

#include "Engine/Systems/Renderer/RenderGraph/RenderGraph.h"

#include <algorithm>
#include <cmath>

namespace Engine
{

	namespace
	{

		// Particles per column cell, and splashes per splash cell.
		constexpr std::uint32_t PerColumn = 8;
		constexpr std::uint32_t PerSplashCell = 2;

		struct Constants
		{
			std::array<float, 4> CameraTime; // Camera xyz, time.
			std::array<float, 4> Fall;		 // Rain speed, snow speed, band height, radius.
			std::array<float, 4> Shape;		 // Drop length, drop width, flake size, radians per pixel.
			std::array<float, 4> Motion;	 // Wind velocity x, z; snow fraction; opacity.
			std::array<float, 4> Color;		 // Lit colour scale rgb; intensity.
			std::array<float, 4> Occlusion;	 // Origin x, z; cell size; size (0: none).
			std::uint32_t Grid[4];			 // Particles drawn, columns per side, per column, splashes drawn.
			std::array<float, 4> Splash;	 // Splash radius, size, cells per side, unused.
		};

		static_assert(sizeof(Constants) == 128);

		std::uint32_t SplashSide(float radius)
		{
			return std::clamp(static_cast<std::uint32_t>(std::ceil(radius * 2.0f / 1.5f)), 1u, 64u);
		}

	} // namespace

	void Weather::ApplyQuality(WeatherQuality quality)
	{
		switch (quality)
		{
		case WeatherQuality::Low:
			Settings.MaxParticles = 4096;
			Settings.Radius = 14.0f;
			Settings.Splashes = false;
			break;
		case WeatherQuality::Medium:
			Settings.MaxParticles = 12288;
			Settings.Radius = 18.0f;
			Settings.Splashes = true;
			Settings.MaxSplashes = 384;
			break;
		case WeatherQuality::High:
			Settings.MaxParticles = 24576;
			Settings.Radius = 22.0f;
			Settings.Splashes = true;
			Settings.MaxSplashes = 768;
			break;
		case WeatherQuality::Ultra:
			Settings.MaxParticles = 49152;
			Settings.Radius = 28.0f;
			Settings.Splashes = true;
			Settings.MaxSplashes = 1536;
			break;
		}
	}

	void Weather::SetOcclusion(float originX, float originZ, float cellSize, std::uint32_t size, std::span<const float> heights)
	{
		if (heights.empty() || size == 0 || heights.size() < static_cast<std::size_t>(size) * size || cellSize <= 0.0f)
		{
			occlusion.assign(1, -1.0e9f);
			occlusionArea = {};
			return;
		}

		occlusion.assign(heights.begin(), heights.begin() + static_cast<std::ptrdiff_t>(size) * size);
		occlusionArea = { originX, originZ, cellSize, static_cast<float>(size) };
	}

	void Weather::Advance(float dt)
	{
		const float target = std::clamp(Settings.Intensity, 0.0f, 1.0f) * (1.0f - std::clamp(Settings.Snow, 0.0f, 1.0f));
		const float rate = target > wetness ? Settings.WetnessRise : Settings.WetnessDry;
		wetness += (target - wetness) * std::clamp(rate * std::max(dt, 0.0f), 0.0f, 1.0f);
	}

	float Weather::GetFogDensity() const
	{
		const float snow = std::clamp(Settings.Snow, 0.0f, 1.0f);
		return std::clamp(Settings.Intensity, 0.0f, 1.0f) * (0.6f + 0.4f * snow);
	}

	RenderGeometryResources Weather::PrepareGeometry(RenderFeatureContext& context)
	{
		const float intensity = std::clamp(Settings.Intensity, 0.0f, 1.0f);
		particles = 0;
		splashes = 0;

		if (intensity <= 0.0f || Settings.MaxParticles == 0)
		{
			return {};
		}

		// The grid depends on the budget only, so the field stays put while the intensity
		// changes; the intensity picks how many of its particles fall.
		const std::uint32_t budget = std::clamp(Settings.MaxParticles, PerColumn, 1u << 20);
		columns = std::max(1u, static_cast<std::uint32_t>(std::ceil(std::sqrt(static_cast<float>(budget / PerColumn)))));
		perColumn = PerColumn;
		particles = static_cast<std::uint32_t>(static_cast<float>(columns * columns * perColumn) * intensity);

		if (Settings.Splashes && occlusionArea[3] > 0.0f && Settings.Snow < 1.0f)
		{
			const auto side = SplashSide(Settings.SplashRadius);
			const auto capacity = std::min(side * side * PerSplashCell, Settings.MaxSplashes);
			splashes =
				static_cast<std::uint32_t>(static_cast<float>(capacity) * intensity * (1.0f - std::clamp(Settings.Snow, 0.0f, 1.0f)));
		}

		occlusionBuffer =
			context.Graph().CreateUpload(std::as_bytes(std::span(occlusion)), "Weather occlusion", Swim::Rhi::BufferUsage::Storage, 16);
		return { { occlusionBuffer }, {}, {} };
	}

	void Weather::DrawGeometry(RenderFeatureDrawContext& context)
	{
		if (context.Pass() != RenderGeometryPass::Transparent || particles + splashes == 0)
		{
			return;
		}

		const auto& view = context.View();
		const float influence = std::clamp(Settings.WindInfluence, 0.0f, 4.0f);
		const auto wind = WindVelocity(Wind, view.Position, view.Time);
		const float brightness = std::max(Settings.Brightness, 0.0f);

		Constants constants{};
		constants.CameraTime = { view.Position[0], view.Position[1], view.Position[2], view.Time };
		constants.Fall = { std::max(Settings.RainSpeed, 0.5f), std::max(Settings.SnowSpeed, 0.1f), std::max(Settings.Height, 2.0f),
			std::max(Settings.Radius, 2.0f) };
		constants.Shape = { std::max(Settings.DropLength, 0.01f), std::max(Settings.DropWidth, 0.001f),
			std::max(Settings.FlakeSize, 0.002f), 2.0f * view.TanHalfFovY / static_cast<float>(std::max(view.Height, 1u)) };
		constants.Motion = { wind[0] * influence, wind[2] * influence, std::clamp(Settings.Snow, 0.0f, 1.0f),
			std::clamp(Settings.Opacity, 0.0f, 1.0f) };
		constants.Color = { Settings.Tint[0] * brightness, Settings.Tint[1] * brightness, Settings.Tint[2] * brightness,
			std::clamp(Settings.Intensity, 0.0f, 1.0f) };
		constants.Occlusion = occlusionArea;
		constants.Grid[0] = particles;
		constants.Grid[1] = columns;
		constants.Grid[2] = perColumn;
		constants.Grid[3] = splashes;
		constants.Splash = { std::max(Settings.SplashRadius, 1.0f), std::max(Settings.SplashSize, 0.01f),
			static_cast<float>(SplashSide(Settings.SplashRadius)), 0.0f };

		context.Program("Weather", {}).Buffer("OcclusionHeights", occlusionBuffer).Constants(constants).Draw(6, particles + splashes);
	}

} // namespace Engine
