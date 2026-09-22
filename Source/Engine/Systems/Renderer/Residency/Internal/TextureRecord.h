#pragma once
#include "Engine/Systems/Renderer/RHI/RhiContracts.h"
#include "Engine/Systems/Renderer/Resources/GpuUploadState.h"

#include <memory>
#include <vector>

namespace Swim::Render::Internal
{
	struct TextureMipUpload
	{
		Rhi::Extent3D Extent{};
		std::uint64_t Offset = 0; // Within Bytes.
		std::uint64_t Size = 0;
	};

	struct TextureRecord
	{
		std::unique_ptr<Rhi::Texture> Texture;
		std::unique_ptr<Rhi::TextureView> View;
		GpuUploadState State = GpuUploadState::PendingUpload;
		Rhi::TimelinePoint Upload{};
		std::uint64_t TexelBytes = 0;
		std::uint64_t Alignment = 4;
		// CPU copy of every mip, retained until the upload submission is committed.
		std::shared_ptr<const std::vector<std::byte>> Bytes;
		std::vector<TextureMipUpload> Mips;
	};
} // namespace Swim::Render::Internal
