#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>

namespace Swim::Platform
{

	class MappedFile
	{
	public:

		MappedFile();
		MappedFile(const MappedFile&) = delete;
		MappedFile& operator=(const MappedFile&) = delete;
		MappedFile(MappedFile&&) noexcept;
		MappedFile& operator=(MappedFile&&) noexcept;
		~MappedFile();

		bool OpenReadOnly(const std::filesystem::path& path);
		void Close();

		bool IsOpen() const;
		const std::byte* Data() const;
		size_t Size() const;
		std::span<const std::byte> Bytes() const { return { Data(), Size() }; }

	private:

		struct Impl;
		std::unique_ptr<Impl> impl;
	};

}
