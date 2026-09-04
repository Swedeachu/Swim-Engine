#pragma once

#include "Tools/AssetCompiler/IntermediateModel.h"

#include <filesystem>
#include <string>

namespace Swim::AssetCompiler
{

	enum class GltfImportErrorCode : std::uint8_t
	{
		None,
		FileOpenFailed,
		ParseFailed,
		UnsupportedFeature,
		InvalidData,
		IndexOverflow
	};

	struct GltfImportError
	{
		GltfImportErrorCode Code = GltfImportErrorCode::None;
		std::string Message;
	};

	struct GltfImportResult
	{
		IntermediateModel Model;
		GltfImportError Error;

		explicit operator bool() const noexcept
		{
			return Error.Code == GltfImportErrorCode::None;
		}
	};

	class GltfImporter final
	{
	public:
		GltfImportResult Import(const std::filesystem::path& path) const;
	};

}
