#pragma once

#include <span>
#include <string>

namespace Swim::Platform
{

	struct ProcessResult
	{
		bool Started = false;
		int ExitCode = -1;
		std::string Output; // Standard output and standard error, interleaved.
		std::string Error;	// Why the process could not start.
	};

	// Runs a program (arguments[0], a path) to completion without a console window and
	// returns its exit code and output. Blocks the calling thread; any thread.
	ProcessResult RunProcess(std::span<const std::string> arguments);

} // namespace Swim::Platform
