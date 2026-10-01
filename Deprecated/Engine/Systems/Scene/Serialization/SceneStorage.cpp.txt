#include "PCH.h"
#include "SceneStorage.h"

#include "Engine/Platform/FileSystem.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <system_error>

namespace Engine
{

	SceneStorageResult SceneStorage::Save(std::string_view sceneName, const nlohmann::json& document) const
	{
		SceneStorageResult result;
		if (!files)
		{
			result.Message = "SceneStorage has no FileSystem.";
			return result;
		}

		std::filesystem::path fileName = std::filesystem::path(std::string(sceneName)).filename();
		if (fileName.empty())
		{
			fileName = "Scene";
		}
		fileName.replace_extension(".json");

		const std::filesystem::path scenesDirectory = files->GetExecutableDirectory() / "Scenes";
		std::error_code error;
		std::filesystem::create_directories(scenesDirectory, error);
		if (error)
		{
			result.Path = scenesDirectory;
			result.Message = "Failed to create scene storage directory: " + error.message();
			return result;
		}

		result.Path = scenesDirectory / fileName;
		std::ofstream output(result.Path, std::ios::binary | std::ios::trunc);
		if (!output.is_open())
		{
			result.Message = "Failed to open scene document for writing.";
			return result;
		}

		output << document.dump(2);
		if (!output.good())
		{
			result.Message = "Failed while writing scene document.";
			return result;
		}

		result.Success = true;
		return result;
	}

}
