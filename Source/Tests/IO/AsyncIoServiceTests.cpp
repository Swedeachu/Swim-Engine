#include "Engine/IO/AsyncIoService.h"
#include "Engine/Jobs/JobSystem.h"
#include "Engine/Platform/FileSystem.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>

namespace
{

	void Require(bool condition, const char* message)
	{
		if (!condition)
		{
			std::cerr << "AsyncIoService test failed: " << message << '\n';
			std::exit(1);
		}
	}

	std::vector<std::byte> MakePayload(std::size_t size)
	{
		std::vector<std::byte> bytes(size);
		for (std::size_t i = 0; i < bytes.size(); ++i)
		{
			bytes[i] = static_cast<std::byte>((i * 37 + 11) & 0xff);
		}
		return bytes;
	}

	void WriteFile(const std::filesystem::path& path, const std::vector<std::byte>& bytes)
	{
		std::ofstream file(path, std::ios::binary | std::ios::trunc);
		Require(static_cast<bool>(file), "open test file for write");
		if (!bytes.empty())
		{
			file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		}
		Require(static_cast<bool>(file), "write test file");
	}

	void RequireChunkEquals(
		const Swim::IO::IoReadChunk& chunk,
		const std::vector<std::byte>& payload,
		std::uint64_t offset,
		std::uint64_t size,
		const char* message
	)
	{
		Require(chunk.Range.Offset == offset, message);
		Require(chunk.Range.Size == size, message);
		Require(chunk.Bytes.size() == static_cast<std::size_t>(size), message);
		for (std::size_t i = 0; i < chunk.Bytes.size(); ++i)
		{
			Require(chunk.Bytes[i] == payload[static_cast<std::size_t>(offset) + i], message);
		}
	}

}

int main()
{
	Swim::Platform::FileSystem fileSystem;
	Swim::Platform::FileSystemDesc fileSystemDesc{};
	fileSystemDesc.OrganizationName = "Swim Tests";
	fileSystemDesc.ApplicationName = "Async IO";
	fileSystemDesc.UserDataRootOverride = std::filesystem::temp_directory_path() / "SwimAsyncIoTestsUser";
	Require(fileSystem.Initialize(fileSystemDesc), "FileSystem initialize");

	Swim::Jobs::JobSystem jobs;
	Swim::Jobs::JobSystemDesc jobDesc{};
	jobDesc.WorkerThreads = 2;
	jobDesc.BlockingThreads = 1;
	Require(jobs.Initialize(jobDesc), "JobSystem initialize");

	Swim::IO::AsyncIoService io;
	Require(io.Initialize(fileSystem, jobs), "AsyncIoService initialize");

	const std::filesystem::path testRoot = std::filesystem::temp_directory_path() / "SwimAsyncIoServiceTests";
	std::filesystem::create_directories(testRoot);
	const std::filesystem::path dataPath = testRoot / "ranges.bin";
	const std::vector<std::byte> payload = MakePayload(4096);
	WriteFile(dataPath, payload);

	const std::thread::id ownerThread = std::this_thread::get_id();
	bool fullCompletionRan = false;
	auto full = io.ReadFileAsync(dataPath, {}, [&](const Swim::IO::ReadRequest& request)
	{
		Require(std::this_thread::get_id() == ownerThread, "completion runs on owner thread");
		Require(request.GetStatus() == Swim::IO::IoStatus::Succeeded, "full read completion status");
		fullCompletionRan = true;
	});
	io.Wait(full);
	Require(fullCompletionRan, "full read completion dispatched");
	Require(full.GetResult().FileSize == payload.size(), "full read file size");
	Require(full.GetResult().GetSingleBuffer() == payload, "full read bytes");

	auto range = io.ReadRangeAsync(dataPath, { 123, 777 });
	io.Wait(range);
	Require(range.GetStatus() == Swim::IO::IoStatus::Succeeded, "range read status");
	Require(range.GetResult().Chunks.size() == 1, "range read chunk count");
	RequireChunkEquals(range.GetResult().Chunks[0], payload, 123, 777, "range read bytes");

	const std::vector<Swim::IO::IoReadRange> ranges = {
		{ 0, 64 },
		{ 64, 32 },
		{ 104, 24 },
		{ 2048, 128 }
	};
	Swim::IO::IoReadOptions batchOptions{};
	batchOptions.MaxCoalesceGapBytes = 8;
	auto batch = io.ReadRangesAsync(dataPath, ranges, batchOptions);
	io.Wait(batch);
	Require(batch.GetResult().Chunks.size() == ranges.size(), "batch preserves chunk count");
	for (std::size_t i = 0; i < ranges.size(); ++i)
	{
		RequireChunkEquals(
			batch.GetResult().Chunks[i],
			payload,
			ranges[i].Offset,
			ranges[i].Size,
			"batch range bytes"
		);
	}

	std::vector<Swim::IO::ReadRequest> concurrent;
	for (std::uint64_t i = 0; i < 16; ++i)
	{
		concurrent.push_back(io.ReadRangeAsync(dataPath, { i * 128, 96 }));
	}
	for (std::size_t i = 0; i < concurrent.size(); ++i)
	{
		io.Wait(concurrent[i]);
		Require(concurrent[i].GetStatus() == Swim::IO::IoStatus::Succeeded, "concurrent range status");
		RequireChunkEquals(
			concurrent[i].GetResult().Chunks[0],
			payload,
			static_cast<std::uint64_t>(i) * 128,
			96,
			"concurrent range bytes"
		);
	}

	auto failed = io.ReadFileAsync(testRoot / "does-not-exist.bin");
	io.Wait(failed);
	Require(failed.GetStatus() == Swim::IO::IoStatus::Failed, "failed IO status");
	Require(!failed.GetErrorMessage().empty(), "failed IO error message");

	std::atomic<bool> releaseBlockingLane{ false };
	auto blocker = jobs.ScheduleBlocking([&]()
	{
		while (!releaseBlockingLane.load(std::memory_order_acquire))
		{
			std::this_thread::yield();
		}
	});

	auto cancelled = io.ReadRangeAsync(dataPath, { 0, 128 });
	cancelled.RequestCancel();
	releaseBlockingLane.store(true, std::memory_order_release);
	jobs.Wait(blocker);
	io.Wait(cancelled);
	Require(cancelled.GetStatus() == Swim::IO::IoStatus::Cancelled, "cancelled IO status");
	Require(cancelled.IsCancellationRequested(), "cancelled IO request flag");

	Swim::Platform::MappedFile mapped = io.MapFileReadOnlyBlocking(dataPath);
	Require(mapped.IsOpen(), "mapped file open");
	Require(mapped.Size() == payload.size(), "mapped file size");
	Require(mapped.Bytes()[123] == payload[123], "mapped file bytes");
	mapped.Close();

	io.Shutdown();
	Require(!io.IsRunning(), "AsyncIoService shutdown");
	jobs.Shutdown();
	Require(!jobs.IsRunning(), "JobSystem shutdown after IO");

	std::error_code error;
	std::filesystem::remove_all(testRoot, error);
	std::filesystem::remove_all(fileSystemDesc.UserDataRootOverride, error);
	return 0;
}
