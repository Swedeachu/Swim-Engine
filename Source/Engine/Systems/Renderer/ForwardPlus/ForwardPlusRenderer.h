#pragma once
#include "Engine/Systems/Renderer/ClusteredLighting/ClusterGraphResources.h"
#include "Engine/Systems/Renderer/Environment/EnvironmentGraphResources.h"
#include "Engine/Systems/Renderer/ForwardPlus/ForwardPlusBindings.h"
#include "Engine/Systems/Renderer/ForwardPlus/ForwardPlusGraphResources.h"
#include "Engine/Systems/Renderer/ForwardPlus/ForwardPlusReference.h"
#include "Engine/Systems/Renderer/Geometry/GeometryGraphResources.h"
#include "Engine/Systems/Renderer/GpuMaterials/GpuMaterialGraphResources.h"
#include "Engine/Systems/Renderer/GpuScene/GpuSceneGraphResources.h"
#include "Engine/Systems/Renderer/Lights/GpuLightGraphResources.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraph.h"
#include "Engine/Systems/Renderer/Visibility/VisibilityDraws.h"
#include "Engine/Systems/Renderer/Visibility/VisibilityGraphResources.h"

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Swim::Render
{
	class GpuVisibility;

	// One compiled Forward+ variant (ClusteredForward.slang as SwimForwardOpaque or
	// SwimForwardTransparent) and a pipeline built from ForwardPlusRenderer::PipelineDesc.
	struct ForwardPlusProgram
	{
		Rhi::GraphicsPipeline* Pipeline = nullptr;
		Rhi::PipelineLayout* Layout = nullptr;
	};

	struct ForwardPlusRendererDesc
	{
		ForwardPlusProgram Opaque;
		ForwardPlusProgram Transparent;
		Rhi::ComputePipeline* SortPipeline = nullptr; // ForwardTransparentSort.slang.
		Rhi::PipelineLayout* SortLayout = nullptr;
		// IndirectCount needs GraphicsCapabilities::IndirectCount; ZeroFilledIndirect
		// needs VisibilityFrameDesc::ZeroUnusedCommands (see VisibilityDraws.h).
		VisibilityDrawPath DrawPath = VisibilityDrawPath::IndirectCount;
		std::string DebugName = "Forward+";
	};

	// A GeometryHeap page pair one index-page slot draws from: every mesh visibility
	// bins into slot s must keep its indices (32-bit) in PageSlots[s].IndexPage and its
	// StandardVertex vertices in PageSlots[s].VertexPage.
	struct ForwardPlusPageSlot
	{
		std::uint32_t IndexPage = 0;
		std::uint32_t VertexPage = 0;
	};

	// Everything one view's Forward+ frame reads, as recorded earlier in the same graph.
	struct ForwardPlusFrame
	{
		const GpuSceneGraphResources* Scene = nullptr;
		const GeometryGraphResources* Geometry = nullptr;
		const VisibilityGraphResources* Visibility = nullptr; // Single or late phase, with ForwardPlusBinCount material bins.
		std::vector<ForwardPlusPageSlot> PageSlots;			  // Same order as VisibilityFrameDesc::IndexPages.
		const GpuMaterialGraphResources* Materials = nullptr;
		Rhi::DescriptorTable* Bindless = nullptr; // BindlessResourceTable::GetTable() (ForwardPlusBindlessSpace).
		const GpuLightGraphResources* Lights = nullptr;
		const ClusterGraphResources* Clusters = nullptr; // Built for this view and viewport.
		// Optional image-based lighting; BrdfLut is required with it. Without it the
		// renderer binds 1x1 zero stand-ins and clears ForwardViewFlagEnvironment.
		const EnvironmentGraphResources* Environment = nullptr;
		std::optional<GraphTexture> BrdfLut;
		ForwardPlusView View;
	};

	// Viewport-sized render targets (the cluster grid's viewport).
	struct ForwardPlusTargets
	{
		GraphTexture Color; // RGBA16Float, ColorAttachment: linear HDR radiance.
		GraphTexture
			ObjectId;		// R32Float, ColorAttachment: GpuInstanceRecord::ObjectId + 1 of opaque pixels (exact below 2^24), 0 elsewhere.
		GraphTexture Depth; // D32Float (reverse-Z), DepthStencilAttachment.
		bool Clear = true;	// Clear color/id/depth first; otherwise load them.
		std::array<float, 4> ClearColor{ 0, 0, 0, 0 };
	};

	// Clustered Forward+ (critical-path items 66 and 67). Per view, Record schedules:
	//  1. opaque: every Opaque-bin draw of every page slot, GPU-driven from the
	//     visibility commands, shaded by ClusteredForward.slang (StandardPbr resolve,
	//     directional + clustered local lights, ambient, IBL, emission), writing
	//     color, object id and depth;
	//  2. sort: one group per page slot orders the Transparent bin back to front by
	//     bounds-center depth (deterministic tie-breaks) into compacted commands;
	//  3. transparent: the sorted draws, blended (premultiplied alpha) over the
	//     opaque result, depth-tested without depth writes.
	// No per-object CPU light or draw list exists anywhere in the frame.
	class ForwardPlusRenderer
	{
	  public:
		static constexpr Rhi::Format ColorFormat = Rhi::Format::RGBA16Float;
		// Float so it can be cleared (the RHI clears only float and normalized targets).
		static constexpr Rhi::Format ObjectIdFormat = Rhi::Format::R32Float;

		// Pipeline state of a variant: both rasterize both faces (single-sided
		// materials discard back faces in the shader, so mirrored transforms work),
		// test depth with the canonical reverse-Z compare, and use the RHI's +Y-up
		// counter-clockwise front faces. Opaque writes depth and two targets (color,
		// object id); Transparent writes color only, premultiplied One /
		// OneMinusSourceAlpha, and never writes depth.
		static Rhi::GraphicsPipelineDesc PipelineDesc(ForwardPlusBin bin, Rhi::ShaderProgram& program, Rhi::PipelineLayout& layout);

		// Draw capacities for GpuVisibilityDesc::MaterialBinCapacities.
		static std::vector<std::uint32_t> VisibilityBinCapacities(std::uint32_t opaque, std::uint32_t transparent);
		// Routes a material set to its bin (ForwardPlus::MaterialBin).
		static void RouteMaterial(GpuVisibility& visibility, std::uint32_t materialSet, const StandardPbr::Parameters& parameters);

		// Throws std::invalid_argument when a program is missing. Creates the linear
		// clamp sampler the environment lookups use.
		ForwardPlusRenderer(Rhi::Device& device, ForwardPlusRendererDesc desc);
		~ForwardPlusRenderer();
		ForwardPlusRenderer(const ForwardPlusRenderer&) = delete;
		ForwardPlusRenderer& operator=(const ForwardPlusRenderer&) = delete;

		// Throws std::invalid_argument for missing inputs, targets that are not
		// viewport-sized with the formats above, visibility bins that do not match
		// ForwardPlusBinCount x PageSlots, a transparent capacity above
		// ForwardTransparentSortBindings::MaxDraws, or an invalid view.
		ForwardPlusGraphResources Record(RenderGraph& graph, const ForwardPlusFrame& frame, const ForwardPlusTargets& targets) const;

		Rhi::Sampler& GetEnvironmentSampler() const { return *environmentSampler; }

	  private:
		ForwardPlusRendererDesc desc;
		std::unique_ptr<Rhi::Sampler> environmentSampler;
	};
} // namespace Swim::Render
