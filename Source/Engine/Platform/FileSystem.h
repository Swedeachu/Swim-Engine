#pragma once

#include "MappedFile.h"
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace Swim::Platform
{

	struct FileSystemDesc
	{
		std::string OrganizationName = "Swim Services";
		std::string ApplicationName = "Swim Engine";
		std::filesystem::path AssetRootOverride;
		std::filesystem::path UserDataRootOverride;
	};

	class FileSystem
	{
	public:

		bool Initialize(const FileSystemDesc& desc = {});

		const std::filesystem::path& GetExecutableDirectory() const { return executableDirectory; }
		const std::filesystem::path& GetAssetRoot() const { return assetRoot; }
		const std::filesystem::path& GetUserDataRoot() const { return userDataRoot; }
		const std::filesystem::path& GetCacheRoot() const { return cacheRoot; }
		const std::filesystem::path& GetTemporaryRoot() const { return temporaryRoot; }

		std::filesystem::path ResolveExecutablePath(const std::filesystem::path& relativePath) const;
		std::filesystem::path ResolveAssetPath(const std::filesystem::path& relativePath) const;
		std::vector<std::byte> ReadFileBlocking(const std::filesystem::path& path) const;
		MappedFile MapFileReadOnly(const std::filesystem::path& path) const;

	private:

		std::filesystem::path executableDirectory;
		std::filesystem::path assetRoot;
		std::filesystem::path userDataRoot;
		std::filesystem::path cacheRoot;
		std::filesystem::path temporaryRoot;
	};

}
