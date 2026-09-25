#pragma once
#include "Engine/Systems/Renderer/RenderGraph/RenderGraph.h"
#include "Engine/Systems/Renderer/Resources/BindlessResourceTable.h"
#include "Engine/Systems/Text/GlyphAtlas.h"

#include <memory>
#include <string>
#include <vector>

namespace Swim::Render
{
	struct UiAtlasTexturesDesc
	{
		std::uint32_t MaxPages = 256;
		std::string DebugName = "UI glyph atlas";
	};

	// What one UiAtlasTextures::Update scheduled. Every page is imported into the graph
	// and exported ShaderRead; the UI pass declares Pages as sampled reads so it runs
	// after this frame's uploads.
	struct UiAtlasFrame
	{
		std::vector<GraphTexture> Pages;
		std::vector<std::uint32_t> TextureIndices; // Bindless element per page.
		std::uint32_t SamplerIndex = 0;			   // Linear, clamp-to-edge, no mips.
		std::uint32_t PageSize = 0;
		std::uint32_t UploadedPages = 0; // Pages with an upload recorded this frame.
		std::uint32_t UploadedRows = 0;
		std::uint64_t UploadedBytes = 0;
	};

	struct UiAtlasTexturesStats
	{
		std::uint32_t Pages = 0;
		std::uint32_t RetiringPages = 0;
		std::uint64_t UploadedBytes = 0; // Committed, over the object's lifetime.
	};

	// GPU residency of one Text::GlyphAtlas (critical-path item 79). One RGBA8 UNORM
	// texture per atlas page (MSDF distances are linear data, never sRGB; alpha is 1),
	// registered in the shared BindlessResourceTable. A page is uploaded whole the first
	// time and afterwards only the band of rows GlyphAtlas::GetChangedRows reports since
	// the last committed upload, all as graph-scheduled transfers.
	//
	// After executing the graph, call CommitFrame; AbortFrame when it was dropped (the
	// uploads are recorded again next frame). Release retires every texture, view and
	// bindless element after the given timeline point (for example when the atlas is
	// replaced); Collect frees the completed ones. Owner thread, externally synchronized.
	// Destroy only after the GPU finished every submission that sampled the pages.
	class UiAtlasTextures
	{
	  public:
		// Throws std::runtime_error when the sampler cannot be created and
		// std::invalid_argument for an invalid desc.
		UiAtlasTextures(Rhi::Device& device, BindlessResourceTable& bindless, UiAtlasTexturesDesc desc = {});
		~UiAtlasTextures();
		UiAtlasTextures(const UiAtlasTextures&) = delete;
		UiAtlasTextures& operator=(const UiAtlasTextures&) = delete;

		// Throws std::logic_error while a frame awaits CommitFrame/AbortFrame or when
		// `atlas` is not the attached one (Release first), std::length_error beyond
		// MaxPages or a full bindless table, std::runtime_error on texture creation failure.
		UiAtlasFrame Update(RenderGraph& graph, const Text::GlyphAtlas& atlas);
		void CommitFrame();
		void AbortFrame();

		// Detaches the atlas. Throws std::logic_error while a frame is pending.
		void Release(Rhi::TimelinePoint lastUse);
		std::size_t Collect();
		std::size_t Drain(); // Waits for every retiring page.

		std::uint32_t GetSamplerIndex() const;
		UiAtlasTexturesStats GetStats() const;

	  private:
		struct Page
		{
			std::unique_ptr<Rhi::Texture> Texture;
			std::unique_ptr<Rhi::TextureView> View;
			BindlessTextureHandle Handle;
			std::uint64_t Revision = 0; // Committed on the GPU.
			bool Initialized = false;
			std::uint64_t PendingRevision = 0;
			bool Pending = false;
		};

		struct Retired
		{
			std::unique_ptr<Rhi::Texture> Texture;
			std::unique_ptr<Rhi::TextureView> View;
			Rhi::TimelinePoint LastUse;
		};

		Page CreatePage(std::uint32_t index, std::uint32_t size);

		Rhi::Device& device;
		BindlessResourceTable& bindless;
		UiAtlasTexturesDesc desc;
		std::unique_ptr<Rhi::Sampler> sampler;
		BindlessSamplerHandle samplerHandle;
		const Text::GlyphAtlas* attached = nullptr;
		std::vector<Page> pages;
		std::vector<Retired> retired;
		std::uint64_t uploadedBytes = 0;
		std::uint64_t pendingBytes = 0;
		bool pending = false;
	};
} // namespace Swim::Render
