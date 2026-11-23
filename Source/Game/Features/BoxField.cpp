#include "Game/Features/BoxField.h"

#include "Engine/Systems/Renderer/Runtime/RenderSettings.h"

#include <cmath>
#include <span>
#include <stdexcept>

namespace Game
{

	namespace
	{

		struct CullView
		{
			std::array<float, 24> Planes{};
		};

		struct BoxConstants
		{
			std::array<float, 4> Origin{};
			std::array<float, 4> Time{};
			std::uint32_t BoxCount = 0;
			std::uint32_t Columns = 0;
			std::uint32_t ViewCount = 0;
			std::uint32_t Cut = 0;
		};

		struct LateConstants
		{
			std::array<float, 16> ViewProjection{};
			std::uint32_t HzbWidth = 0;
			std::uint32_t HzbHeight = 0;
			std::uint32_t HzbMipCount = 0;
			std::uint32_t Reserved0 = 0;
			std::uint32_t BoxCount = 0;
			std::uint32_t ViewCount = 0;
			std::uint32_t Cut = 0;
			std::uint32_t Reserved1 = 0;
		};

		struct ShadowConstants
		{
			std::uint32_t ViewIndex = 0;
			std::uint32_t Reserved[3]{};
		};

		constexpr std::uint32_t BoxBytes = 48;
		constexpr std::uint32_t ArgumentBytes = sizeof(Swim::Rhi::DrawIndirectCommand);

		// The frustum of a row-major reverse-Z view-projection (clip = M * p): inside where
		// -w <= x, y <= w and 0 <= z <= w.
		CullView FrustumOf(const std::array<float, 16>& m)
		{
			CullView view;
			const auto row = [&](int r, int c)
			{
				return m[r * 4 + c];
			};
			const std::array<std::array<float, 4>, 6> planes{ {
				{ row(3, 0) + row(0, 0), row(3, 1) + row(0, 1), row(3, 2) + row(0, 2), row(3, 3) + row(0, 3) },
				{ row(3, 0) - row(0, 0), row(3, 1) - row(0, 1), row(3, 2) - row(0, 2), row(3, 3) - row(0, 3) },
				{ row(3, 0) + row(1, 0), row(3, 1) + row(1, 1), row(3, 2) + row(1, 2), row(3, 3) + row(1, 3) },
				{ row(3, 0) - row(1, 0), row(3, 1) - row(1, 1), row(3, 2) - row(1, 2), row(3, 3) - row(1, 3) },
				{ row(2, 0), row(2, 1), row(2, 2), row(2, 3) },
				{ row(3, 0) - row(2, 0), row(3, 1) - row(2, 1), row(3, 2) - row(2, 2), row(3, 3) - row(2, 3) },
			} };

			for (std::size_t p = 0; p < planes.size(); ++p)
			{
				for (std::size_t c = 0; c < 4; ++c)
				{
					view.Planes[p * 4 + c] = planes[p][c];
				}
			}

			return view;
		}

	} // namespace

	std::uint32_t BoxField::Random()
	{
		seed = seed * 1664525u + 1013904223u;
		return seed >> 8;
	}

	// A fresh colour scheme for one row, in a newly allocated range.
	void BoxField::WriteRow(std::uint32_t row)
	{
		if (colors->IsValid(rowRanges[row]))
		{
			colors->Free(rowRanges[row]);
		}

		const auto range = colors->Allocate(std::uint64_t(Columns) * 16);

		if (!range)
		{
			return;
		}

		rowRanges[row] = *range;
		const float hue = float(Random() % 1000u) / 1000.0f;
		std::vector<std::array<float, 4>> values(Columns);

		for (std::uint32_t c = 0; c < Columns; ++c)
		{
			const float h = hue + 0.15f * float(c) / float(Columns);

			for (int k = 0; k < 3; ++k)
			{
				values[c][k] = 0.5f + 0.5f * std::cos(6.2831853f * (h + 0.33f * float(k)));
			}

			values[c][3] = 0.35f + 0.5f * float(Random() % 100u) / 100.0f;
		}

		colors->Write(*range, std::as_bytes(std::span(values)));
	}

	std::uint32_t BoxField::GetGeometryPasses() const
	{
		using Engine::RenderGeometryPass;
		return Engine::RenderGeometryPassBit(RenderGeometryPass::DepthPrepass) | Engine::RenderGeometryPassBit(RenderGeometryPass::Opaque) |
			Engine::RenderGeometryPassBit(RenderGeometryPass::Shadow);
	}

