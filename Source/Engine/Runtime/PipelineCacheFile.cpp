#include "Engine/Runtime/PipelineCacheFile.h"

#include "Engine/Systems/Renderer/RHI/RhiContracts.h"

#include <fstream>
#include <iostream>
#include <vector>

namespace Engine
{

	bool LoadPipelineCacheFile(Swim::Rhi::Device& device, const std::filesystem::path& path)
	{
		std::error_code error;
		const auto size = std::filesystem::file_size(path, error);

		if (error || size == 0 || size > Swim::Rhi::MaxPipelineCacheDataBytes)
		{
			return false;
		}

		std::vector<std::byte> bytes(size);
		std::ifstream file(path, std::ios::binary);

		if (!file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size)))
		{
			return false;
		}

		const auto status = device.LoadPipelineCache(bytes);

		if (status != Swim::Rhi::PipelineCacheLoadStatus::Loaded)
		{
			std::cout << "[Render] Pipeline cache not used (status " << static_cast<int>(status) << "): " << path.string() << '\n';
			return false;
		}

		return true;
	}

	std::filesystem::path PipelineCacheFileName(const Swim::Rhi::Device& device)
	{
		return device.GetAdapterInfo().Api == Swim::Rhi::GraphicsApi::D3D12 ? "pipelines-d3d12.bin" : "pipelines.bin";
	}

	bool SavePipelineCacheFile(const Swim::Rhi::Device& device, const std::filesystem::path& path)
	{
		const auto data = device.GetPipelineCacheData();

		if (data.Status != Swim::Rhi::PipelineCacheDataStatus::Ready || data.Bytes.empty())
		{
			return false;
		}

		auto temporary = path;
		temporary += ".tmp";

		{
			std::ofstream file(temporary, std::ios::binary | std::ios::trunc);

			if (!file.write(reinterpret_cast<const char*>(data.Bytes.data()), static_cast<std::streamsize>(data.Bytes.size())))
			{
				return false;
			}
		}

		std::error_code error;
		std::filesystem::rename(temporary, path, error);

		if (error)
		{
			std::filesystem::remove(temporary, error);
			return false;
		}

		return true;
	}

} // namespace Engine
