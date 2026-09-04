#pragma once

#include <filesystem>

namespace Engine::Logging
{

	bool Initialize();
	void Flush();
	void Shutdown();
	const std::filesystem::path& GetLogFilePath();

}