	void BoxField::PaintLayer(std::uint32_t layer, std::uint32_t variant)
	{
		constexpr std::uint32_t Size = 64;
		Swim::Render::TextureLayerData data;
		data.Format = Swim::Rhi::Format::RGBA8UnormSrgb;
		data.Width = Size;
		data.Height = Size;
		auto& pixels = data.Mips.emplace_back(std::size_t(Size) * Size * 4);
		const std::uint32_t hue = variant * 2654435761u;
		const std::array<std::uint8_t, 3> tint{ static_cast<std::uint8_t>(96 + (hue >> 8) % 160), static_cast<std::uint8_t>(96 + (hue >> 16) % 160),
			static_cast<std::uint8_t>(96 + (hue >> 24) % 160) };

		for (std::uint32_t y = 0; y < Size; ++y)
		{
			for (std::uint32_t x = 0; x < Size; ++x)
			{
				bool on = false;

				switch ((layer + variant) % 3)
				{
				case 0:
					on = ((x / 8) + (y / 8)) % 2 == 0;
					break;
				case 1:
					on = ((x + y) / 6) % 2 == 0;
					break;
				default:
				{
					const float dx = static_cast<float>(x) - 31.5f;
					const float dy = static_cast<float>(y) - 31.5f;
					on = static_cast<int>(std::sqrt(dx * dx + dy * dy) / 5.0f) % 2 == 0;
					break;
				}
				}

				auto* texel = pixels.data() + (std::size_t(y) * Size + x) * 4;
				texel[0] = std::byte{ on ? tint[0] : std::uint8_t(255) };
				texel[1] = std::byte{ on ? tint[1] : std::uint8_t(255) };
				texel[2] = std::byte{ on ? tint[2] : std::uint8_t(255) };
				texel[3] = std::byte{ 255 };
			}
		}

		faces->SetLayer(layer, std::move(data));
	}

	Engine::RenderGeometryResources BoxField::PrepareGeometry(Engine::RenderFeatureContext& context)
	{
		boxCount = Columns * Rows;
		std::vector<CullView> cullViews{ FrustumOf(context.View().ViewProjection) };
		shadowSlots.clear();

		for (const auto& shadow : context.ShadowViews())
		{
			if (shadowSlots.size() <= shadow.Index)
			{
				shadowSlots.resize(shadow.Index + 1, -1);
			}

			if (shadow.Rendered)
			{
				shadowSlots[shadow.Index] = static_cast<int>(cullViews.size());
				cullViews.push_back(FrustumOf(shadow.ViewProjection));
			}
		}

		viewCount = static_cast<std::uint32_t>(cullViews.size());
		const float now = context.View().Time;

		if (!history || history->GetDesc().Size != std::uint64_t(boxCount) * sizeof(std::uint32_t))
		{
			Swim::Rhi::BufferDesc historyDesc;
			historyDesc.Size = std::uint64_t(boxCount) * sizeof(std::uint32_t);
			historyDesc.Usage = Swim::Rhi::BufferUsage::Storage;
			historyDesc.DebugName = "Box field visibility";
			history = context.Device()->CreateBuffer(historyDesc);
			cut = true;
		}

		cut = cut || context.View().Cut;
		visibility = context.Graph().ImportBuffer(*history, Swim::Rhi::ResourceState::ShaderRead);

		if (!colors || rowRanges.size() != Rows)
		{
			if (!context.Device())
			{
				throw std::runtime_error("BoxField needs a device");
			}

			Swim::Render::StreamingBufferDesc desc;
			desc.Capacity = std::uint64_t(Columns) * Rows * 16 * 3; // Room for spacers and moves.
			desc.DebugName = "Box field colours";
			colors = std::make_unique<Swim::Render::StreamingBuffer>(*context.Device(), desc);
			colors->SetDefragmentBudget(std::uint64_t(Columns) * 16 * 4);
			rowRanges.assign(Rows, {});
			spacers.clear();

			for (std::uint32_t row = 0; row < Rows; ++row)
			{
				WriteRow(row);
			}
		}
		else if (now >= nextChurn)
		{
			nextChurn = now + 0.25f;

			if (spacers.size() > 8 || (!spacers.empty() && Random() % 2u == 0u))
			{
				const auto index = Random() % spacers.size();
				colors->Free(spacers[index]);
				spacers.erase(spacers.begin() + static_cast<std::ptrdiff_t>(index));
			}

			if (const auto spacer = colors->Allocate(std::uint64_t(1 + Random() % Columns) * 16))
			{
				spacers.push_back(*spacer);
			}

			WriteRow(Random() % Rows);
		}

		if (!faces)
		{
			Swim::Render::TextureArrayDesc desc;
			desc.Width = 64;
			desc.Height = 64;
			desc.Layers = 4;
			desc.DebugName = "Box field faces";
			faces = std::make_unique<Swim::Render::TextureArray>(*context.Device(), desc);
			faces->SetHotReload(true);

			for (std::uint32_t layer = 0; layer < 3; ++layer)
			{
				PaintLayer(layer, 0);
			}

			if (FaceImage.empty())
			{
				PaintLayer(3, 1);
			}
			else
			{
				faces->LoadLayer(3, FaceImage);
			}
		}
		else if (now >= nextRepaint)
		{
			nextRepaint = now + 2.0f;
			PaintLayer(2, ++repaints);
		}

		const auto colorBuffer = colors->Import(context.Graph(), context.Settings().StreamingUploadBudget);
		faceTexture = faces->Import(context.Graph(), context.Settings().StreamingUploadBudget);
		faceMinLod = faces->ImportMinLod(context.Graph());
		std::vector<std::uint32_t> rowOffsets(Rows, 0);

		for (std::uint32_t row = 0; row < Rows; ++row)
		{
			rowOffsets[row] = colors->IsValid(rowRanges[row]) ? static_cast<std::uint32_t>(colors->GetOffset(rowRanges[row]) / 16) : 0u;
		}

		const auto rowBuffer =
			context.Graph().CreateUpload(std::as_bytes(std::span(rowOffsets)), "Box field rows", Swim::Rhi::BufferUsage::Storage, 16);
		BoxConstants constants;
		constants.Origin = { Origin[0], Origin[1], Origin[2], Spacing };
		constants.Time = { now, previousTime < 0.0f ? now : previousTime, 0.0f, 0.0f };
		constants.BoxCount = boxCount;
		constants.Columns = Columns;
		constants.ViewCount = viewCount;
		constants.Cut = cut ? 1u : 0u;
		previousTime = now;

		views = context.Graph().CreateUpload(std::as_bytes(std::span(cullViews)), "Box field views", Swim::Rhi::BufferUsage::Storage, 16);
		boxes = context.CreateBuffer(std::uint64_t(boxCount) * BoxBytes, "Box field boxes");
		// One slot per view plus the late phase's.
		counts = context.CreateBuffer(std::uint64_t(viewCount + 1) * sizeof(std::uint32_t), "Box field counts");
		arguments = context.CreateBuffer(std::uint64_t(viewCount + 1) * boxCount * ArgumentBytes, "Box field arguments");
		context.Compute("BoxFieldGenerate")
			.WriteBuffer("Boxes", boxes)
			.Buffer("Colors", colorBuffer)
			.Buffer("RowOffsets", rowBuffer)
			.Constants(constants)
			.Dispatch(boxCount, 1);
		context.Compute("BoxFieldReset").WriteBuffer("Counts", counts).Constants(constants).Dispatch(viewCount + 1, 1);
		context.Compute("BoxFieldCull")
			.Buffer("Boxes", boxes)
			.Buffer("Views", views)
			.Buffer("History", visibility)
			.StorageBuffer("Counts", counts)
			.WriteBuffer("Arguments", arguments)
			.Constants(constants)
			.Dispatch(boxCount, viewCount);
		return { { boxes, faceMinLod }, { arguments, counts }, { faceTexture } };
	}

