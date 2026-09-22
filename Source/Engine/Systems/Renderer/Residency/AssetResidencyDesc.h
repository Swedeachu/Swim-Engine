#pragma once
#include "Engine/Assets/AssetId.h"

#include <cstdint>
#include <filesystem>
#include <functional>

namespace Swim::Render
{
	class BindlessResourceTable;

	struct AssetResidencyDesc
	{
		// Maps a cooked asset identity to its .sasset object. Requests for assets
		// that are not already resident in the AssetSystem fail with NotFound when
		// this is empty or returns an empty path.
		std::function<std::filesystem::path(Assets::AssetId)> ResolveCookedPath;
		std::uint32_t MaxConcurrentReads = 8;
		std::uint32_t MaxConcurrentDecodes = 8;
		// CPU bytes handed to GeometryHeap/TextureResidency per Update. At least one
		// waiting asset advances per Update even when it alone exceeds the budget.
		std::uint64_t UploadBudgetBytes = 64ull << 20;
		// Keep the CPU MeshAsset/TextureAsset resident after its GPU copy is staged.
		// By default it is unloaded (GPU residency is independent of CPU validity).
		bool RetainCpuAssets = false;
		// Optional. Resident textures are registered here (their view becomes a bindless
		// element) and released with the texture's lastUse. Must outlive the service.
		BindlessResourceTable* Bindless = nullptr;
	};
} // namespace Swim::Render
