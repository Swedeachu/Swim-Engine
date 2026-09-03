#pragma once

#include "Engine/Platform/MappedFile.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace Swim::Jobs
{
	class JobSystem;
}

namespace Swim::Platform
{
	class FileSystem;
}

namespace Swim::IO
{

	enum class IoPriority : std::uint8_t
	{
		High,
		Normal,
		Low
	};

	enum class IoStatus : std::uint8_t
	{
		Invalid,
		Queued,
		Reading,
		Succeeded,
		Failed,
		Cancelled
	};

	enum class IoShutdownMode : std::uint8_t
	{
		Drain,
		CancelPending
	};

	struct IoReadRange
	{
		std::uint64_t Offset = 0;
		std::uint64_t Size = 0;
	};

	struct IoReadOptions
	{
		IoPriority Priority = IoPriority::Normal;

		// Batched ranges are sorted internally and adjacent/overlapping reads are
		// coalesced. A non-zero gap permits reading a small amount of unrequested
		// data to reduce seek/syscall overhead for streaming package workloads.
		std::uint64_t MaxCoalesceGapBytes = 0;
	};

	struct IoReadChunk
	{
		IoReadRange Range{};
		std::vector<std::byte> Bytes;
	};

	struct IoReadResult
	{
		std::filesystem::path Path;
		std::uint64_t FileSize = 0;
		std::vector<IoReadChunk> Chunks;

		const std::vector<std::byte>& GetSingleBuffer() const;
	};

	namespace Detail
	{
		struct IoRequestState;
	}

	class ReadRequest
	{
	public:

		ReadRequest() = default;

		bool IsValid() const;
		IoStatus GetStatus() const;
		bool IsComplete() const;
		bool IsCancellationRequested() const;
		void RequestCancel() const;

		const IoReadResult& GetResult() const;
		const std::string& GetErrorMessage() const;

		explicit operator bool() const { return IsValid(); }

	private:

		explicit ReadRequest(std::shared_ptr<Detail::IoRequestState> state)
			: state(std::move(state))
		{
		}

		std::shared_ptr<Detail::IoRequestState> state;

		friend class AsyncIoService;
	};

	class AsyncIoService
	{
	public:

		using CompletionCallback = std::function<void(const ReadRequest&)>;

		AsyncIoService();
		~AsyncIoService();

		AsyncIoService(const AsyncIoService&) = delete;
		AsyncIoService& operator=(const AsyncIoService&) = delete;

		bool Initialize(Platform::FileSystem& fileSystem, Jobs::JobSystem& jobs);
		void Shutdown(IoShutdownMode mode = IoShutdownMode::Drain);
		bool IsRunning() const;

		ReadRequest ReadFileAsync(
			const std::filesystem::path& path,
			const IoReadOptions& options = {},
			CompletionCallback completion = {}
		);
		ReadRequest ReadRangeAsync(
			const std::filesystem::path& path,
			IoReadRange range,
			const IoReadOptions& options = {},
			CompletionCallback completion = {}
		);
		ReadRequest ReadRangesAsync(
			const std::filesystem::path& path,
			std::span<const IoReadRange> ranges,
			const IoReadOptions& options = {},
			CompletionCallback completion = {}
		);

		// Completion callbacks are always dispatched by PumpCompletions on the
		// thread which initialized this service. The engine pumps this queue from
		// its main thread, so callbacks never run on a blocking IO lane.
		std::size_t PumpCompletions(std::size_t maxCompletions = std::numeric_limits<std::size_t>::max());

		// Explicitly blocking entrypoints are for bootstrap, tools and tests only.
		// Runtime asset/streaming code should use the async request path above.
		IoReadResult ReadFileBlocking(const std::filesystem::path& path) const;
		IoReadResult ReadRangeBlocking(const std::filesystem::path& path, IoReadRange range) const;
		IoReadResult ReadRangesBlocking(
			const std::filesystem::path& path,
			std::span<const IoReadRange> ranges,
			std::uint64_t maxCoalesceGapBytes = 0
		) const;
		Platform::MappedFile MapFileReadOnlyBlocking(const std::filesystem::path& path) const;

		// Waiting is deliberately explicit and intended for bootstrap/tools/tests.
		// On the owner thread it also dispatches queued main-thread completions.
		void Wait(const ReadRequest& request);

	private:

		ReadRequest ScheduleRead(
			const std::filesystem::path& path,
			std::vector<IoReadRange> ranges,
			bool fullFile,
			const IoReadOptions& options,
			CompletionCallback completion
		);

		struct Impl;
		std::unique_ptr<Impl> impl;
	};

}
