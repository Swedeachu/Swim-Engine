#pragma once
#include "Engine/Assets/TextureAsset.h"
#include "Engine/Systems/Renderer/Residency/Internal/TextureRecord.h"
#include "Engine/Systems/Renderer/Residency/TextureGraphResources.h"
#include "Engine/Systems/Renderer/Residency/TextureResidencyDesc.h"
#include "Engine/Systems/Renderer/Residency/TextureResidencyStats.h"
#include "Engine/Systems/Renderer/Resources/GpuResourceRegistry.h"

#include <optional>

namespace Swim::Render
{
	class RenderGraph;

	// A payload variant of a TextureAsset this residency can upload directly.
	struct TexturePayloadSelection
	{
		std::size_t Payload = 0;
		Rhi::Format Format = Rhi::Format::Undefined;
	};

	// GPU residency for compiled TextureAssets, mirroring GeometryHeap:
	//
	//   CreateTexture  create the RHI texture + view, copy mip bytes (PendingUpload)
	//   Import(graph)  record one staged transfer pass per pending texture
	//   CommitUploads  after the graph's successful Execute (or AbortUploads)
	//   Collect        Uploading -> Resident, destroy retired textures
	//   DestroyTexture handle invalid now; RHI objects retire after the last use
	//
	// This checkpoint uploads uncompressed native-mip 2D payloads (R8, RG8,
	// RGBA8, RGBA8 sRGB, RGBA16F). Block-compressed/KTX2 payloads are rejected
	// explicitly until the RHI gains block-aware buffer/image copies. Handles are
	// registry slots (not yet bindless descriptors). Externally synchronized; the
	// device must outlive this object and supplied timelines its pending work.
	class TextureResidency
	{
	  public:
		TextureResidency(Rhi::Device& device, const TextureResidencyDesc& desc = {});
		~TextureResidency();
		TextureResidency(const TextureResidency&) = delete;
		TextureResidency& operator=(const TextureResidency&) = delete;

		// Chooses the first payload variant this residency can upload, if any.
		static std::optional<TexturePayloadSelection> SelectPayload(const Assets::TextureAsset& texture);

		// Throws std::invalid_argument for unsupported/inconsistent payloads,
		// std::length_error when slots are exhausted and std::runtime_error when the
		// RHI cannot create the texture or view. Nothing changes on failure.
		GpuTextureHandle CreateTexture(const Assets::TextureAsset& texture, std::string_view debugName = {});
		// Same retirement rules as GeometryHeap::DestroyMesh.
		bool DestroyTexture(GpuTextureHandle texture, Rhi::TimelinePoint lastUse = {});

		// Import once per graph; throws while a previous import awaits commit/abort.
		TextureGraphResources Import(RenderGraph& graph);
		void CommitUploads(Rhi::TimelinePoint completion);
		void AbortUploads();

		std::size_t Collect();
		void Drain();

		bool IsValid(GpuTextureHandle texture) const { return textures.IsValid(texture); }

		GpuUploadState GetState(GpuTextureHandle texture) const;
		Rhi::Texture* GetTexture(GpuTextureHandle texture) const;
		// Full-resource sampled view, created with the texture.
		Rhi::TextureView* GetView(GpuTextureHandle texture) const;
		TextureResidencyStats GetStats() const;

	  private:
		Rhi::Device& device;
		std::string name;
		GpuResourceRegistry<GpuTextureTag, Internal::TextureRecord> textures;
		std::vector<GpuTextureHandle> recorded;
		bool importPending = false;
	};
} // namespace Swim::Render
