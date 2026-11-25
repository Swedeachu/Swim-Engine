#include "Game/Features/CullDemo.h"

#include <cmath>
#include <span>

namespace Game
{

	namespace
	{

		float Hash(std::uint32_t x)
		{
			x ^= x >> 16;
			x *= 0x7feb352du;
			x ^= x >> 15;
			x *= 0x846ca68bu;
			x ^= x >> 16;
			return float(x & 0xffffffu) / 16777216.0f;
		}

	} // namespace

	std::uint32_t CullDemo::GetGeometryPasses() const
	{
		using Engine::RenderGeometryPass;
		return Engine::RenderGeometryPassBit(RenderGeometryPass::DepthPrepass) | Engine::RenderGeometryPassBit(RenderGeometryPass::Opaque) |
			Engine::RenderGeometryPassBit(RenderGeometryPass::Shadow) | Engine::RenderGeometryPassBit(RenderGeometryPass::Transparent);
	}

	// The grid (items 0..count-1) and the wall (item count, not culled).
	void CullDemo::Build()
	{
		count = Columns * Rows;
		builtOrigin = Origin;
		boxData.assign(count + 1, {});
		boundsData.assign(count, {});
		commandData.assign(count, {});

		for (std::uint32_t i = 0; i < count; ++i)
		{
			const float half = 0.15f + 0.1f * Hash(i * 3u + 1u);
			const float height = half * (1.0f + 3.0f * Hash(i * 7u + 2u));
			const std::array<float, 3> center{ Origin[0] + float(i % Columns) * Spacing, Origin[1] + height, Origin[2] + float(i / Columns) * Spacing };
			const float hue = Hash(i * 5u + 3u);
			auto& box = boxData[i];
			box = { center[0], center[1], center[2], 0.0f, half, height, half, 0.0f };

			for (int k = 0; k < 3; ++k)
			{
				box[8 + k] = 0.5f + 0.5f * std::cos(6.2831853f * (hue + 0.33f * float(k)));
			}

			box[11] = 0.6f;
			boundsData[i] = { center[0] - half, center[1] - height, center[2] - half, 0.0f, center[0] + half, center[1] + height, center[2] + half, 0.0f };
			commandData[i] = { 36, 1, 0, i };
		}

		const float width = float(Columns) * Spacing;
		boxData[count] = { Origin[0] + width * 0.5f - Spacing * 0.5f, Origin[1] + 3.0f, Origin[2] - 2.0f, 0.0f, width * 0.5f + 2.0f, 3.0f, 0.3f, 0.0f, 0.7f,
			0.7f, 0.7f, 0.9f };
	}

	Engine::RenderGeometryResources CullDemo::PrepareGeometry(Engine::RenderFeatureContext& context)
	{
		if (boxData.empty() || count != Columns * Rows || builtOrigin != Origin)
		{
			Build();
		}

		auto& graph = context.Graph();
		boxes = graph.CreateUpload(std::as_bytes(std::span(boxData)), "Cull demo boxes", Swim::Rhi::BufferUsage::Storage, 16);
		Engine::IndirectCullItems items;
		items.Bounds = graph.CreateUpload(std::as_bytes(std::span(boundsData)), "Cull demo bounds", Swim::Rhi::BufferUsage::Storage, 16);
		items.Commands = graph.CreateUpload(std::as_bytes(std::span(commandData)), "Cull demo commands", Swim::Rhi::BufferUsage::Storage, 16);
		items.Count = count;
		culler.Occlusion = Occlusion;
		culler.Early(context, items);
		Engine::RenderGeometryResources resources{ { boxes } };
		culler.Declare(resources);
		return resources;
	}

	void CullDemo::CullGeometry(Engine::RenderFeatureContext& context, const Engine::RenderFeatureOcclusion& occlusion)
	{
		culler.Late(context, occlusion);
	}

	void CullDemo::DrawGeometry(Engine::RenderFeatureDrawContext& context)
	{
		using Engine::RenderGeometryPass;
		const auto pass = context.Pass();

		if (pass == RenderGeometryPass::Transparent)
		{
			if (Xray)
			{
				const std::array<std::array<float, 4>, 2> tints{ { { 0.0f, 3.0f, 0.2f, 0.35f }, { 4.0f, 0.1f, 0.0f, 0.6f } } };
				culler.List(context)->Draw(context.Program("CullDemoXray").Buffer("Boxes", boxes).Constants(tints[0]));
				culler.LateList().Draw(context.Program("CullDemoXray").Buffer("Boxes", boxes).Constants(tints[1]));
			}

			return;
		}

		const auto list = culler.List(context);
		const char* program = pass == RenderGeometryPass::DepthPrepass ? "CullDemoDepth" : pass == RenderGeometryPass::Shadow ? "CullDemoShadow" : "CullDemo";
		const std::array<std::uint32_t, 4> shadow{ context.ShadowView(), 0, 0, 0 };

		if (Wall)
		{
			auto draw = context.Program(program);
			draw.Buffer("Boxes", boxes);

			if (pass == RenderGeometryPass::Shadow)
			{
				draw.Constants(shadow);
			}

			draw.Draw(36, 1, 0, count);
		}

		if (list)
		{
			auto draw = context.Program(program);
			draw.Buffer("Boxes", boxes);

			if (pass == RenderGeometryPass::Shadow)
			{
				draw.Constants(shadow);
			}

			list->Draw(draw);
		}

		if (pass == RenderGeometryPass::Opaque)
		{
			Engine::RenderGeometryState late;
			late.SkipsPrepass = true;
			auto draw = context.Program("CullDemo", late);
			culler.LateList().Draw(draw.Buffer("Boxes", boxes));
		}
	}

} // namespace Game
