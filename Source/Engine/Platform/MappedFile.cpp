#include "MappedFile.h"
#include <utility>

#if defined(_WIN32)
	#include "Internal/WindowsApi.h"
#else
	#include <fcntl.h>
	#include <sys/mman.h>
	#include <sys/stat.h>
	#include <unistd.h>
#endif

namespace Swim::Platform
{

	struct MappedFile::Impl
	{
		const std::byte* Data = nullptr;
		size_t Size = 0;

	#if defined(_WIN32)
		HANDLE File = INVALID_HANDLE_VALUE;
		HANDLE Mapping = nullptr;
	#else
		int File = -1;
	#endif
	};

	MappedFile::MappedFile() : impl(std::make_unique<Impl>()) {}
	MappedFile::MappedFile(MappedFile&&) noexcept = default;
	MappedFile& MappedFile::operator=(MappedFile&&) noexcept = default;

	MappedFile::~MappedFile()
	{
		Close();
	}

	bool MappedFile::OpenReadOnly(const std::filesystem::path& path)
	{
		Close();
		if (!impl)
		{
			impl = std::make_unique<Impl>();
		}

	#if defined(_WIN32)
		impl->File = CreateFileW(
			path.c_str(),
			GENERIC_READ,
			FILE_SHARE_READ,
			nullptr,
			OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS,
			nullptr
		);
		if (impl->File == INVALID_HANDLE_VALUE)
		{
			return false;
		}

		LARGE_INTEGER fileSize{};
		if (!GetFileSizeEx(impl->File, &fileSize) || fileSize.QuadPart < 0)
		{
			Close();
			return false;
		}

		impl->Size = static_cast<size_t>(fileSize.QuadPart);
		if (impl->Size == 0)
		{
			return true;
		}

		impl->Mapping = CreateFileMappingW(impl->File, nullptr, PAGE_READONLY, 0, 0, nullptr);
		if (!impl->Mapping)
		{
			Close();
			return false;
		}

		impl->Data = static_cast<const std::byte*>(MapViewOfFile(impl->Mapping, FILE_MAP_READ, 0, 0, 0));
		if (!impl->Data)
		{
			Close();
			return false;
		}
	#else
		impl->File = open(path.c_str(), O_RDONLY);
		if (impl->File < 0)
		{
			return false;
		}

		struct stat status{};
		if (fstat(impl->File, &status) != 0 || status.st_size < 0)
		{
			Close();
			return false;
		}

		impl->Size = static_cast<size_t>(status.st_size);
		if (impl->Size == 0)
		{
			return true;
		}

		void* mapped = mmap(nullptr, impl->Size, PROT_READ, MAP_PRIVATE, impl->File, 0);
		if (mapped == MAP_FAILED)
		{
			Close();
			return false;
		}
		impl->Data = static_cast<const std::byte*>(mapped);
	#endif

		return true;
	}

	void MappedFile::Close()
	{
		if (!impl)
		{
			return;
		}

	#if defined(_WIN32)
		if (impl->Data)
		{
			UnmapViewOfFile(impl->Data);
		}
		if (impl->Mapping)
		{
			CloseHandle(impl->Mapping);
		}
		if (impl->File != INVALID_HANDLE_VALUE)
		{
			CloseHandle(impl->File);
		}
		impl->File = INVALID_HANDLE_VALUE;
		impl->Mapping = nullptr;
	#else
		if (impl->Data && impl->Size > 0)
		{
			munmap(const_cast<std::byte*>(impl->Data), impl->Size);
		}
		if (impl->File >= 0)
		{
			close(impl->File);
		}
		impl->File = -1;
	#endif

		impl->Data = nullptr;
		impl->Size = 0;
	}

	bool MappedFile::IsOpen() const
	{
		if (!impl)
		{
			return false;
		}

	#if defined(_WIN32)
		return impl->File != INVALID_HANDLE_VALUE;
	#else
		return impl->File >= 0;
	#endif
	}

	const std::byte* MappedFile::Data() const
	{
		return impl ? impl->Data : nullptr;
	}

	size_t MappedFile::Size() const
	{
		return impl ? impl->Size : 0;
	}

}
