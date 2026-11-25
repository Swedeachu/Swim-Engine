#include "Engine/Systems/Renderer/Features/CelestialSky.h"

#include "Engine/Math/VectorMath.h"
#include "Engine/Systems/Renderer/Runtime/ShaderLibrary.h"

#include <algorithm>
#include <cmath>
#include <span>

#include <glm/glm.hpp>

namespace Engine
{

	namespace
	{

		using Float4 = std::array<float, 4>;
		constexpr float Pi = 3.14159265f;

		struct CelestialParams
		{
			Float4 Forward, Right, Up, SunDirection, SunColor, MoonDirection, MoonLight, MoonParams;
			std::array<Float4, 3> WorldToStar;
			std::array<Float4, 3> StarToGalaxy;
			Float4 Stars, Night;
			std::array<Float4, 4> MeteorStart;
			std::array<Float4, 4> MeteorEnd;
			std::uint32_t Size[4];
		};

		glm::vec3 Normalize(const glm::vec3& v)
		{
			return ::Engine::NormalizeOr(v, { 0.0f, 1.0f, 0.0f }, 1.0e-8f);
		}

		// Rotation about a unit axis (Rodrigues). Not glm::rotate: that renormalizes the axis.
		glm::mat3 Rotation(const glm::vec3& axis, float angle)
		{
			const float c = std::cos(angle), s = std::sin(angle), t = 1.0f - c;
			const float x = axis.x, y = axis.y, z = axis.z;
			return glm::transpose(glm::mat3(t * x * x + c, t * x * y - s * z, t * x * z + s * y, t * x * y + s * z, t * y * y + c,
				t * y * z - s * x, t * x * z - s * y, t * y * z + s * x, t * z * z + c));
		}

		Float4 Row(const glm::vec3& v)
		{
			return { v.x, v.y, v.z, 0.0f };
		}

	} // namespace

	void CelestialSky::Record(RenderFeatureContext& context)
	{
		const auto& view = context.View();
		const auto& s = Settings;
		auto* device = context.Device();

		if (!device)
		{
			return;
		}

		if (!deepSky)
		{
			Swim::Rhi::TextureDesc desc;
			desc.PixelFormat = Swim::Rhi::Format::RGBA16Float;
			desc.Usage = Swim::Rhi::TextureUsage::Sampled | Swim::Rhi::TextureUsage::Storage;
			desc.Extent = { BakeWidth, BakeHeight * 2u, 1 };
			desc.DebugName = "Deep sky";
			deepSky = device->CreateTexture(desc);
			baked = false;

			if (!deepSky)
			{
				return;
			}
		}

		auto& graph = context.Graph();
		const auto& load = context.GetServices().LoadCompute;
		const void* bakeProgram = load ? load("CelestialSkyBake").Pipeline.get() : nullptr;
		baked = baked && bakeProgram == bakedWith;
		const auto deep = graph.ImportTexture(*deepSky, baked ? Swim::Rhi::ResourceState::ShaderRead : Swim::Rhi::ResourceState::Undefined);

		if (!baked)
		{
			context.Compute("CelestialSkyBake").Storage("DeepSky", deep).Dispatch(BakeWidth, BakeHeight * 2u);
			baked = true;
			bakedWith = bakeProgram;
		}

		graph.Export(deep, Swim::Rhi::ResourceState::ShaderRead);

		const glm::vec3 sun = Normalize(::Engine::ToGlm(view.SunDirection));
		// Stars come out between the sun 2 and 12 degrees below the horizon.
		const float night = std::clamp((0.035f - sun[1]) / 0.17f, 0.0f, 1.0f);
		const float visibility = std::clamp(s.Visibility, 0.0f, 1.0f);
		const float dt = std::clamp(view.DeltaTime, 0.0f, 0.25f);

		// Meteors: random arrivals while the sky is dark, each a short arc high in the sky.
		std::uniform_real_distribution<float> unit(0.0f, 1.0f);

		for (auto& meteor : meteors)
		{
			meteor.Age += dt;
		}

		std::erase_if(meteors, [](const Meteor& meteor) { return meteor.Age >= meteor.Duration; });
		const float rate = s.MeteorRate * night * visibility / 60.0f;

		if (rate > 0.0f && (meteorTimer -= dt) <= 0.0f)
		{
			meteorTimer = -std::log(std::max(unit(random), 1.0e-4f)) / rate;

			if (meteors.size() < MaxMeteors)
			{
				const float azimuth = unit(random) * 2.0f * Pi;
				const float elevation = 0.35f + unit(random) * 0.9f;
				const glm::vec3 start{ std::cos(elevation) * std::sin(azimuth), std::sin(elevation),
					std::cos(elevation) * std::cos(azimuth) };
				const glm::vec3 side =
					Normalize(glm::cross(start, glm::vec3{ unit(random) - 0.5f, unit(random) - 0.5f, unit(random) - 0.5f }));
				const float length = 0.08f + unit(random) * 0.25f;
				glm::vec3 end = start + side * length;
				end.y -= 0.3f * length;
				meteors.push_back({ ::Engine::ToArray(start), ::Engine::ToArray(Normalize(end)), 0.0f, 0.35f + unit(random) * 0.8f,
					(0.3f + 1.7f * unit(random) * unit(random)) });
			}
		}
		else if (rate <= 0.0f)
		{
			meteorTimer = 0.0f;
		}

		CelestialParams params{};
		params.Forward = { view.Forward[0], view.Forward[1], view.Forward[2], view.TanHalfFovX };
		params.Right = { view.Right[0], view.Right[1], view.Right[2], view.TanHalfFovY };
		params.Up = { view.Up[0], view.Up[1], view.Up[2], view.Time };

		const float sunRadius = std::clamp(s.SunSize, 0.01f, 20.0f) * Pi / 180.0f;
		params.SunDirection = { sun[0], sun[1], sun[2], std::cos(sunRadius) };
		// The disc is much brighter than the sky's sun glow; bloom carries it.
		const float disc = 60.0f * std::max(s.SunIntensity, 0.0f);
		params.SunColor = { view.SunColor[0] * disc, view.SunColor[1] * disc, view.SunColor[2] * disc, std::clamp(s.LimbDarkening, 0.0f, 1.0f) };

		glm::vec3 moon = ::Engine::ToGlm(s.MoonDirection);
		moon = glm::dot(moon, moon) > 1.0e-8f ? Normalize(moon) : -sun;
		const float moonRadius = std::clamp(s.MoonSize, 0.01f, 20.0f) * Pi / 180.0f;
		params.MoonDirection = { moon[0], moon[1], moon[2], std::cos(moonRadius) };
		// The phase sets where the light comes from: full (0.5) from behind the viewer, new
		// (0, 1) from behind the moon, waxing from the west side.
		const float phaseAngle = (std::clamp(s.MoonPhase, 0.0f, 1.0f) - 0.5f) * 2.0f * Pi;
		const glm::vec3 east = Normalize(glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(moon.x + 1.0e-4f, moon.y, moon.z)));
		const glm::vec3 light = -moon * std::cos(phaseAngle) - east * std::sin(phaseAngle);
		params.MoonLight = { light[0], light[1], light[2], std::max(s.MoonBrightness, 0.0f) };
		params.MoonParams = { std::max(s.Earthshine, 0.0f), visibility, s.Moon ? 1.0f : 0.0f, s.Sun && sun[1] > -0.05f ? 1.0f : 0.0f };

