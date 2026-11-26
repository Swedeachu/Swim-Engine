#pragma once

#include "Engine/Systems/Renderer/Runtime/RenderFeature.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Engine
{

	// What a feature asks IndirectCuller to cull: one world-space box and one draw command per
	// item, in graph buffers of this frame (uploads or the output of the feature's compute).
	struct IndirectCullItems
	{
		Swim::Render::GraphBuffer Bounds;	// float4 min, float4 max (xyz) per item.
		Swim::Render::GraphBuffer Commands; // Swim::Rhi::DrawIndirectCommand per item, copied as is.
		std::uint32_t Count = 0;
		bool ShadowViews = true; // Also cull into a list per rendered shadow tile.
	};

	// One compacted list of draw commands; Draw records it with DrawIndirectCount.
	struct IndirectCullList
	{
		Swim::Render::GraphBuffer Arguments;
		std::uint64_t Offset = 0;
		Swim::Render::GraphBuffer Counts;
		std::uint64_t CountOffset = 0;
		std::uint32_t MaxCount = 0;

		void Draw(RenderFeatureDraw& draw) const;
	};

	// Two-phase GPU culling of a feature's own draw list against the camera (frustum and the
	// main view's HZB) and every shadow tile (frustum), into DrawIndirectCount lists:
	//  - Early (PrepareGeometry): the camera list holds the items visible last frame that are
	//    in the frustum (every item in the frustum after a camera cut or a resize), and each
	//    rendered shadow tile gets the items in its frustum;
	//  - Late (CullGeometry, with UsesOcclusion() true): the camera's items are tested against
	//    the HZB built from this frame's prepass; the visible ones the early list skipped go to
	//    the late list, and every item's visibility is kept for the next frame.
	// Draw List(context) in the depth prepass, the opaque pass (prepassed), the shadow pass and
	// any other pass; draw LateList() in the opaque pass with RenderGeometryState::SkipsPrepass
	// (and in a transparent pass, after List). Program: IndirectCullReset/Early/Late.
	//
	//   PrepareGeometry: culler.Early(context, { bounds, commands, count });
	//                    RenderGeometryResources resources{ { vertices } };
	//                    culler.Declare(resources);
	//   CullGeometry:    culler.Late(context, occlusion);
	//   DrawGeometry:    if (const auto list = culler.List(context)) list->Draw(context.Program(...));
	//                    if (context.Pass() == RenderGeometryPass::Opaque) culler.LateList().Draw(context.Program(..., late));
	class IndirectCuller
	{

	  public:

		explicit IndirectCuller(std::string debugName = "Indirect cull");

		~IndirectCuller();

		IndirectCuller(const IndirectCuller&) = delete;

		IndirectCuller& operator=(const IndirectCuller&) = delete;

		void Early(RenderFeatureContext& context, const IndirectCullItems& items);

		void Late(RenderFeatureContext& context, const RenderFeatureOcclusion& occlusion);

		// Adds the argument and count buffers to the feature's indirect buffers.
		void Declare(RenderGeometryResources& resources) const;

		// This pass's list: the camera's early list, or in the Shadow pass the tile's (none
		// when the tile was not culled this frame).
		std::optional<IndirectCullList> List(const RenderFeatureDrawContext& context) const;

		// The camera items the HZB test found visible that the early list did not draw.
		IndirectCullList LateList() const;

		// Index of a shadow tile's list (1..), -1 when it was not culled this frame.
		int ShadowSlot(std::uint32_t shadowView) const;

		std::uint32_t ViewCount() const { return viewCount; }

		// Off: no HZB test and no history (every item in the camera frustum is drawn early).
		bool Occlusion = true;

	  private:

		IndirectCullList Slot(std::uint32_t slot) const;

		std::string name;
		std::unique_ptr<Swim::Rhi::Buffer> history; // Per item: visible last frame.
		// Replaced history buffers, kept until frames that may still use them have finished
		// (frames left before release).
		std::vector<std::pair<std::unique_ptr<Swim::Rhi::Buffer>, std::uint32_t>> retired;
		Swim::Render::GraphBuffer visibility{};
		Swim::Render::GraphBuffer bounds{};
		Swim::Render::GraphBuffer commands{};
		Swim::Render::GraphBuffer views{};
		Swim::Render::GraphBuffer arguments{};
		Swim::Render::GraphBuffer counts{};
		std::vector<int> shadowSlots;
		std::uint32_t count = 0;
		std::uint32_t viewCount = 0;
		std::array<std::uint32_t, 2> viewport{};
		bool cut = true;
		bool lateRan = true;

	};

} // namespace Engine
