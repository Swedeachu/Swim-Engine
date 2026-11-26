#include "Engine/Systems/Renderer/Features/IndirectCuller.h"

#include <algorithm>
#include <span>
#include <stdexcept>
#include <utility>

namespace Engine
{

	namespace
	{
		// More than any frame-in-flight count: a replaced buffer outlives every frame that used it.
		constexpr std::uint32_t RetireFrames = 8;
	} // namespace

	namespace
	{

		struct CullView
		{
			std::array<float, 24> Planes{};
		};

		struct EarlyConstants
		{
			std::uint32_t Count = 0;
			std::uint32_t ViewCount = 0;
			std::uint32_t Cut = 0;
			std::uint32_t Reserved = 0;
		};

		struct LateConstants
		{
			std::array<float, 16> ViewProjection{};
			std::uint32_t HzbWidth = 0;
			std::uint32_t HzbHeight = 0;
			std::uint32_t HzbMipCount = 0;
			std::uint32_t Reserved0 = 0;
			std::uint32_t Count = 0;
			std::uint32_t ViewCount = 0;
			std::uint32_t Cut = 0;
			std::uint32_t Reserved1 = 0;
		};

		constexpr std::uint32_t CommandBytes = sizeof(Swim::Rhi::DrawIndirectCommand);

		// The frustum of a row-major reverse-Z view-projection (clip = M * p): inside where
		// -w <= x, y <= w and 0 <= z <= w.
		CullView FrustumOf(const std::array<float, 16>& m)
		{
			const auto row = [&](int r, int c)
			{
				return m[r * 4 + c];
			};
			CullView view;

			for (int c = 0; c < 4; ++c)
			{
				view.Planes[0 + c] = row(3, c) + row(0, c);
				view.Planes[4 + c] = row(3, c) - row(0, c);
				view.Planes[8 + c] = row(3, c) + row(1, c);
				view.Planes[12 + c] = row(3, c) - row(1, c);
				view.Planes[16 + c] = row(2, c);
				view.Planes[20 + c] = row(3, c) - row(2, c);
			}

			return view;
		}

	} // namespace

	void IndirectCullList::Draw(RenderFeatureDraw& draw) const
	{
		draw.DrawIndirectCount(Arguments, Offset, Counts, CountOffset, MaxCount);
	}

	IndirectCuller::IndirectCuller(std::string debugName) : name(std::move(debugName)) {}

	IndirectCuller::~IndirectCuller() = default;

	void IndirectCuller::Early(RenderFeatureContext& context, const IndirectCullItems& items)
	{
		count = items.Count;
		std::vector<CullView> cullViews{ FrustumOf(context.View().ViewProjection) };
		shadowSlots.clear();

		if (items.ShadowViews)
		{
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
		}

		viewCount = static_cast<std::uint32_t>(cullViews.size());
		std::erase_if(retired,
			[](auto& entry)
			{
				return entry.second-- == 0;
			});
		const std::uint64_t historyBytes = std::uint64_t(std::max(count, 1u)) * sizeof(std::uint32_t);

		if (!history || history->GetDesc().Size != historyBytes)
		{
			if (!context.Device())
			{
				throw std::runtime_error("IndirectCuller needs a device");
			}

			Swim::Rhi::BufferDesc desc;
			desc.Size = historyBytes;
			desc.Usage = Swim::Rhi::BufferUsage::Storage;
			desc.DebugName = name;

			if (history)
			{
				// Earlier frames on other frame slots may still read it.
				retired.emplace_back(std::move(history), RetireFrames);
			}

			history = context.Device()->CreateBuffer(desc);
			cut = true;
		}

		const std::array<std::uint32_t, 2> size{ context.View().Width, context.View().Height };
		cut = cut || context.View().Cut || !lateRan || size != viewport || !Occlusion;
		viewport = size;
		lateRan = false;
		bounds = items.Bounds;
		commands = items.Commands;
		visibility = context.Graph().ImportBuffer(*history, Swim::Rhi::ResourceState::ShaderRead);
		views = context.Graph().CreateUpload(std::as_bytes(std::span(cullViews)), name + " views", Swim::Rhi::BufferUsage::Storage, 16);
		const std::uint64_t listCapacity = std::max(count, 1u);
		counts = context.CreateBuffer(std::uint64_t(viewCount + 1) * sizeof(std::uint32_t), name + " counts");
		arguments = context.CreateBuffer(std::uint64_t(viewCount + 1) * listCapacity * CommandBytes, name + " arguments");
		EarlyConstants constants;
		constants.Count = count;
		constants.ViewCount = viewCount;
		constants.Cut = cut ? 1u : 0u;
		context.Compute("IndirectCullReset").WriteBuffer("Counts", counts).Constants(constants).Dispatch(viewCount + 1, 1);

		if (count > 0)
		{
			context.Compute("IndirectCullEarly")
				.Buffer("Bounds", bounds)
				.Buffer("Commands", commands)
				.Buffer("Views", views)
				.Buffer("History", visibility)
				.StorageBuffer("Counts", counts)
				.WriteBuffer("Arguments", arguments)
				.Constants(constants)
				.Dispatch(count, viewCount);
		}
	}

	void IndirectCuller::Late(RenderFeatureContext& context, const RenderFeatureOcclusion& occlusion)
	{
		if (viewCount == 0)
		{
			return;
		}

		lateRan = true;

		if (count > 0)
		{
			LateConstants constants;
			constants.ViewProjection = occlusion.ViewProjection;
			constants.HzbWidth = occlusion.Width;
			constants.HzbHeight = occlusion.Height;
			constants.HzbMipCount = Occlusion ? occlusion.MipCount : 0u;
			constants.Count = count;
			constants.ViewCount = viewCount;
			constants.Cut = cut ? 1u : 0u;
			context.Compute("IndirectCullLate")
				.Buffer("Bounds", bounds)
				.Buffer("Commands", commands)
				.Buffer("Views", views)
				.StorageBuffer("History", visibility)
				.StorageBuffer("Counts", counts)
				.StorageBuffer("Arguments", arguments)
				.Texture("Hzb", occlusion.Pyramid)
				.Constants(constants)
				.Dispatch(count, 1);
		}

		cut = false;
	}

	void IndirectCuller::Declare(RenderGeometryResources& resources) const
	{
		resources.IndirectBuffers.push_back(arguments);
		resources.IndirectBuffers.push_back(counts);
	}

	IndirectCullList IndirectCuller::Slot(std::uint32_t slot) const
	{
		const std::uint64_t capacity = std::max(count, 1u);
		return { arguments, std::uint64_t(slot) * capacity * CommandBytes, counts, std::uint64_t(slot) * sizeof(std::uint32_t), count };
	}

	std::optional<IndirectCullList> IndirectCuller::List(const RenderFeatureDrawContext& context) const
	{
		if (context.Pass() != RenderGeometryPass::Shadow)
		{
			return Slot(0);
		}

		const int slot = ShadowSlot(context.ShadowView());

		if (slot < 0)
		{
			return std::nullopt;
		}

		return Slot(static_cast<std::uint32_t>(slot));
	}

	IndirectCullList IndirectCuller::LateList() const
	{
		return Slot(viewCount);
	}

	int IndirectCuller::ShadowSlot(std::uint32_t shadowView) const
	{
		return shadowView < shadowSlots.size() ? shadowSlots[shadowView] : -1;
	}

} // namespace Engine
