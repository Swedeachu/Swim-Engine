#pragma once
#include "Engine/Systems/Renderer/Geometry/GeometryGraphResources.h"
#include "Engine/Systems/Renderer/GpuMaterials/GpuMaterialGraphResources.h"
#include "Engine/Systems/Renderer/GpuScene/GpuSceneGraphResources.h"
#include "Engine/Systems/Renderer/Materials/StandardPbr.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraph.h"
#include "Engine/Systems/Renderer/Shadows/ShadowBindings.h"
#include "Engine/Systems/Renderer/Shadows/ShadowGraphResources.h"
#include "Engine/Systems/Renderer/Shadows/ShadowPlanner.h"
#include "Engine/Systems/Renderer/Visibility/VisibilityDraws.h"

#include <string>
#include <vector>

namespace Swim::Render
{
	class GpuVisibility;

	// Visibility material bins of a shadow visibility instance.
	enum class ShadowBin : std::uint32_t
	{
		Opaque = 0,	  // Depth only.
		Masked = 1,	  // StandardPbr::FlagAlphaMask: the alpha-tested variant.
		Excluded = 2, // StandardPbr::FlagAlphaBlend: blended surfaces cast no shadow.
	};

	inline constexpr std::uint32_t ShadowBinCount = 3;

	// One compiled ShadowDepth.slang variant and a pipeline from ShadowRenderer::PipelineDesc.
	struct ShadowProgram
	{
		Rhi::GraphicsPipeline* Pipeline = nullptr;
		Rhi::PipelineLayout* Layout = nullptr;
	};

	struct ShadowRendererDesc
	{
		ShadowProgram Opaque; // SwimShadowDepth.
		ShadowProgram Masked; // SwimShadowMasked (SHADOW_ALPHA_TEST=1).
		VisibilityDrawPath DrawPath = VisibilityDrawPath::IndirectCount;
		std::string DebugName = "Shadows";
	};

	// A GeometryHeap page pair an index-page slot draws from (StandardVertex vertices, 32-bit indices).
	struct ShadowPageSlot
	{
		std::uint32_t IndexPage = 0;
		std::uint32_t VertexPage = 0;
	};

	struct ShadowFrame
	{
		const GpuSceneGraphResources* Scene = nullptr;
		const GeometryGraphResources* Geometry = nullptr;
		GpuVisibility* Visibility = nullptr; // A dedicated instance with ShadowBinCount material bins.
		std::vector<ShadowPageSlot> PageSlots;
		const GpuMaterialGraphResources* Materials = nullptr;
		Rhi::DescriptorTable* Bindless = nullptr; // ShadowBindlessSpace; used by the masked variant.
		const ShadowPlan* Plan = nullptr;
		bool ZeroUnusedCommands = false; // VisibilityFrameDesc::ZeroUnusedCommands for the fallback draw path.
	};

	// Renders every tile of a ShadowPlan into one D32Float atlas (Phase 16, items 70-72).
	// For each view it records a GPU caster cull (GpuVisibility with the view's
	// GpuViewFlags::ShadowCasters record), then a single depth pass clears the atlas
	// (reverse-Z far = 0) and draws each view's Opaque and Masked bins GPU-driven into
	// the view's tile. Blended materials land in the Excluded bin and cast nothing.
	class ShadowRenderer
	{
	  public:
		static constexpr Rhi::Format AtlasFormat = Rhi::Format::D32Float;

		// Depth-only pipeline state: no color targets, D32Float, GreaterEqual with
		// writes, both faces rasterized (thin and single-sided casters shadow from
		// either side). Bias is applied when sampling, not while rendering.
		static Rhi::GraphicsPipelineDesc PipelineDesc(Rhi::ShaderProgram& program, Rhi::PipelineLayout& layout);
		static std::vector<std::uint32_t> VisibilityBinCapacities(std::uint32_t opaque, std::uint32_t masked, std::uint32_t excluded = 1);
		static ShadowBin MaterialBin(const StandardPbr::Parameters& parameters);
		static void RouteMaterial(GpuVisibility& visibility, std::uint32_t materialSet, const StandardPbr::Parameters& parameters);

		// Throws std::invalid_argument when a program is missing.
		explicit ShadowRenderer(ShadowRendererDesc desc);

		// Throws std::invalid_argument for missing inputs, a visibility instance without
		// ShadowBinCount material bins over the frame's page slots, or missing pages.
		ShadowGraphResources Record(RenderGraph& graph, const ShadowFrame& frame) const;

	  private:
		ShadowRendererDesc desc;
	};
} // namespace Swim::Render
