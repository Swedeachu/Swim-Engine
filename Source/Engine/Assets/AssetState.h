#pragma once

#include "Engine/Assets/AssetId.h"
#include "Engine/Assets/ContentHash.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Swim::Assets
{

	enum class AssetLoadState : std::uint8_t
	{
		Unloaded,
		Queued,
		Loading,
		Resident,
		Failed
	};

	enum class AssetErrorCode : std::uint8_t
	{
		None,
		NotFound,
		Io,
		InvalidData,
		UnsupportedVersion,
		DependencyFailed,
		Cancelled,
		Internal
	};

	struct AssetError
	{
		AssetErrorCode Code = AssetErrorCode::None;
		std::string Message;

		bool HasError() const { return Code != AssetErrorCode::None; }
	};

	struct AssetStatus
	{
		AssetId Id{};
		std::uint32_t Generation = 0;
		AssetLoadState State = AssetLoadState::Unloaded;
		ContentHash Hash{};
		std::vector<AssetId> Dependencies;
		AssetError Error{};
		std::string LogicalPath;
		bool Declared = false;
	};

}
