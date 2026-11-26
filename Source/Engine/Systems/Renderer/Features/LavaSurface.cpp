#include "Engine/Systems/Renderer/Features/LavaSurface.h"

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
			float Tint[4];
			float Crust[4];
			float Motion[4];
		};

		struct QuadRecord
		{
			float Corners[4][4]; // xyz; w: flow x, flow z, heat, falling.
		};

		static_assert(sizeof(Params) == 64 && sizeof(QuadRecord) == 64);

		bool Finite(const std::array<LavaSurface::Float3, 4>& corners, const LavaSurface::Quad& quad)
		{
			for (const auto& corner : corners)
			{
				if (!std::isfinite(corner[0]) || !std::isfinite(corner[1]) || !std::isfinite(corner[2]))
				{
					return false;
				}
			}

			return std::isfinite(quad.Flow[0]) && std::isfinite(quad.Flow[1]) && std::isfinite(quad.Heat);
		}

	} // namespace

	std::array<LavaSurface::Float3, 4> LavaSurface::Quad::GetCorners() const
	{
		if (UseCorners)
		{
			return Corners;
		}

		std::array<Float3, 4> corners{};

		for (int c = 0; c < 3; ++c)
		{
			corners[0][c] = Origin[c];
			corners[1][c] = Origin[c] + AxisU[c];
			corners[2][c] = Origin[c] + AxisU[c] + AxisV[c];
			corners[3][c] = Origin[c] + AxisV[c];
		}

		return corners;
	}

	std::uint32_t LavaSurface::GetGeometryPasses() const
	{
		return RenderGeometryPassBit(RenderGeometryPass::Opaque);
	}

	RenderGeometryResources LavaSurface::PrepareGeometry(RenderFeatureContext& context)
	{
		count = 0;

		if (Quads.empty())
		{
			return {};
		}

		std::vector<QuadRecord> records;
		records.reserve(std::min<std::size_t>(Quads.size(), MaxQuads));

		for (const auto& quad : Quads)
		{
			if (records.size() == MaxQuads)
			{
				break;
			}

			const auto corners = quad.GetCorners();

			if (!Finite(corners, quad))
			{
				continue;
			}

			QuadRecord& record = records.emplace_back();

			for (int k = 0; k < 4; ++k)
			{
				for (int c = 0; c < 3; ++c)
				{
					record.Corners[k][c] = corners[k][c];
				}
			}

			record.Corners[0][3] = std::clamp(quad.Flow[0], -20.0f, 20.0f);
			record.Corners[1][3] = std::clamp(quad.Flow[1], -20.0f, 20.0f);
			record.Corners[2][3] = std::clamp(quad.Heat, 0.0f, 2.0f);
			record.Corners[3][3] = quad.Falling ? 1.0f : 0.0f;
		}

		if (records.empty())
		{
			return {};
		}

		const auto& look = Settings;
		Params p{};
		p.Misc[0] = std::fmod(context.View().Time, 3600.0f);
		p.Misc[1] = std::clamp(look.Intensity, 0.0f, 1000.0f);
		p.Misc[2] = std::clamp(look.Scale, 0.02f, 50.0f);
		p.Misc[3] = std::clamp(look.Crust, 0.0f, 1.0f);

		for (int c = 0; c < 3; ++c)
		{
			p.Tint[c] = std::max(look.Tint[c], 0.0f);
			p.Crust[c] = std::clamp(look.CrustColor[c], 0.0f, 1.0f);
		}

		p.Tint[3] = std::clamp(look.TintMix, 0.0f, 1.0f);
		p.Crust[3] = std::clamp(look.CrustRoughness, 0.02f, 1.0f);
		p.Motion[0] = std::clamp(look.ChurnSpeed, 0.0f, 20.0f);
		p.Motion[1] = std::clamp(look.Pulse, 0.0f, 1.0f);
		p.Motion[2] = std::clamp(look.FallSpeed, 0.0f, 20.0f);
		p.Motion[3] = std::clamp(look.WaveHeight, 0.0f, 1.0f);

		auto& graph = context.Graph();
		params = graph.CreateUpload(std::as_bytes(std::span(&p, 1)), "Lava params", Swim::Rhi::BufferUsage::Storage, 16);
		quads = graph.CreateUpload(std::as_bytes(std::span(records)), "Lava quads", Swim::Rhi::BufferUsage::Storage, 16);
		count = static_cast<std::uint32_t>(records.size());
		RenderGeometryResources resources;
		resources.Buffers = { params, quads };
		return resources;
	}

	void LavaSurface::DrawGeometry(RenderFeatureDrawContext& context)
	{
		if (count == 0 || context.Pass() != RenderGeometryPass::Opaque)
		{
			return;
		}

		context.Program("LavaSurface").Buffer("Params", params).Buffer("Quads", quads).Draw(count * 6);
	}

} // namespace Engine
