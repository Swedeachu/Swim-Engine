#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>
#include <string_view>

namespace Swim::Platform
{
	class FileSystem;
}

namespace Engine
{

	struct SceneStorageResult
	{
		bool Success = false;
		std::filesystem::path Path;
		std::string Message;
	};

	class SceneStorage
	{
	public:

		explicit SceneStorage(Swim::Platform::FileSystem& files)
			: files(&files)
		{}

		SceneStorageResult Save(std::string_view sceneName, const nlohmann::json& document) const;

	private:

		Swim::Platform::FileSystem* files = nullptr;

	};

}
