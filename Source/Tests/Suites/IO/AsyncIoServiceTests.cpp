#include "Engine/IO/AsyncIoService.h"
#include "Engine/Jobs/JobSystem.h"
#include "Engine/Platform/FileSystem.h"
#include "Tests/Framework/Test.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

namespace
{

	std::vector<std::byte> MakePayload(std::size_t size)
	{
		std::vector<std::byte> bytes(size);
		for (std::size_t i = 0; i < bytes.size(); ++i)
		{
			bytes[i] = static_cast<std::byte>((i * 37 + 11) & 0xff);
		}
		return bytes;
	}

	void WritePayloadFile(const std::filesystem::path& path, const std::vector<std::byte>& bytes)
	{
		std::ofstream file(path, std::ios::binary | std::ios::trunc);
		SWIM_REQUIRE(static_cast<bool>(file));
		if (!bytes.empty())
		{
			file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		}
		SWIM_CHECK(static_cast<bool>(file));
	}

	void CheckChunkEquals(
		const Swim::IO::IoReadChunk& chunk,
		const std::vector<std::byte>& payload,
		std::uint64_t offset,
		std::uint64_t size)
	{
		SWIM_CHECK_EQUAL(chunk.Range.Offset, offset);
		SWIM_CHECK_EQUAL(chunk.Range.Size, size);
		SWIM_REQUIRE(chunk.Bytes.size() == static_cast<std::size_t>(size));

		bool bytesMatch = true;
		for (std::size_t i = 0; i < chunk.Bytes.size(); ++i)
		{
			if (chunk.Bytes[i] != payload[static_cast<std::size_t>(offset) + i])
			{
				bytesMatch = false;
				break;
			}
		}
		SWIM_CHECK(bytesMatch);
	}

	// The async IO service sits on top of the platform filesystem and the job
	// system, so each case builds and tears down the whole stack around a scratch
	// directory of its own.
	class ScopedIoFixture
	{

	public:

		ScopedIoFixture()
		{
			fileSystemDesc.OrganizationName = "Swim Tests";
			fileSystemDesc.ApplicationName = "Async IO";
			fileSystemDesc.UserDataRootOverride = std::filesystem::temp_directory_path() / "SwimAsyncIoTestsUser";
			fileSystemReady = fileSystem.Initialize(fileSystemDesc);

			Swim::Jobs::JobSystemDesc jobDesc{};
			jobDesc.WorkerThreads = 2;
			jobDesc.BlockingThreads = 1;
			jobsReady = jobs.Initialize(jobDesc);

			ioReady = io.Initialize(fileSystem, jobs);

			std::filesystem::create_directories(root);
			WritePayloadFile(DataPath(), payload);
		}

		~ScopedIoFixture()
		{
			if (io.IsRunning())
			{
				io.Shutdown();
			}
			if (jobs.IsRunning())
			{
				jobs.Shutdown();
			}

			std::error_code ignored;
			std::filesystem::remove_all(root, ignored);
			std::filesystem::remove_all(fileSystemDesc.UserDataRootOverride, ignored);
		}

		ScopedIoFixture(const ScopedIoFixture&) = delete;
		ScopedIoFixture& operator=(const ScopedIoFixture&) = delete;

		bool IsReady() const
		{
			return fileSystemReady && jobsReady && ioReady;
		}

		Swim::IO::AsyncIoService& Io()
		{
			return io;
		}

		Swim::Jobs::JobSystem& Jobs()
		{
			return jobs;
		}

		const std::vector<std::byte>& Payload() const
		{
			return payload;
		}

		std::filesystem::path Root() const
		{
			return root;
		}

		std::filesystem::path DataPath() const
		{
			return root / "ranges.bin";
		}

	private:

		std::filesystem::path root = std::filesystem::temp_directory_path() / "SwimAsyncIoServiceTests";
		std::vector<std::byte> payload = MakePayload(4096);

		Swim::Platform::FileSystem fileSystem;
		Swim::Platform::FileSystemDesc fileSystemDesc{};
		Swim::Jobs::JobSystem jobs;
		Swim::IO::AsyncIoService io;

		bool fileSystemReady = false;
		bool jobsReady = false;
		bool ioReady = false;

	};

}

SWIM_TEST("IO.AsyncIoService", "FullFileReadCompletesOnTheOwnerThread")
{
	ScopedIoFixture fixture;
	SWIM_REQUIRE(fixture.IsReady());

	const std::thread::id ownerThread = std::this_thread::get_id();
	bool completionRan = false;
	auto full = fixture.Io().ReadFileAsync(fixture.DataPath(), {}, [&](const Swim::IO::ReadRequest& request)
	{
		SWIM_CHECK(std::this_thread::get_id() == ownerThread);
		SWIM_CHECK(request.GetStatus() == Swim::IO::IoStatus::Succeeded);
		completionRan = true;
	});

	fixture.Io().Wait(full);
	SWIM_CHECK(completionRan);
	SWIM_CHECK_EQUAL(full.GetResult().FileSize, fixture.Payload().size());
	SWIM_CHECK(full.GetResult().GetSingleBuffer() == fixture.Payload());
}

