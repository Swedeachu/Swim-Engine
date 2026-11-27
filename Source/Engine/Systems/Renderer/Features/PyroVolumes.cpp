#include "Engine/Systems/Renderer/Features/PyroVolumes.h"

#include <algorithm>
#include <cmath>
#include <span>

namespace Engine
{

	namespace
	{

		struct VolumeRecord
		{
			float PositionRadius[4];
			float Shape[4];
			float Color[4];
			float Params[4];
			float Extra[4];
		};

		struct Params
		{
			float Forward[4];
			float Right[4];
			float Up[4];
			float Camera[4];
			float Misc[4];
			float Sun[4];
			float SunColor[4];
			std::uint32_t Size[4];
			VolumeRecord Volumes[PyroVolumes::MaxVolumes];
		};

		static_assert(sizeof(VolumeRecord) == 80 && sizeof(Params) == 128 + 80 * PyroVolumes::MaxVolumes);

		float Finite(float value, float fallback)
		{
			return std::isfinite(value) ? value : fallback;
		}

		float BoundRadius(const PyroVolumes::Volume& volume)
		{
			switch (volume.Kind)
			{
			case PyroVolumes::Type::Flame:
				// The haze above the flame reaches past its tip.
				return std::sqrt(volume.Height * volume.Height * 0.25f + volume.Radius * volume.Radius) + volume.Radius * 1.2f;
			case PyroVolumes::Type::Smoke:
				return std::sqrt(volume.Height * volume.Height * 0.25f + volume.Radius * volume.Radius);
			case PyroVolumes::Type::Haze:
				return std::max(volume.Radius, volume.Height);
			case PyroVolumes::Type::Shockwave:
				return volume.Radius * 1.3f + 0.2f;
			default:
				return volume.Radius * 1.2f;
			}
		}

		// One volume's shader record, every field clamped to its valid range.
		void PackVolume(const PyroVolumes::Volume& volume, float height, float radius, float bound, VolumeRecord& record)
		{
			using Type = PyroVolumes::Type;

			for (int c = 0; c < 3; ++c)
			{
				record.PositionRadius[c] = Finite(volume.Position[c], 0.0f);
				record.Color[c] = std::max(Finite(volume.Tint[c], 1.0f), 0.0f);
				record.Shape[0] = float(static_cast<std::uint32_t>(volume.Kind));
			}

			record.PositionRadius[3] = bound;
			record.Shape[1] = height;
			record.Shape[2] = radius;
			record.Shape[3] = std::clamp(Finite(volume.Age, 0.0f), 0.0f, 1.0f);
			record.Color[3] = std::clamp(Finite(volume.Intensity, 1.0f), 0.0f, 1000.0f);
			record.Params[0] = std::clamp(Finite(volume.Density, 1.0f), 0.0f, 100.0f);
			record.Params[1] = std::clamp(Finite(volume.RiseSpeed, 1.5f), -50.0f, 50.0f);
			record.Params[2] = std::fmod(std::abs(Finite(volume.Seed, 0.0f)), 1000.0f);
			record.Params[3] = std::clamp(Finite(volume.Distortion, 0.0f), 0.0f, 10.0f);
			record.Extra[0] = std::clamp(Finite(volume.TintMix, 0.0f), 0.0f, 1.0f);
			record.Extra[1] = float(std::min(volume.Embers, 32u));
			record.Extra[2] = std::clamp(Finite(volume.SmokeAlbedo, 0.35f), 0.0f, 1.0f);
			record.Extra[3] = std::clamp(Finite(volume.SmokeGlowStrength, 0.0f), 0.0f, 100.0f);

			if (volume.Kind == Type::Smoke)
			{
				for (int c = 0; c < 3; ++c)
				{
					record.Color[c] = std::max(Finite(volume.SmokeGlow[c], 0.0f), 0.0f);
				}
			}
		}

	} // namespace

	void PyroVolumes::Record(RenderFeatureContext& context)
	{
		const auto& view = context.View();

		if (Volumes.empty() || view.Width == 0 || view.Height == 0)
		{
			return;
		}

		Params params{};
		std::uint32_t count = 0;
		const float maxDistance = std::max(Quality.MaxDistance, 1.0f);

		for (const auto& volume : Volumes)
		{
			if (count == MaxVolumes)
			{
				break;
			}

			const float height = std::clamp(Finite(volume.Height, 1.0f), 0.01f, 512.0f);
			const float radius = std::clamp(Finite(volume.Radius, 0.5f), 0.01f, 512.0f);
			Volume clamped = volume;
			clamped.Height = height;
			clamped.Radius = radius;
			const float bound = BoundRadius(clamped);
			float distance2 = 0.0f;

			for (int c = 0; c < 3; ++c)
			{
				const float d = Finite(volume.Position[c], 0.0f) - view.Position[c];
				distance2 += d * d;
			}

			if (std::sqrt(distance2) - bound > maxDistance)
			{
				continue;
			}

			PackVolume(volume, height, radius, bound, params.Volumes[count++]);
		}

		if (count == 0)
		{
			return;
		}

		for (int c = 0; c < 3; ++c)
		{
			params.Forward[c] = view.Forward[c];
			params.Right[c] = view.Right[c];
			params.Up[c] = view.Up[c];
			params.Camera[c] = view.Position[c];
			params.Sun[c] = view.SunDirection[c];
			params.SunColor[c] = view.SunColor[c];
		}

		params.Forward[3] = view.TanHalfFovX;
		params.Right[3] = view.TanHalfFovY;
		params.Up[3] = std::max(view.Projection[11], 1.0e-4f);
		params.Camera[3] = float(count);
		params.Misc[0] = std::fmod(view.Time, 3600.0f);
		params.Misc[1] = 2.0f * view.TanHalfFovY / float(view.Height);
		params.Misc[2] = float(view.Frame % 64u);
		// Many volumes at once (explosion spam): fewer steps and octaves each, so the pass cost
		// stays near that of a few.
		const float crowd = count > 6 ? 0.5f : count > 3 ? 0.7f : 1.0f;
		params.Misc[3] = float(std::clamp(std::uint32_t(float(Quality.Steps) * crowd), 4u, 64u));
		params.Sun[3] = 1.0f;
		params.SunColor[3] = float(std::clamp(Quality.Octaves - (count > 3 && Quality.Octaves > 1 ? 1u : 0u), 1u, 4u));
		params.Size[0] = view.Width;
		params.Size[1] = view.Height;

		auto& graph = context.Graph();
		const auto buffer = graph.CreateUpload(std::as_bytes(std::span(&params, 1)), "Pyro params", Swim::Rhi::BufferUsage::Storage, 16);
		const auto output = context.CreateColorTarget("Pyro volumes");
		context.Compute("PyroVolumes")
			.Buffer("Params", buffer)
			.Texture("Color", context.Color())
			.Texture("Depth", context.Depth())
			.Sampler("LinearClamp")
			.Storage("Output", output)
			.Dispatch(view.Width, view.Height);
		context.SetColor(output);
	}

} // namespace Engine
