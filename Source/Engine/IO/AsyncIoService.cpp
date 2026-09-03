#include "AsyncIoService.h"

#include "Engine/Jobs/JobSystem.h"
#include "Engine/Platform/FileSystem.h"

#include <algorithm>
#include <deque>
#include <exception>
#include <fstream>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

namespace Swim::IO
{

	namespace Detail
	{

		struct IoRequestState
		{
			std::atomic<IoStatus> Status{ IoStatus::Queued };
			std::atomic<bool> CancelRequested{ false };
			std::filesystem::path Path;
			std::vector<IoReadRange> Ranges;
			bool FullFile = false;
			IoReadOptions Options{};
			IoReadResult Result;
			std::string ErrorMessage;
			AsyncIoService::CompletionCallback Completion;
			Jobs::JobHandle Work;
		};

	}

	namespace
	{

		struct IndexedRange
		{
			IoReadRange Range{};
			std::size_t OriginalIndex = 0;
		};

		bool IsTerminal(IoStatus status)
		{
			return status == IoStatus::Succeeded
				|| status == IoStatus::Failed
				|| status == IoStatus::Cancelled;
		}

		Jobs::JobPriority ToJobPriority(IoPriority priority)
		{
			switch (priority)
			{
				case IoPriority::High:
					return Jobs::JobPriority::High;
				case IoPriority::Low:
					return Jobs::JobPriority::Low;
				case IoPriority::Normal:
				default:
					return Jobs::JobPriority::Normal;
			}
		}

		std::uint64_t CheckedEnd(IoReadRange range)
		{
			if (range.Size > std::numeric_limits<std::uint64_t>::max() - range.Offset)
			{
				throw std::out_of_range("IO read range overflows uint64_t");
			}
			return range.Offset + range.Size;
		}

		std::uint64_t QueryFileSize(std::ifstream& file, const std::filesystem::path& path)
		{
			file.seekg(0, std::ios::end);
			const std::streamoff end = file.tellg();
			if (end < 0)
			{
				throw std::runtime_error("Failed to query file size: " + path.string());
			}
			file.seekg(0, std::ios::beg);
			return static_cast<std::uint64_t>(end);
		}

		void ReadExact(
			std::ifstream& file,
			const std::filesystem::path& path,
			std::uint64_t offset,
			std::span<std::byte> destination
		)
		{
			if (destination.empty())
			{
				return;
			}
			if (offset > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max()))
			{
				throw std::out_of_range("IO read offset exceeds streamoff range: " + path.string());
			}
			if (destination.size() > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max()))
			{
				throw std::out_of_range("IO read size exceeds streamsize range: " + path.string());
			}

			file.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
			if (!file)
			{
				throw std::runtime_error("Failed to seek file: " + path.string());
			}

