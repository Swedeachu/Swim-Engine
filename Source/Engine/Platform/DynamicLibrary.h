#pragma once

#include <filesystem>
#include <string>

namespace Swim::Platform
{

	class DynamicLibrary
	{
	public:

		using FunctionPointer = void (*)();

		DynamicLibrary() = default;
		explicit DynamicLibrary(const std::filesystem::path& path);
		DynamicLibrary(const DynamicLibrary&) = delete;
		DynamicLibrary& operator=(const DynamicLibrary&) = delete;
		DynamicLibrary(DynamicLibrary&& other) noexcept;
		DynamicLibrary& operator=(DynamicLibrary&& other) noexcept;
		~DynamicLibrary();

		bool Open(const std::filesystem::path& path);
		void Close();
		FunctionPointer GetFunction(const char* name) const;
		bool IsOpen() const { return handle != nullptr; }
		const std::string& GetLastError() const { return lastError; }

	private:

		void* handle = nullptr;
		std::string lastError;
	};

}