		// The celestial sphere: its pole (star +Y) Latitude above the north (-Z) horizon,
		// turned by the sidereal angle.
		const float latitude = std::clamp(s.Latitude, -90.0f, 90.0f) * Pi / 180.0f;
		const glm::vec3 pole{ 0.0f, std::sin(latitude), -std::cos(latitude) };
		const glm::vec3 x{ 1.0f, 0.0f, 0.0f };
		const glm::mat3 base(x, pole, glm::cross(x, pole)); // Columns: star axes in the world.
		const glm::mat3 starToWorld = Rotation(pole, s.SiderealAngle) * base;
		const glm::mat3 galaxyToStar =
			Rotation({ 0.0f, 1.0f, 0.0f }, s.GalaxyRotation * Pi / 180.0f) * Rotation({ 1.0f, 0.0f, 0.0f }, s.GalaxyTilt * Pi / 180.0f);

		// Rows of the inverses (transposes) are the columns of these.
		for (int i = 0; i < 3; ++i)
		{
			params.WorldToStar[i] = Row(starToWorld[i]);
			params.StarToGalaxy[i] = Row(galaxyToStar[i]);
		}

		// About 9000 stars at density 1 over 6 faces of 200 x 200 cells.
		const float density = std::clamp(s.StarDensity, 0.0f, 3.0f);
		const float cells = 200.0f;
		params.Stars = { cells, std::min(density * 9000.0f / (6.0f * cells * cells), 1.0f),
			std::max(s.StarBrightness, 0.0f) * std::max(s.NightIntensity, 0.0f), std::clamp(s.Twinkle, 0.0f, 1.0f) };
		params.Night = { night, visibility, std::max(s.GalaxyIntensity, 0.0f), std::max(s.NebulaIntensity, 0.0f) };

		for (std::size_t i = 0; i < meteors.size() && i < MaxMeteors; ++i)
		{
			const auto& meteor = meteors[i];
			params.MeteorStart[i] = { meteor.Start[0], meteor.Start[1], meteor.Start[2], meteor.Age / meteor.Duration };
			params.MeteorEnd[i] = { meteor.End[0], meteor.End[1], meteor.End[2],
				meteor.Brightness * std::max(s.MeteorBrightness, 0.0f) * std::max(s.NightIntensity, 0.0f) * 4.0f };
		}

		params.Size[0] = view.Width;
		params.Size[1] = view.Height;

		const auto buffer = graph.CreateUpload(std::as_bytes(std::span(&params, 1)), "Celestial params", Swim::Rhi::BufferUsage::Storage, 16);
		const auto output = context.CreateColorTarget("Celestial sky");
		context.Compute("CelestialSky")
			.Buffer("Params", buffer)
			.Texture("Color", context.Color())
			.Texture("Depth", context.Depth())
			.Texture("DeepSky", deep)
			.Sampler("LinearRepeat", "LinearRepeat")
			.Storage("Output", output)
			.Dispatch(view.Width, view.Height);
		context.SetColor(output);
	}

} // namespace Engine