	void BoxField::CullGeometry(Engine::RenderFeatureContext& context, const Engine::RenderFeatureOcclusion& occlusion)
	{
		LateConstants constants;
		constants.ViewProjection = occlusion.ViewProjection;
		constants.HzbWidth = occlusion.Width;
		constants.HzbHeight = occlusion.Height;
		constants.HzbMipCount = occlusion.MipCount;
		constants.BoxCount = boxCount;
		constants.ViewCount = viewCount;
		constants.Cut = cut ? 1u : 0u;
		context.Compute("BoxFieldLateCull")
			.Buffer("Boxes", boxes)
			.Buffer("Views", views)
			.StorageBuffer("History", visibility)
			.StorageBuffer("Counts", counts)
			.StorageBuffer("Arguments", arguments)
			.Texture("Hzb", occlusion.Pyramid)
			.Constants(constants)
			.Dispatch(boxCount, 1);
		cut = false;
	}

	void BoxField::DrawGeometry(Engine::RenderFeatureDrawContext& context)
	{
		using Engine::RenderGeometryPass;
		std::uint32_t slot = 0;
		const char* program = "BoxField";

		switch (context.Pass())
		{
		case RenderGeometryPass::DepthPrepass:
			program = "BoxFieldDepth";
			break;
		case RenderGeometryPass::Shadow:
		{
			const auto view = context.ShadowView();

			if (view >= shadowSlots.size() || shadowSlots[view] < 0)
			{
				return;
			}

			slot = static_cast<std::uint32_t>(shadowSlots[view]);
			program = "BoxFieldShadow";
			break;
		}
		default:
			break;
		}

		auto draw = context.Program(program);
		draw.Buffer("Boxes", boxes);

		if (context.Pass() == RenderGeometryPass::Shadow)
		{
			draw.Constants(ShadowConstants{ context.ShadowView() });
		}
		else if (context.Pass() != RenderGeometryPass::DepthPrepass)
		{
			draw.Texture("Faces", faceTexture).Sampler("FaceSampler", "LinearRepeat").Buffer("FaceMinLod", faceMinLod);
		}

		draw.DrawIndirectCount(arguments, std::uint64_t(slot) * boxCount * ArgumentBytes, counts, slot * sizeof(std::uint32_t), boxCount);

		if (context.Pass() == RenderGeometryPass::Opaque)
		{
			// The late phase: boxes the prepass did not draw, with depth writes.
			Engine::RenderGeometryState late;
			late.SkipsPrepass = true;
			auto lateDraw = context.Program("BoxField", late);
			lateDraw.Buffer("Boxes", boxes).Texture("Faces", faceTexture).Sampler("FaceSampler", "LinearRepeat").Buffer("FaceMinLod", faceMinLod);
			lateDraw.DrawIndirectCount(
				arguments, std::uint64_t(viewCount) * boxCount * ArgumentBytes, counts, viewCount * sizeof(std::uint32_t), boxCount);
		}
	}

} // namespace Game
