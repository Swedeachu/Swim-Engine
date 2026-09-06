#pragma once

#include "Engine/Platform/FileSystem.h"

#include <atomic>
#include <chrono>
#include <fstream>
#include <stdexcept>

namespace Swim::Testing
{

	class TemporaryPipelineCacheFile
	{
	public:
		TemporaryPipelineCacheFile()
		{
			static std::atomic<unsigned> sequence{ 0 };
			const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
			for (unsigned attempt = 0; attempt < 16; ++attempt)
			{
				directory = std::filesystem::temp_directory_path() /
					("swim-pipeline-cache-" + std::to_string(stamp) + "-" + std::to_string(sequence++));
				if (std::filesystem::create_directory(directory))
				{
					return;
				}
			}
			throw std::runtime_error("Could not create pipeline cache test directory");
		}

		~TemporaryPipelineCacheFile()
		{
			std::error_code error;
			std::filesystem::remove_all(directory, error);
		}

		void Save(std::span<const std::byte> data) const
		{
			std::ofstream file(directory / "pipelines.cache", std::ios::binary | std::ios::trunc);
			file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
			file.close();
			if (!file)
			{
				throw std::runtime_error("Could not save test pipeline cache");
			}
		}

		std::vector<std::byte> Load() const
		{
			return Platform::FileSystem{}.ReadFileBlocking(directory / "pipelines.cache");
		}

	private:
		std::filesystem::path directory;
	};

} // namespace Swim::Testing
