#include "Engine/Systems/Renderer/Features/BlockFire.h"

#include <algorithm>
#include <cmath>
#include <span>

namespace Engine
{

	namespace
	{

		struct Params
		{
			float Misc[4];
			GpuWind Wind;
			float Motion[4];
			std::uint32_t Counts[4];
		};

		struct FireRecord
		{
			float PositionSeed[4];
			float Size[4];
			float Tint[4];
			std::uint32_t Info[4];
		};

		static_assert(sizeof(Params) == 80 && sizeof(FireRecord) == 64);

		float Clean(float value, float fallback, float low, float high)
		{
			return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
		}

	} // namespace

	float BlockFire::Flicker(float time, float seed, float amount)
	{
		const float wave = 0.12f * std::sin(time * 9.3f + seed * 7.0f) + 0.06f * std::sin(time * 23.1f + seed * 3.0f + 1.7f) +
			0.04f * std::sin(time * 41.0f + seed);
		return 1.0f + (wave - 0.02f) * amount * 4.0f;
	}

	std::uint32_t BlockFire::GetGeometryPasses() const
	{
		return RenderGeometryPassBit(RenderGeometryPass::Opaque) | RenderGeometryPassBit(RenderGeometryPass::Transparent);
	}

	RenderGeometryResources BlockFire::PrepareGeometry(RenderFeatureContext& context)
	{
		count = 0;
		particleCount = 0;
		heldTexture.reset();
		frameTexture = nullptr;
		const float time = std::fmod(context.View().Time, 3600.0f);
		const float previous = previousTime > 0.0f && previousTime <= time ? previousTime : time;
		previousTime = time;

		if (Fires.empty())
		{
			return {};
		}

		std::vector<FireRecord> records;
		records.reserve(std::min<std::size_t>(Fires.size(), MaxFires));

		for (const auto& fire : Fires)
		{
			if (records.size() == MaxFires)
			{
				break;
			}

			if (!std::isfinite(fire.Position[0]) || !std::isfinite(fire.Position[1]) || !std::isfinite(fire.Position[2]))
			{
				continue;
			}

			FireRecord& r = records.emplace_back();

			for (int c = 0; c < 3; ++c)
			{
				r.PositionSeed[c] = fire.Position[c];
				r.Tint[c] = Clean(fire.Tint[c], 1.0f, 0.0f, 64.0f);
			}

			r.PositionSeed[3] = std::fmod(std::abs(Clean(fire.Seed, 0.0f, -1.0e6f, 1.0e6f)), 64.0f);
			r.Size[0] = Clean(fire.Width, 1.0f, 0.01f, 64.0f);
			r.Size[1] = Clean(fire.Height, 1.0f, 0.01f, 64.0f);
			r.Size[2] = Clean(fire.Intensity, 1.0f, 0.0f, 100.0f);
			r.Size[3] = Clean(fire.FrameRate, 16.0f, 0.0f, 120.0f);
			r.Info[0] = fire.Layer;
			r.Info[1] = std::clamp(fire.Frames, 1u, 0xFFFFu) | (std::clamp(fire.Rows, 1u, 0xFFFFu) << 16);
			r.Info[2] = std::uint32_t(std::min<std::uint8_t>(static_cast<std::uint8_t>(fire.Kind), 2)) |
				(std::uint32_t(std::min<std::uint8_t>(static_cast<std::uint8_t>(fire.Attach), 5)) << 8);
			r.Info[3] = std::min(fire.Embers, 6u) | (std::min(fire.Sparks, 2u) << 8) | (std::min(fire.Smoke, 3u) << 16);
		}

		if (records.empty())
		{
			return {};
		}

		Params p{};
		p.Misc[0] = time;
		p.Misc[1] = previous;
		p.Misc[2] = Clean(Settings.Emission, 3.0f, 0.0f, 1000.0f);
		p.Misc[3] = Clean(Settings.Glow, 1.0f, 0.0f, 100.0f);
		p.Wind = PackWind(Wind, time);
		p.Motion[0] = Clean(Settings.Sway, 0.06f, 0.0f, 2.0f);
		p.Motion[1] = Clean(Settings.FlickerAmount, 1.0f, 0.0f, 1.0f);
		p.Motion[2] = Clean(Settings.SmokeOpacity, 0.12f, 0.0f, 1.0f);
		count = static_cast<std::uint32_t>(records.size());
		p.Counts[0] = count;
		const std::weak_ptr<Swim::Rhi::TextureView> unset;
		const bool shared = SharedTexture.owner_before(unset) || unset.owner_before(SharedTexture);
		heldTexture = SharedTexture.lock();
		frameTexture = shared ? heldTexture.get() : Texture;
		p.Counts[1] = frameTexture ? frameTexture->GetDesc().ArrayLayerCount : 0u;
		particleCount = std::min(count, Settings.ParticleFires);

		auto& graph = context.Graph();
		params = graph.CreateUpload(std::as_bytes(std::span(&p, 1)), "Block fire params", Swim::Rhi::BufferUsage::Storage, 16);
		fires = graph.CreateUpload(std::as_bytes(std::span(records)), "Block fires", Swim::Rhi::BufferUsage::Storage, 16);
		RenderGeometryResources resources;
		resources.Buffers = { params, fires };
		return resources;
	}

	void BlockFire::DrawGeometry(RenderFeatureDrawContext& context)
	{
		if (count == 0)
		{
			return;
		}

		if (context.Pass() == RenderGeometryPass::Opaque)
		{
			if (frameTexture)
			{
				context.Program("BlockFire")
					.Buffer("Params", params)
					.Buffer("Fires", fires)
					.Texture("FireTexture", *frameTexture)
					.Sampler("FireSampler", "PointClamp")
					.Draw(count * 4 * 6);
			}
			else
			{
				context.Program("BlockFireProcedural").Buffer("Params", params).Buffer("Fires", fires).Draw(count * 4 * 6);
			}
		}
		else if (context.Pass() == RenderGeometryPass::Transparent && particleCount > 0)
		{
			context.Program("BlockFireGlow").Buffer("Params", params).Buffer("Fires", fires).Draw(particleCount * 12 * 6);
		}
	}

} // namespace Engine
