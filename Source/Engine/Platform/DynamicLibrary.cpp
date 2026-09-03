#include "DynamicLibrary.h"
#include <SDL3/SDL_loadso.h>
#include <utility>

namespace Swim::Platform
{

	DynamicLibrary::DynamicLibrary(const std::filesystem::path& path)
	{
		Open(path);
	}

	DynamicLibrary::DynamicLibrary(DynamicLibrary&& other) noexcept
		: handle(std::exchange(other.handle, nullptr)), lastError(std::move(other.lastError)) {}

	DynamicLibrary& DynamicLibrary::operator=(DynamicLibrary&& other) noexcept
	{
		if (this != &other)
		{
			Close();
			handle = std::exchange(other.handle, nullptr);
			lastError = std::move(other.lastError);
		}
		return *this;
	}

	DynamicLibrary::~DynamicLibrary()
	{
		Close();
	}

	bool DynamicLibrary::Open(const std::filesystem::path& path)
	{
		Close();
		const std::u8string utf8Path = path.u8string();
		handle = SDL_LoadObject(reinterpret_cast<const char*>(utf8Path.c_str()));
		if (!handle)
		{
			lastError = SDL_GetError();
			return false;
		}

		lastError.clear();
		return true;
	}

	void DynamicLibrary::Close()
	{
		if (handle)
		{
			SDL_UnloadObject(static_cast<SDL_SharedObject*>(handle));
			handle = nullptr;
		}
	}

	DynamicLibrary::FunctionPointer DynamicLibrary::GetFunction(const char* name) const
	{
		return handle ? SDL_LoadFunction(static_cast<SDL_SharedObject*>(handle), name) : nullptr;
	}

}
