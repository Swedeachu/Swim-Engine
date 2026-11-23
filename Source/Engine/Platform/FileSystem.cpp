#include "FileSystem.h"
#include <SDL3/SDL_filesystem.h>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace Swim::Platform
{

	namespace
	{

		std::filesystem::path PathFromUtf8(const char* value)
		{
			if (!value || value[0] == '\0')
			{
				return {};
			}

			const size_t length = std::strlen(value);
			std::u8string utf8(length, u8'\0');
			std::memcpy(utf8.data(), value, length);
			return std::filesystem::path(utf8);
		}

	}

	std::filesystem::path FileSystem::GetDevPath(std::string_view key) const
	{
		const auto found = devPaths.find(key);
		return found == devPaths.end() ? std::filesystem::path{} : found->second;
	}

	bool FileSystem::Initialize(const FileSystemDesc& desc)
	{
		const char* basePath = SDL_GetBasePath();

		if (!basePath)
		{
			return false;
		}

		executableDirectory = PathFromUtf8(basePath);

		assetRoot = desc.AssetRootOverride.empty()
			? executableDirectory / "Assets"
			: desc.AssetRootOverride;

		if (!desc.UserDataRootOverride.empty())
		{
			userDataRoot = desc.UserDataRootOverride;
		}
		else
		{
			char* prefPath = SDL_GetPrefPath(desc.OrganizationName.c_str(), desc.ApplicationName.c_str());

			if (!prefPath)
			{
				return false;
			}

			userDataRoot = PathFromUtf8(prefPath);
			SDL_free(prefPath);
		}

		cacheRoot = userDataRoot / "Cache";
		temporaryRoot = std::filesystem::temp_directory_path() / "SwimEngine";

		devPaths.clear();

		if (std::ifstream file(executableDirectory / "DevPaths.cfg"); file)
		{
			std::string line;

			while (std::getline(file, line))
			{
				while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
				{
					line.pop_back();
				}

				const auto equals = line.find('=');

				if (equals != std::string::npos && equals > 0 && equals + 1 < line.size() && line[0] != '#')
				{
					devPaths[line.substr(0, equals)] = PathFromUtf8(line.c_str() + equals + 1);
				}
			}
		}

		std::error_code error;
		std::filesystem::create_directories(userDataRoot, error);
		std::filesystem::create_directories(cacheRoot, error);
		std::filesystem::create_directories(temporaryRoot, error);
		return true;
	}

	std::filesystem::path FileSystem::ResolveExecutablePath(const std::filesystem::path& relativePath) const
	{
		return executableDirectory / relativePath;
	}

	std::filesystem::path FileSystem::ResolveAssetPath(const std::filesystem::path& relativePath) const
	{
		return assetRoot / relativePath;
	}

	std::vector<std::byte> FileSystem::ReadFileBlocking(const std::filesystem::path& path) const
	{
		std::ifstream file(path, std::ios::binary | std::ios::ate);

		if (!file)
		{
			throw std::runtime_error("Failed to open file: " + path.string());
		}

		const std::streamsize size = file.tellg();

		if (size < 0)
		{
			throw std::runtime_error("Failed to query file size: " + path.string());
		}

		std::vector<std::byte> bytes(static_cast<size_t>(size));
		file.seekg(0, std::ios::beg);

		if (size > 0 && !file.read(reinterpret_cast<char*>(bytes.data()), size))
		{
			throw std::runtime_error("Failed to read file: " + path.string());
		}

		return bytes;
	}

	MappedFile FileSystem::MapFileReadOnly(const std::filesystem::path& path) const
	{
		MappedFile mapped;

		if (!mapped.OpenReadOnly(path))
		{
			throw std::runtime_error("Failed to memory-map file: " + path.string());
		}

		return mapped;
	}

}