			if (!file.read(reinterpret_cast<char*>(destination.data()), static_cast<std::streamsize>(destination.size())))
			{
				throw std::runtime_error("Failed to read file range: " + path.string());
			}
		}

		IoReadResult ReadFullFileBlocking(Platform::FileSystem& fileSystem, const std::filesystem::path& path)
		{
			IoReadResult result{};
			result.Path = path;
			IoReadChunk chunk{};
			chunk.Range.Offset = 0;
			chunk.Bytes = fileSystem.ReadFileBlocking(path);
			chunk.Range.Size = static_cast<std::uint64_t>(chunk.Bytes.size());
			result.FileSize = chunk.Range.Size;
			result.Chunks.push_back(std::move(chunk));
			return result;
		}

		IoReadResult ReadRangesBlockingImpl(
			const std::filesystem::path& path,
			std::span<const IoReadRange> ranges,
			std::uint64_t maxCoalesceGapBytes
		)
		{
			if (path.empty())
			{
				throw std::invalid_argument("ReadRangesBlocking requires a non-empty path");
			}
			if (ranges.empty())
			{
				throw std::invalid_argument("ReadRangesBlocking requires at least one range");
			}

			std::ifstream file(path, std::ios::binary);
			if (!file)
			{
				throw std::runtime_error("Failed to open file: " + path.string());
			}

			IoReadResult result{};
			result.Path = path;
			result.FileSize = QueryFileSize(file, path);
			result.Chunks.resize(ranges.size());

			std::vector<IndexedRange> sorted;
			sorted.reserve(ranges.size());
			for (std::size_t i = 0; i < ranges.size(); ++i)
			{
				const std::uint64_t end = CheckedEnd(ranges[i]);
				if (end > result.FileSize)
				{
					throw std::out_of_range("IO read range exceeds file size: " + path.string());
				}
				if (ranges[i].Size > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()))
				{
					throw std::out_of_range("IO read range exceeds size_t range: " + path.string());
				}
				result.Chunks[i].Range = ranges[i];
				result.Chunks[i].Bytes.resize(static_cast<std::size_t>(ranges[i].Size));
				sorted.push_back({ ranges[i], i });
			}

			std::sort(sorted.begin(), sorted.end(), [](const IndexedRange& left, const IndexedRange& right)
			{
				if (left.Range.Offset != right.Range.Offset)
				{
					return left.Range.Offset < right.Range.Offset;
				}
				return left.Range.Size < right.Range.Size;
			});

			std::size_t beginIndex = 0;
			while (beginIndex < sorted.size())
			{
				const std::uint64_t mergedBegin = sorted[beginIndex].Range.Offset;
				std::uint64_t mergedEnd = CheckedEnd(sorted[beginIndex].Range);
				std::size_t endIndex = beginIndex + 1;

				while (endIndex < sorted.size())
				{
					const std::uint64_t nextBegin = sorted[endIndex].Range.Offset;
					const std::uint64_t nextEnd = CheckedEnd(sorted[endIndex].Range);
					const bool overlaps = nextBegin <= mergedEnd;
					const bool withinGap = !overlaps
						&& nextBegin - mergedEnd <= maxCoalesceGapBytes;
					if (!overlaps && !withinGap)
					{
						break;
					}
					mergedEnd = std::max(mergedEnd, nextEnd);
					++endIndex;
				}

				const std::uint64_t mergedSize64 = mergedEnd - mergedBegin;
				if (mergedSize64 > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()))
				{
					throw std::out_of_range("Coalesced IO read exceeds size_t range: " + path.string());
				}
				std::vector<std::byte> merged(static_cast<std::size_t>(mergedSize64));
				ReadExact(file, path, mergedBegin, merged);

				for (std::size_t i = beginIndex; i < endIndex; ++i)
				{
					const IndexedRange& indexed = sorted[i];
					IoReadChunk& chunk = result.Chunks[indexed.OriginalIndex];
					const std::uint64_t relative64 = indexed.Range.Offset - mergedBegin;
					const std::size_t relative = static_cast<std::size_t>(relative64);
					std::copy_n(merged.begin() + relative, chunk.Bytes.size(), chunk.Bytes.begin());
				}

				beginIndex = endIndex;
			}

			return result;
		}

	}

	struct AsyncIoService::Impl
	{
		Platform::FileSystem* FileSystem = nullptr;
		Jobs::JobSystem* Jobs = nullptr;
		std::atomic<bool> Running{ false };
		std::thread::id OwnerThread{};
		std::mutex RequestsMutex;
		std::vector<std::shared_ptr<Detail::IoRequestState>> Requests;
		std::mutex CompletionMutex;
		std::deque<std::shared_ptr<Detail::IoRequestState>> Completions;

		void RequireRunning(const char* operation) const
		{
			if (!Running.load(std::memory_order_acquire))
			{
				throw std::logic_error(std::string(operation) + " requires an initialized AsyncIoService");
			}
		}

		void RequireOwnerThread(const char* operation) const
		{
			if (OwnerThread != std::this_thread::get_id())
			{
				throw std::logic_error(std::string(operation) + " must run on the AsyncIoService owner thread");
			}
		}

		void Track(const std::shared_ptr<Detail::IoRequestState>& request)
		{
			std::lock_guard lock(RequestsMutex);
			Requests.push_back(request);
		}

		void QueueCompletion(const std::shared_ptr<Detail::IoRequestState>& request)
		{
			std::lock_guard lock(CompletionMutex);
			Completions.push_back(request);
		}

		std::vector<std::shared_ptr<Detail::IoRequestState>> SnapshotRequests()
		{
			std::lock_guard lock(RequestsMutex);
			return Requests;
		}

		void RemoveTracked(const std::shared_ptr<Detail::IoRequestState>& request)
		{
			std::lock_guard lock(RequestsMutex);
			std::erase(Requests, request);
		}
	};

	const std::vector<std::byte>& IoReadResult::GetSingleBuffer() const
	{
		if (Chunks.size() != 1)
		{
			throw std::logic_error("IoReadResult does not contain exactly one read chunk");
		}
		return Chunks.front().Bytes;
	}

	bool ReadRequest::IsValid() const
	{
		return static_cast<bool>(state);
	}

	IoStatus ReadRequest::GetStatus() const
	{
		return state ? state->Status.load(std::memory_order_acquire) : IoStatus::Invalid;
	}

	bool ReadRequest::IsComplete() const
	{
		return IsTerminal(GetStatus());
	}

	bool ReadRequest::IsCancellationRequested() const
	{
		return state && state->CancelRequested.load(std::memory_order_acquire);
	}

	void ReadRequest::RequestCancel() const
	{
		if (state && !IsTerminal(state->Status.load(std::memory_order_acquire)))
		{
			state->CancelRequested.store(true, std::memory_order_release);
		}
	}

	const IoReadResult& ReadRequest::GetResult() const
	{
		if (!state || GetStatus() != IoStatus::Succeeded)
		{
			throw std::logic_error("ReadRequest result is available only after a successful read");
		}
		return state->Result;
	}

	const std::string& ReadRequest::GetErrorMessage() const
	{
		if (!state || !IsComplete())
		{
			throw std::logic_error("ReadRequest error state is available only after completion");
		}
		return state->ErrorMessage;
	}

	AsyncIoService::AsyncIoService()
		: impl(std::make_unique<Impl>())
	{
	}

	AsyncIoService::~AsyncIoService()
	{
		if (impl && impl->Running.load(std::memory_order_acquire))
		{
			try
			{
				Shutdown(IoShutdownMode::Drain);
			}
			catch (...)
			{
				std::terminate();
			}
		}
	}

	bool AsyncIoService::Initialize(Platform::FileSystem& fileSystem, Jobs::JobSystem& jobs)
	{
		if (impl->Running.load(std::memory_order_acquire))
		{
			return true;
		}
		if (!jobs.IsRunning() || jobs.GetBlockingThreadCount() == 0)
		{
			return false;
		}

		impl->FileSystem = &fileSystem;
		impl->Jobs = &jobs;
		impl->OwnerThread = std::this_thread::get_id();
		impl->Running.store(true, std::memory_order_release);
		return true;
	}

	void AsyncIoService::Shutdown(IoShutdownMode mode)
	{
		if (!impl || !impl->Running.load(std::memory_order_acquire))
		{
			return;
		}
		impl->RequireOwnerThread("Shutdown");
		if (!impl->Running.exchange(false, std::memory_order_acq_rel))
		{
			return;
		}

		const auto requests = impl->SnapshotRequests();
		if (mode == IoShutdownMode::CancelPending)
		{
			for (const auto& request : requests)
			{
				if (!IsTerminal(request->Status.load(std::memory_order_acquire)))
				{
					request->CancelRequested.store(true, std::memory_order_release);
				}
			}
		}

		if (impl->Jobs && impl->Jobs->IsRunning())
		{
			for (const auto& request : requests)
			{
				if (request->Work && request->Work.IsSubmitted() && !request->Work.IsComplete())
				{
					impl->Jobs->Wait(request->Work);
				}
			}
		}

		PumpCompletions();

		{
			std::lock_guard lock(impl->RequestsMutex);
			impl->Requests.clear();
		}
		{
			std::lock_guard lock(impl->CompletionMutex);
			impl->Completions.clear();
		}

		impl->FileSystem = nullptr;
		impl->Jobs = nullptr;
		impl->OwnerThread = {};
	}

	bool AsyncIoService::IsRunning() const
	{
		return impl && impl->Running.load(std::memory_order_acquire);
	}

	ReadRequest AsyncIoService::ReadFileAsync(
		const std::filesystem::path& path,
		const IoReadOptions& options,
		CompletionCallback completion
	)
	{
		return ScheduleRead(path, {}, true, options, std::move(completion));
	}

	ReadRequest AsyncIoService::ReadRangeAsync(
		const std::filesystem::path& path,
		IoReadRange range,
		const IoReadOptions& options,
		CompletionCallback completion
	)
	{
		std::vector<IoReadRange> ranges;
		ranges.push_back(range);
		return ScheduleRead(path, std::move(ranges), false, options, std::move(completion));
	}

	ReadRequest AsyncIoService::ReadRangesAsync(
		const std::filesystem::path& path,
		std::span<const IoReadRange> ranges,
		const IoReadOptions& options,
		CompletionCallback completion
	)
	{
		if (ranges.empty())
		{
			throw std::invalid_argument("ReadRangesAsync requires at least one range");
		}
		return ScheduleRead(
			path,
			std::vector<IoReadRange>(ranges.begin(), ranges.end()),
			false,
			options,
			std::move(completion)
		);
	}

	ReadRequest AsyncIoService::ScheduleRead(
		const std::filesystem::path& path,
		std::vector<IoReadRange> ranges,
		bool fullFile,
		const IoReadOptions& options,
		CompletionCallback completion
	)
	{
		impl->RequireRunning("ScheduleRead");
		if (path.empty())
		{
			throw std::invalid_argument("ScheduleRead requires a non-empty path");
		}

		auto request = std::make_shared<Detail::IoRequestState>();
		request->Path = path;
		request->Ranges = std::move(ranges);
		request->FullFile = fullFile;
		request->Options = options;
		request->Completion = std::move(completion);
		impl->Track(request);

		Impl* service = impl.get();
		request->Work = impl->Jobs->ScheduleBlocking([service, request]()
		{
			if (request->CancelRequested.load(std::memory_order_acquire))
			{
				request->ErrorMessage = "IO request cancelled before read";
				request->Status.store(IoStatus::Cancelled, std::memory_order_release);
				service->QueueCompletion(request);
				return;
			}

			request->Status.store(IoStatus::Reading, std::memory_order_release);
			try
			{
				if (request->FullFile)
				{
					request->Result = ReadFullFileBlocking(*service->FileSystem, request->Path);
				}
				else
				{
					request->Result = ReadRangesBlockingImpl(
						request->Path,
						request->Ranges,
						request->Options.MaxCoalesceGapBytes
					);
				}

				if (request->CancelRequested.load(std::memory_order_acquire))
				{
					request->Result = {};
					request->ErrorMessage = "IO request cancelled while read was in progress";
					request->Status.store(IoStatus::Cancelled, std::memory_order_release);
				}
				else
				{
					request->Status.store(IoStatus::Succeeded, std::memory_order_release);
				}
			}
			catch (const std::exception& error)
			{
				request->Result = {};
				request->ErrorMessage = error.what();
				request->Status.store(
					request->CancelRequested.load(std::memory_order_acquire) ? IoStatus::Cancelled : IoStatus::Failed,
					std::memory_order_release
				);
			}
			catch (...)
			{
				request->Result = {};
				request->ErrorMessage = "Unknown IO read failure";
				request->Status.store(
					request->CancelRequested.load(std::memory_order_acquire) ? IoStatus::Cancelled : IoStatus::Failed,
					std::memory_order_release
				);
			}

			service->QueueCompletion(request);
		}, ToJobPriority(options.Priority));

		return ReadRequest(std::move(request));
	}

	std::size_t AsyncIoService::PumpCompletions(std::size_t maxCompletions)
	{
		if (!impl)
		{
			return 0;
		}
		impl->RequireOwnerThread("PumpCompletions");

		std::vector<std::shared_ptr<Detail::IoRequestState>> ready;
		{
			std::lock_guard lock(impl->CompletionMutex);
			const std::size_t count = std::min(maxCompletions, impl->Completions.size());
			ready.reserve(count);
			for (std::size_t i = 0; i < count; ++i)
			{
				ready.push_back(std::move(impl->Completions.front()));
				impl->Completions.pop_front();
			}
		}

		for (const auto& requestState : ready)
		{
			impl->RemoveTracked(requestState);
			if (requestState->Completion)
			{
				const ReadRequest request(requestState);
				requestState->Completion(request);
			}
		}
		return ready.size();
	}

	IoReadResult AsyncIoService::ReadFileBlocking(const std::filesystem::path& path) const
	{
		if (!impl || !impl->FileSystem)
		{
			throw std::logic_error("ReadFileBlocking requires an initialized AsyncIoService");
		}
		return ReadFullFileBlocking(*impl->FileSystem, path);
	}

	IoReadResult AsyncIoService::ReadRangeBlocking(const std::filesystem::path& path, IoReadRange range) const
	{
		return ReadRangesBlocking(path, std::span<const IoReadRange>(&range, 1));
	}

	IoReadResult AsyncIoService::ReadRangesBlocking(
		const std::filesystem::path& path,
		std::span<const IoReadRange> ranges,
		std::uint64_t maxCoalesceGapBytes
	) const
	{
		if (!impl || !impl->FileSystem)
		{
			throw std::logic_error("ReadRangesBlocking requires an initialized AsyncIoService");
		}
		return ReadRangesBlockingImpl(path, ranges, maxCoalesceGapBytes);
	}

	Platform::MappedFile AsyncIoService::MapFileReadOnlyBlocking(const std::filesystem::path& path) const
	{
		if (!impl || !impl->FileSystem)
		{
			throw std::logic_error("MapFileReadOnlyBlocking requires an initialized AsyncIoService");
		}
		return impl->FileSystem->MapFileReadOnly(path);
	}

	void AsyncIoService::Wait(const ReadRequest& request)
	{
		if (!request.state)
		{
			throw std::invalid_argument("Wait requires a valid ReadRequest");
		}
		if (!impl || !impl->Jobs || !impl->Jobs->IsRunning())
		{
			throw std::logic_error("Wait requires an initialized AsyncIoService and JobSystem");
		}
		if (!request.state->Work || !request.state->Work.IsSubmitted())
		{
			throw std::logic_error("ReadRequest has no submitted IO work");
		}

		impl->Jobs->Wait(request.state->Work);
		if (impl->OwnerThread == std::this_thread::get_id())
		{
			PumpCompletions();
		}
	}

}
