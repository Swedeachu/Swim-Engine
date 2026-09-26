#include "PlatformSystem.h"
#include <SDL3/SDL.h>
#include <iostream>
#include <stdexcept>

namespace Swim::Platform
{

	PlatformSystem::PlatformSystem() = default;

	PlatformSystem::~PlatformSystem()
	{
		Shutdown();
	}

	bool PlatformSystem::Initialize(const PlatformDesc& desc)
	{
		if (initialized)
		{
			return true;
		}

		headless = desc.Headless;

		SDL_InitFlags flags = SDL_INIT_EVENTS | SDL_INIT_GAMEPAD;
		bool initializedSdl = false;
		if (!headless)
		{
			flags |= SDL_INIT_VIDEO;
		}
		else
		{
			// Headless still needs the video subsystem's Vulkan loader (offscreen GPU
			// rendering, captures, CI); the offscreen driver needs no display. Without it,
			// fall back to events only (no windows, no Vulkan through SDL).
			SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
			initializedSdl = SDL_Init(flags | SDL_INIT_VIDEO);
			SDL_ResetHint(SDL_HINT_VIDEO_DRIVER);
		}

		if (!initializedSdl && !SDL_Init(flags))
		{
			std::cerr << "Failed to initialize SDL3 platform services: " << SDL_GetError() << '\n';
			return false;
		}

		fileSystem = std::make_unique<FileSystem>();
		FileSystemDesc fileSystemDesc{};
		fileSystemDesc.OrganizationName = desc.OrganizationName;
		fileSystemDesc.ApplicationName = desc.ApplicationName;
		if (!fileSystem->Initialize(fileSystemDesc))
		{
			fileSystem.reset();
			SDL_Quit();
			return false;
		}

		windowSystem = std::make_unique<WindowSystem>();
		initialized = true;
		return true;
	}

	void PlatformSystem::Shutdown()
	{
		if (!initialized)
		{
			return;
		}

		windowSystem.reset();
		fileSystem.reset();
		SDL_Quit();
		initialized = false;
		headless = false;
	}

	WindowSystem& PlatformSystem::GetWindowSystem()
	{
		if (!windowSystem)
		{
			throw std::runtime_error("PlatformSystem is not initialized.");
		}

		return *windowSystem;
	}

	const WindowSystem& PlatformSystem::GetWindowSystem() const
	{
		if (!windowSystem)
		{
			throw std::runtime_error("PlatformSystem is not initialized.");
		}

		return *windowSystem;
	}

	FileSystem& PlatformSystem::GetFileSystem()
	{
		if (!fileSystem)
		{
			throw std::runtime_error("PlatformSystem is not initialized.");
		}

		return *fileSystem;
	}

	const FileSystem& PlatformSystem::GetFileSystem() const
	{
		if (!fileSystem)
		{
			throw std::runtime_error("PlatformSystem is not initialized.");
		}

		return *fileSystem;
	}

	void PlatformSystem::PumpEvents(const WindowSystem::WindowEventHandler& windowHandler, const WindowSystem::InputEventHandler& inputHandler)
	{
		GetWindowSystem().PumpEvents(windowHandler, inputHandler);
	}

}
