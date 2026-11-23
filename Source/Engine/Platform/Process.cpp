#include "Process.h"

#include <SDL3/SDL_process.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_stdinc.h>

#include <vector>

namespace Swim::Platform
{

	ProcessResult RunProcess(std::span<const std::string> arguments)
	{
		ProcessResult result;

		if (arguments.empty())
		{
			result.Error = "no program";
			return result;
		}

		std::vector<const char*> argv;
		argv.reserve(arguments.size() + 1);

		for (const auto& argument : arguments)
		{
			argv.push_back(argument.c_str());
		}

		argv.push_back(nullptr);
		const SDL_PropertiesID properties = SDL_CreateProperties();
		SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, argv.data());
		SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL);
		SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
		SDL_SetBooleanProperty(properties, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);
		SDL_SetBooleanProperty(properties, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, true);
		SDL_Process* process = SDL_CreateProcessWithProperties(properties);
		SDL_DestroyProperties(properties);

		if (!process)
		{
			result.Error = SDL_GetError();
			return result;
		}

		result.Started = true;
		std::size_t size = 0;
		int exitCode = -1;

		if (void* data = SDL_ReadProcess(process, &size, &exitCode))
		{
			result.Output.assign(static_cast<const char*>(data), size);
			SDL_free(data);
		}

		result.ExitCode = exitCode;
		SDL_DestroyProcess(process);
		return result;
	}

} // namespace Swim::Platform
