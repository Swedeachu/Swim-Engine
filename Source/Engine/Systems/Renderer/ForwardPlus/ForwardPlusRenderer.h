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
#include "Engine/Systems/Renderer/Shadows/ShadowGraphResources.h"
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
		// Optional depth prepass (both or neither): DepthPrepass is SwimForwardDepth with
		// DepthPrepassPipelineDesc, OpaquePrepassed is SwimForwardOpaquePrepassed with
		// PrepassedPipelineDesc. With them the opaque bin is drawn twice: depth only, then
		// shaded with early depth tests and no depth writes, so every pixel runs the
		// lighting loop once instead of once per overlapping surface. Opaque is unused then.
		ForwardPlusProgram DepthPrepass;
		ForwardPlusProgram OpaquePrepassed;
		// Optional back-face depth (SwimForwardBackDepth with DepthPrepassPipelineDesc): with
		// it and ForwardPlusTargets::BackDepth, the Opaque bin is drawn once more keeping only
		// back faces, so screen-space effects know how thick each visible surface is.
		ForwardPlusProgram BackDepth;
		// Optional deferred local lights (all or none, with the depth prepass):
		// OpaqueDeferred is SwimForwardOpaqueDeferred with DeferredPipelineDesc, the compute
		// program ForwardLocalLights.slang. See ForwardPlusFrame::DeferLocalLights.
		ForwardPlusProgram OpaqueDeferred;
		Rhi::ComputePipeline* LocalLightsPipeline = nullptr;
		Rhi::PipelineLayout* LocalLightsLayout = nullptr;
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
		// renderer binds 1x1 zero stand-ins and clears ForwardViewFlagEnvironment. A
		// BrdfLut alone (item 76) is still bound and sets ForwardViewFlagBrdfLut, so the
		// specular reflectance target (and screen-space reflections) work without IBL.
		const EnvironmentGraphResources* Environment = nullptr;
		std::optional<GraphTexture> BrdfLut;
		// Optional shadows (ShadowRenderer::Record earlier in the graph). Without them the
		// renderer binds a 1x1 atlas and one empty record and clears ForwardViewFlagShadows.
		const ShadowGraphResources* Shadows = nullptr;
		ForwardPlusView View;
		// The transparent sort and pass (off: only the opaque result; profiling).
		bool Transparent = true;
		// Shade the clustered local lights in a compute pass after the opaque pass instead of
		// in its fragment stage (needs the renderer's deferred programs and a Color target with
		// Storage usage; otherwise ignored). Same result; transparent draws keep the loop.
		bool DeferLocalLights = false;
		// Pass-name prefix for this frame (empty: the renderer's DebugName).
		std::string DebugName;
	};

	// Viewport-sized render targets (the cluster grid's viewport).
	struct ForwardPlusTargets
	{
		GraphTexture Color; // RGBA16Float, ColorAttachment: linear HDR radiance.
		GraphTexture
			ObjectId;		// R32Float, ColorAttachment: GpuInstanceRecord::ObjectId + 1 of opaque pixels (exact below 2^24), 0 elsewhere.
		GraphTexture Depth; // D32Float (reverse-Z), DepthStencilAttachment.
		// Item 75: RG16Float, ColorAttachment: opaque pixels' motion (current minus previous
		// UV, ForwardPlus::MotionVector), 0 elsewhere. Without one, a transient target is used.
		std::optional<GraphTexture> Velocity;
		// Item 76, for screen-space effects; transient targets stand in when absent:
		//  - Normal (RGBA16Float, ColorAttachment): opaque pixels' world-space shading
		//    normal (xyz, after normal mapping and back-face flips) and perceptual
		//    roughness (w); 0 elsewhere;
		//  - Indirect (RGBA16Float, ColorAttachment): opaque pixels' ambient + IBL
		//    radiance (rgb, ForwardPlus::IndirectRadiance), scaled by the transmittance of
		//    the transparent layers blended over them; 0 elsewhere. Ambient occlusion
		//    removes a fraction of exactly this part of Color.
		std::optional<GraphTexture> Normal;
		std::optional<GraphTexture> Indirect;
		// Item 76, for screen-space reflections; transient targets stand in when absent,
		// both scaled by the transmittance of the transparent layers like Indirect:
		//  - Reflectance (RGBA16Float, ColorAttachment): opaque pixels' split-sum
		//    specular reflectance (rgb, ForwardPlus::SpecularEnvironment), the weight a
		//    reflected radiance is multiplied by; 0 elsewhere;
		//  - Specular (RGBA16Float, ColorAttachment): opaque pixels' specular IBL radiance
		//    (rgb), the part of Indirect a screen-space reflection replaces; 0 elsewhere.
		std::optional<GraphTexture> Reflectance;
		std::optional<GraphTexture> Specular;
		// Optional (needs ForwardPlusRendererDesc::BackDepth): D32Float, DepthStencilAttachment:
		// the reverse-Z depth of the nearest opaque back face per pixel (0 where none), cleared
		// and written by a pass after the opaque one. Screen-space reflections take each
		// surface's thickness from it (ScreenSpace::SurfaceThickness).
		std::optional<GraphTexture> BackDepth;
		bool Clear = true; // Clear color/id/depth first; otherwise load them.
		// With Clear off: still clear every target but Color and Depth (the sky pass before
		// the frame writes only those, instead of storing zeros to six more targets per pixel).
		bool ClearAuxiliary = false;
		std::array<float, 4> ClearColor{ 0, 0, 0, 0 };
	};

	// Clustered Forward+ (critical-path items 66 and 67). Per view, Record schedules:
	//  0. (optional) depth prepass of the Opaque bin, see ForwardPlusRendererDesc;
	//  1. opaque: every Opaque-bin draw of every page slot, GPU-driven from the
	//     visibility commands, shaded by ClusteredForward.slang (StandardPbr resolve,
	//     directional + clustered local lights, ambient, IBL, emission), writing
	//     color, object id, motion vectors (jittered rasterization, item 75), normal +
	//     roughness, indirect radiance, specular reflectance and specular IBL radiance
	//     (item 76), and depth;
	//  2. sort: one group per page slot orders the Transparent bin back to front by
	//     bounds-center depth (deterministic tie-breaks) into compacted commands;
	//  3. transparent: the sorted draws, blended (premultiplied alpha) over the
	//     opaque result, depth-tested without depth writes; the indirect, reflectance
	//     and specular targets are scaled by each layer's transmittance.
	// No per-object CPU light or draw list exists anywhere in the frame.
	class ForwardPlusRenderer
	{
	  public:
		static constexpr Rhi::Format ColorFormat = Rhi::Format::RGBA16Float;
		// Float so it can be cleared (the RHI clears only float and normalized targets).
		static constexpr Rhi::Format ObjectIdFormat = Rhi::Format::R32Float;
		static constexpr Rhi::Format VelocityFormat = Rhi::Format::RG16Float;
		static constexpr Rhi::Format NormalFormat = Rhi::Format::RGBA16Float;	   // Item 76: world shading normal + roughness.
		static constexpr Rhi::Format IndirectFormat = Rhi::Format::RGBA16Float;	   // Item 76: ambient + IBL radiance.
		static constexpr Rhi::Format ReflectanceFormat = Rhi::Format::RGBA16Float; // Item 76 (SSR): specular reflectance.
		static constexpr Rhi::Format SpecularFormat = Rhi::Format::RGBA16Float;	   // Item 76 (SSR): specular IBL radiance.
		static constexpr Rhi::Format MaterialFormat = Rhi::Format::RGBA16Float;	   // Deferred local lights: base colour, metalness.

		// Pipeline state of a variant: both rasterize both faces (single-sided
		// materials discard back faces in the shader, so mirrored transforms work),
		// test depth with the canonical reverse-Z compare, and use the RHI's +Y-up
		// counter-clockwise front faces. Opaque writes depth and seven targets (color,
		// object id, velocity, normal, indirect, reflectance, specular); Transparent
		// writes color, indirect, reflectance and specular, premultiplied One /
		// OneMinusSourceAlpha, and never writes depth.
		static Rhi::GraphicsPipelineDesc PipelineDesc(ForwardPlusBin bin, Rhi::ShaderProgram& program, Rhi::PipelineLayout& layout);
		// Depth prepass: no color targets, depth test + write (canonical reverse-Z compare).
		static Rhi::GraphicsPipelineDesc DepthPrepassPipelineDesc(Rhi::ShaderProgram& program, Rhi::PipelineLayout& layout);
		// Opaque shading after the prepass: the Opaque state without depth writes (the
		// GreaterEqual compare passes exactly the prepass's nearest surface).
		static Rhi::GraphicsPipelineDesc PrepassedPipelineDesc(Rhi::ShaderProgram& program, Rhi::PipelineLayout& layout);
		// The prepassed state with an eighth target (MaterialFormat) for deferred local lights.
		static Rhi::GraphicsPipelineDesc DeferredPipelineDesc(Rhi::ShaderProgram& program, Rhi::PipelineLayout& layout);

		bool SupportsDeferredLocalLights() const { return desc.OpaqueDeferred.Pipeline != nullptr && desc.LocalLightsPipeline != nullptr; }

		bool UsesDepthPrepass() const { return desc.DepthPrepass.Pipeline != nullptr; }

		bool SupportsBackDepth() const { return desc.BackDepth.Pipeline != nullptr; }

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
