#pragma once
#include "Engine/Systems/Renderer/UiRendering/UiRenderer.h"

#include <memory>
#include <string>
#include <vector>

namespace Swim::Render
{
	struct UiRenderSurfaceHandle
	{
		std::uint32_t Index = 0;
		std::uint32_t Generation = 0; // 0 is never valid.

		explicit operator bool() const { return Generation != 0; }

		bool operator==(const UiRenderSurfaceHandle&) const = default;
	};

	struct UiRenderSurfaceDesc
	{
		std::uint32_t Width = 512; // Pixels of mip 0 (the document's Layout size x its DPI scale).
		std::uint32_t Height = 512;
		// Linear storage with hardware sRGB encoding: sampled back as linear premultiplied
		// color, as UI images and materials expect. RGBA16Float for HDR panels.
		Rhi::Format Format = Rhi::Format::RGBA8UnormSrgb;
		// 0: the full chain. Every level is drawn from the document (re-rasterized, so text
		// stays sharp when the surface is seen far away), not downsampled.
		std::uint32_t MipLevels = 0;
		std::string DebugName = "UI surface";
	};

	// What a document shows on a surface this frame.
	struct UiSurfaceContent
	{
		std::span<const UI::UiPaintQuad> Paint;
		// UiDocument::GetPaintRevision; the surface is drawn again only when it changes (or
		// with Force), otherwise the texture keeps last frame's image at no cost.
		std::uint64_t PaintRevision = 0;
		bool Force = false;
		float DpiScale = 1.0f; // Of mip 0.
		UiCompositionSettings Composition{ UiOutputEncoding::Linear };
		const UiAtlasFrame* Atlas = nullptr;
		std::span<const GraphTexture> Images;
		std::array<float, 4> ClearColor{}; // Premultiplied; transparent by default.
	};

	struct UiSurfaceFrame
	{
		GraphTexture Texture;			// Imported; exported ShaderRead (sample it after this frame's draws).
		std::uint32_t TextureIndex = 0; // Bindless element of every mip.
		std::uint32_t SamplerIndex = 0; // Trilinear, clamp-to-edge.
		std::uint32_t MipLevels = 0;
		bool Drawn = false; // Draw passes were recorded this frame.
	};

	struct UiRenderSurfacesStats
	{
		std::uint32_t Surfaces = 0;
		std::uint32_t Retiring = 0;
		std::uint64_t DrawnSurfaces = 0; // Committed, over the object's lifetime.
		std::uint64_t SkippedSurfaces = 0;
	};

	// Render-surface canvases (critical-path item 79): documents drawn into their own
	// persistent textures, registered in the shared bindless table so any material, mesh or
	// world panel samples them. A surface is drawn only when its paint changed, into every
	// mip level. After executing the graph call CommitFrame (AbortFrame redraws next frame).
	// Release retires a surface after its last use; Collect frees completed ones. Owner
	// thread, externally synchronized.
	class UiRenderSurfaces
	{
	  public:
		// Throws std::runtime_error when the sampler cannot be created.
		UiRenderSurfaces(Rhi::Device& device, BindlessResourceTable& bindless, std::string debugName = "UI surfaces");
		~UiRenderSurfaces();
		UiRenderSurfaces(const UiRenderSurfaces&) = delete;
		UiRenderSurfaces& operator=(const UiRenderSurfaces&) = delete;

		// Throws std::invalid_argument for an empty/oversized extent, a depth or unknown format
		// or too many mips, std::length_error when the bindless table is full and
		// std::runtime_error when the texture cannot be created.
		UiRenderSurfaceHandle Create(const UiRenderSurfaceDesc& desc);
		bool IsValid(UiRenderSurfaceHandle surface) const;
		bool Release(UiRenderSurfaceHandle surface, Rhi::TimelinePoint lastUse = {});
		std::size_t Collect();
		std::size_t Drain();

		// Imports the surface and, when its content changed, records one UiRenderer pass per
		// mip (clearing to ClearColor). Throws std::invalid_argument for an invalid handle and
		// std::logic_error for a second Record of the same surface before CommitFrame.
		UiSurfaceFrame Record(RenderGraph& graph, UiRenderSurfaceHandle surface, UiRenderer& renderer, const UiRenderProgram& program,
			Rhi::DescriptorTable& bindlessTable, const UiSurfaceContent& content);
		void CommitFrame();
		void AbortFrame();

		UiRenderSurfacesStats GetStats() const;

		// One image quad showing a surface over a canvasSize rectangle: the paint list of a
		// world panel (UiRenderFrame with ClipFromCanvas) that displays the surface.
		static std::vector<UI::UiPaintQuad> PanelPaint(const UiSurfaceFrame& frame, UI::UiPoint canvasSize);

	  private:
		struct Surface
		{
			UiRenderSurfaceDesc Desc;
			std::uint32_t Generation = 0;
			std::uint32_t Mips = 1;
			std::unique_ptr<Rhi::Texture> Texture;
			std::unique_ptr<Rhi::TextureView> View;
			BindlessTextureHandle Handle;
			bool Initialized = false;			  // Drawn and committed at least once.
			std::uint64_t Revision = 0;			  // Committed paint revision.
			std::optional<std::uint64_t> Pending; // Drawn this frame, awaiting CommitFrame.
			bool Recorded = false;
		};

		struct Retired
		{
			std::unique_ptr<Rhi::Texture> Texture;
			std::unique_ptr<Rhi::TextureView> View;
			Rhi::TimelinePoint LastUse;
		};

		Surface* Find(UiRenderSurfaceHandle handle);
		const Surface* Find(UiRenderSurfaceHandle handle) const;

		Rhi::Device& device;
		BindlessResourceTable& bindless;
		std::string debugName;
		std::unique_ptr<Rhi::Sampler> sampler;
		BindlessSamplerHandle samplerHandle;
		std::vector<Surface> surfaces; // Index = handle index; Generation 0 when free.
		std::vector<Retired> retired;
		std::uint64_t drawn = 0;
		std::uint64_t skipped = 0;
		std::uint64_t pendingDrawn = 0;
		std::uint64_t pendingSkipped = 0;
	};
} // namespace Swim::Render
