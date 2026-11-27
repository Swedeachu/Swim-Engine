#pragma once

#include <filesystem>

namespace Swim::Rhi
{
	class Device;
}

namespace Engine
{

	// The device's pipeline cache kept on disk between runs, so pipelines first created mid-session
	// (joining a world, a new effect) come from the driver's compiled binaries instead of a full
	// compile. Load before the first pipeline is created; a missing, stale or foreign file is ignored.
	bool LoadPipelineCacheFile(Swim::Rhi::Device& device, const std::filesystem::path& path);

	// Written to a temporary file and renamed over the old one.
	bool SavePipelineCacheFile(const Swim::Rhi::Device& device, const std::filesystem::path& path);

	// The cache file's name for the device's backend, so each backend keeps its own cache.
	std::filesystem::path PipelineCacheFileName(const Swim::Rhi::Device& device);

} // namespace Engine