SWIM_TEST("IO.AsyncIoService", "SingleRangeRead")
{
	ScopedIoFixture fixture;
	SWIM_REQUIRE(fixture.IsReady());

	auto range = fixture.Io().ReadRangeAsync(fixture.DataPath(), { 123, 777 });
	fixture.Io().Wait(range);

	SWIM_CHECK(range.GetStatus() == Swim::IO::IoStatus::Succeeded);
	SWIM_REQUIRE(range.GetResult().Chunks.size() == 1);
	CheckChunkEquals(range.GetResult().Chunks[0], fixture.Payload(), 123, 777);
}

SWIM_TEST("IO.AsyncIoService", "CoalescedBatchPreservesRequestedChunks")
{
	ScopedIoFixture fixture;
	SWIM_REQUIRE(fixture.IsReady());

	const std::vector<Swim::IO::IoReadRange> ranges = {
		{ 0, 64 },
		{ 64, 32 },
		{ 104, 24 },
		{ 2048, 128 }
	};

	Swim::IO::IoReadOptions batchOptions{};
	batchOptions.MaxCoalesceGapBytes = 8;
	auto batch = fixture.Io().ReadRangesAsync(fixture.DataPath(), ranges, batchOptions);
	fixture.Io().Wait(batch);

	SWIM_REQUIRE(batch.GetResult().Chunks.size() == ranges.size());
	for (std::size_t i = 0; i < ranges.size(); ++i)
	{
		CheckChunkEquals(batch.GetResult().Chunks[i], fixture.Payload(), ranges[i].Offset, ranges[i].Size);
	}
}

SWIM_TEST("IO.AsyncIoService", "ConcurrentRangeReads")
{
	ScopedIoFixture fixture;
	SWIM_REQUIRE(fixture.IsReady());

	std::vector<Swim::IO::ReadRequest> concurrent;
	for (std::uint64_t i = 0; i < 16; ++i)
	{
		concurrent.push_back(fixture.Io().ReadRangeAsync(fixture.DataPath(), { i * 128, 96 }));
	}

	for (std::size_t i = 0; i < concurrent.size(); ++i)
	{
		fixture.Io().Wait(concurrent[i]);
		SWIM_CHECK(concurrent[i].GetStatus() == Swim::IO::IoStatus::Succeeded);
		SWIM_REQUIRE(!concurrent[i].GetResult().Chunks.empty());
		CheckChunkEquals(concurrent[i].GetResult().Chunks[0], fixture.Payload(), static_cast<std::uint64_t>(i) * 128, 96);
	}
}

SWIM_TEST("IO.AsyncIoService", "MissingFileFailsWithAMessage")
{
	ScopedIoFixture fixture;
	SWIM_REQUIRE(fixture.IsReady());

	auto failed = fixture.Io().ReadFileAsync(fixture.Root() / "does-not-exist.bin");
	fixture.Io().Wait(failed);

	SWIM_CHECK(failed.GetStatus() == Swim::IO::IoStatus::Failed);
	SWIM_CHECK(!failed.GetErrorMessage().empty());
}

SWIM_TEST("IO.AsyncIoService", "CancellationBeforeDispatch")
{
	ScopedIoFixture fixture;
	SWIM_REQUIRE(fixture.IsReady());

	// Occupy the blocking lane so the request is guaranteed to still be queued
	// when cancellation is requested.
	std::atomic<bool> releaseBlockingLane{ false };
	auto blocker = fixture.Jobs().ScheduleBlocking([&]()
	{
		while (!releaseBlockingLane.load(std::memory_order_acquire))
		{
			std::this_thread::yield();
		}
	});

	auto cancelled = fixture.Io().ReadRangeAsync(fixture.DataPath(), { 0, 128 });
	cancelled.RequestCancel();
	releaseBlockingLane.store(true, std::memory_order_release);
	fixture.Jobs().Wait(blocker);
	fixture.Io().Wait(cancelled);

	SWIM_CHECK(cancelled.GetStatus() == Swim::IO::IoStatus::Cancelled);
	SWIM_CHECK(cancelled.IsCancellationRequested());
}

SWIM_TEST("IO.AsyncIoService", "BlockingReadOnlyMapping")
{
	ScopedIoFixture fixture;
	SWIM_REQUIRE(fixture.IsReady());

	Swim::Platform::MappedFile mapped = fixture.Io().MapFileReadOnlyBlocking(fixture.DataPath());
	SWIM_REQUIRE(mapped.IsOpen());
	SWIM_CHECK_EQUAL(mapped.Size(), fixture.Payload().size());
	SWIM_CHECK(mapped.Bytes()[123] == fixture.Payload()[123]);
	mapped.Close();
}

SWIM_TEST("IO.AsyncIoService", "ShutdownOrderIsDeterministic")
{
	ScopedIoFixture fixture;
	SWIM_REQUIRE(fixture.IsReady());

	fixture.Io().Shutdown();
	SWIM_CHECK(!fixture.Io().IsRunning());
	fixture.Jobs().Shutdown();
	SWIM_CHECK(!fixture.Jobs().IsRunning());
}
