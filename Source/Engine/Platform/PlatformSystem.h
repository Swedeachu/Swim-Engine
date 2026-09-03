#pragma once

#include "FileSystem.h"
#include "WindowSystem.h"
#include <memory>
#include <string>

namespace Swim::Platform
{

	struct PlatformDesc
	{
		bool Headless = false;
		std::string OrganizationName = "Swim Services";
		std::string ApplicationName = "Swim Engine";
	};

	class PlatformSystem
	{
	public:

		PlatformSystem();
		~PlatformSystem();

		bool Initialize(const PlatformDesc& desc = {});
		void Shutdown();
		bool IsInitialized() const { return initialized; }
		bool IsHeadless() const { return headless; }

		WindowSystem& GetWindowSystem();
		const WindowSystem& GetWindowSystem() const;

		FileSystem& GetFileSystem();
		const FileSystem& GetFileSystem() const;

		void PumpEvents(const WindowSystem::WindowEventHandler& windowHandler, const WindowSystem::InputEventHandler& inputHandler);

	private:

		bool initialized = false;
		bool headless = false;
		std::unique_ptr<WindowSystem> windowSystem;
		std::unique_ptr<FileSystem> fileSystem;
	};

}
