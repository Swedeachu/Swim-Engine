#pragma once
#include "Engine/Assets/AssetHandle.h"
#include "Engine/Assets/AssetState.h"
#include "Engine/Assets/SassetDecodedAsset.h"
#include "Engine/IO/AsyncIoService.h"
#include "Engine/Jobs/JobSystem.h"
#include "Engine/Systems/Renderer/RHI/RhiContracts.h"
#include "Engine/Systems/Renderer/GpuScene/RenderBounds.h"
#include "Engine/Systems/Renderer/Residency/AssetResidencyState.h"
#include "Engine/Systems/Renderer/Resources/GpuHandle.h"

#include <memory>

namespace Swim::Render::Internal
{
	enum class ResidencyAssetKind : std::uint8_t
	{
		Mesh,
		Texture
	};

	// Written by exactly one decode job, read on the owner thread only after the
	// job handle reports completion.
	struct AssetDecodeSlot
	{
		Assets::SassetDecodeResult Result;
	};

	struct AssetResidencyRequest
	{
		ResidencyAssetKind Kind = ResidencyAssetKind::Mesh;
		Assets::AssetHandle<Assets::MeshAsset> MeshAsset;
		Assets::AssetHandle<Assets::TextureAsset> TextureAsset;
		AssetResidencyState State = AssetResidencyState::Queued;
		std::uint64_t Sequence = 0; // FIFO order across kinds.
		IO::ReadRequest Read;
		Jobs::JobHandle Decode;
		std::shared_ptr<AssetDecodeSlot> Slot;
		GpuMeshHandle Mesh;
		RenderBounds MeshBounds = RenderBounds::Infinite();
		GpuTextureHandle Texture;
		BindlessTextureHandle Bindless; // Registered once the texture is Resident.
		bool BindlessRejected = false;	// The RHI refused the view; Error explains why.
		Assets::AssetError Error;
	};
} // namespace Swim::Render::Internal
