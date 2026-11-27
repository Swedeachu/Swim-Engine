#pragma once

#include <filesystem>
#include <string_view>

namespace Engine::Logging
{

	bool Initialize();
	void Flush();
	void Shutdown();
	const std::filesystem::path& GetLogFilePath();

	enum class Level
	{
		Debug,
		Info,
		Warning,
		Error,
	};

	// Detail level for debug lines (0 = none). It starts from the SWIM_DEBUG_LOG_LEVEL
	// environment variable; asking whether a level is shown costs one relaxed atomic load.
	void SetDebugLevel(int level) noexcept;
	int GetDebugLevel() noexcept;

	// One line into the engine log at its level. Before Initialize (or after Shutdown),
	// warnings and errors go to stderr and the rest is dropped.
	void Write(Level level, std::string_view text) noexcept;
}
