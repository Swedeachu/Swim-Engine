#pragma once
#include "Engine/Systems/Renderer/RenderGraph/RenderGraph.h"
#include "Engine/Systems/Renderer/UiRendering/UiAtlasTextures.h"
#include "Engine/Systems/Renderer/UiRendering/UiRenderBindings.h"
#include "Engine/Systems/Renderer/UiRendering/UiRenderReference.h"

#include <array>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace Swim::Render
{
	// SwimUiQuad built by UiRenderer::PipelineDesc for the target's format; its layout's
	// space 1 is the shared bindless space. DepthPipeline (PipelineDesc with D32Float) is
	// needed only by frames with Depth.
	struct UiRenderProgram
	{
		Rhi::GraphicsPipeline* Pipeline = nullptr;
		Rhi::PipelineLayout* Layout = nullptr;
		Rhi::GraphicsPipeline* DepthPipeline = nullptr;
	};

	struct UiRendererDesc
	{
		std::uint32_t MaxQuads = 1u << 18; // Per Record.
		std::string DebugName = "UI";
	};

	struct UiRenderFrame
	{
		std::span<const UI::UiPaintQuad> Paint; // UiDocument::Paint output (logical units).
		GraphTexture Target;					// ColorAttachment; loaded and blended into unless Clear.
		float DpiScale = 1.0f;					// The document's Layout DPI scale.
		float OffsetX = 0.0f;					// Framebuffer pixels (split screen, viewports).
		float OffsetY = 0.0f;
		// World panels, billboards and surfaces on quads: canvas pixels -> clip
		// (UI::ClipFromCanvas). Quads stay in canvas pixels (offsets must be 0); coverage and
		// glyph ranges come from screen-space derivatives. Empty: a screen overlay.
		std::optional<std::array<float, 16>> ClipFromCanvas;
		// D32Float reverse-Z scene depth, tested (GreaterEqual) and never written; needs
		// ClipFromCanvas and UiRenderProgram::DepthPipeline.
		std::optional<GraphTexture> Depth;
		float Opacity = 1.0f;		 // Canvas fade (UI::CanvasFade), [0, 1].
		std::uint32_t TargetMip = 0; // The mip level drawn (render-surface chains); its extent is the target's >> mip.
		UiCompositionSettings Composition;
		const UiAtlasFrame* Atlas = nullptr;  // This frame's UiAtlasTextures::Update (glyph pages).
		std::span<const GraphTexture> Images; // Imported textures behind UiImage handles, declared as sampled reads.
		bool Clear = false;					  // Clear the target first (overlay-only targets).
		std::array<float, 4> ClearColor{};
	};

	struct UiRenderStats
	{
		std::uint32_t Quads = 0; // Instances drawn.
		std::uint32_t Solids = 0;
		std::uint32_t Glyphs = 0;
		std::uint32_t Images = 0;
		std::uint32_t Culled = 0;
	};

	// Retained UI and text rendering (critical-path item 79): one graphics pass that draws
	// a document's paint list as a single instanced draw of GpuUiQuads in paint order.
	// Glyph pages and images are sampled through the bindless table, so quads of any
	// texture share the draw; clipping happens per instance in the vertex stage, so no
	// scissor changes or batch splits are needed. Colors are encoded for the target
	// (UiOutputEncoding) and blended as premultiplied alpha. UiRenderReference.h is the
	// CPU definition of every rule.
	class UiRenderer
	{
	  public:
		// Premultiplied One / OneMinusSourceAlpha on color and alpha, no culling. With a depth
		// format (D32Float): the canonical reverse-Z test (GreaterEqual), no depth writes.
		static Rhi::GraphicsPipelineDesc PipelineDesc(Rhi::Format colorFormat, Rhi::ShaderProgram& program, Rhi::PipelineLayout& layout,
			Rhi::Format depthFormat = Rhi::Format::Undefined);

		explicit UiRenderer(UiRendererDesc desc = {});

		// Nothing is recorded without visible quads unless Clear is set. Throws
		// std::invalid_argument for a missing pipeline, a target that is not a 2D color
		// attachment, a mip beyond the target's, invalid composition settings, opacity or
		// canvas matrix, Srgb encoding into an *Srgb format (double encoding), offsets with a
		// canvas matrix, or a depth target without a canvas matrix, depth pipeline or the
		// drawn extent, and std::length_error above MaxQuads.
		std::optional<GraphPass> Record(
			RenderGraph& graph, const UiRenderFrame& frame, const UiRenderProgram& program, Rhi::DescriptorTable& bindless);

		// The instances of the last Record, for diagnostics and reference comparisons.
		const std::vector<GpuUiQuad>& GetLastQuads() const { return lastQuads; }

		const GpuUiDrawConstants& GetLastConstants() const { return lastConstants; }

		const UiRenderStats& GetStats() const { return stats; }

	  private:
		UiRendererDesc desc;
		std::vector<GpuUiQuad> lastQuads;
		GpuUiDrawConstants lastConstants;
		UiRenderStats stats;
	};
} // namespace Swim::Render
