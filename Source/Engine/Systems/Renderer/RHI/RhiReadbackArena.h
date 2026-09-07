#pragma once

#include "Engine/Systems/Renderer/RHI/RhiContracts.h"

#include <algorithm>
#include <cstring>
#include <optional>

namespace Swim::Rhi
{

	struct ReadbackArenaDesc
	{
		std::uint64_t Capacity = 0;
		std::string_view DebugName = "Swim readback arena";
	};

	enum class ReadbackStatus : std::uint8_t
	{
		NotSubmitted,
		NotReady,
		Ready,
	};

	class ReadbackSlice
	{
	public:
		ReadbackSlice() = default;

		Buffer& GetBuffer() const
		{
			const auto current = batch.lock();
			if (!current)
			{
				throw std::invalid_argument("Readback slice is empty or expired");
			}
			return *current->Resource;
		}

		std::uint64_t GetOffset() const { return offset; }
		std::uint64_t GetSize() const { return size; }

	private:
		friend class ReadbackArena;

		struct Batch
		{
			Buffer* Resource = nullptr;
		};

		ReadbackSlice(const std::shared_ptr<Batch>& batch, std::uint64_t offset, std::uint64_t size)
			: batch(batch), offset(offset), size(size)
		{
		}

		std::weak_ptr<Batch> batch;
		std::uint64_t offset = 0;
		std::uint64_t size = 0;
	};

	// One externally synchronized host owner and one completion lifetime per batch.
	// Submit through FrameContextRing, listing this arena alongside its commands.
	// Results survive frame reuse; only explicit TryReset discards them. No method
	// waits for the GPU. Returned spans remain valid only until reset/destruction.
	class ReadbackArena
	{
	public:
		static std::unique_ptr<ReadbackArena> Create(Device& device, const ReadbackArenaDesc& desc)
		{
			if (desc.Capacity == 0 || desc.Capacity > std::numeric_limits<std::size_t>::max())
			{
				return nullptr;
			}
			auto buffer = device.CreateBuffer({ desc.Capacity, BufferUsage::TransferDestination,
				MemoryPreference::GpuToCpu, desc.DebugName, true });
			if (!buffer || buffer->GetMappedReadSpan().size() != desc.Capacity)
			{
				return nullptr;
			}
			return std::unique_ptr<ReadbackArena>(new ReadbackArena(std::move(buffer)));
		}

		ReadbackArena(const ReadbackArena&) = delete;
		ReadbackArena& operator=(const ReadbackArena&) = delete;

		std::optional<ReadbackSlice> Allocate(std::uint64_t size, std::uint64_t alignment = 4)
		{
			if (timeline)
			{
				throw std::logic_error("Reset the completed readback batch before allocating again");
			}
			if (size == 0 || alignment == 0 || (alignment & (alignment - 1)) != 0)
			{
				throw std::invalid_argument("Readback allocation requires nonzero size and power-of-two alignment");
			}
			buffer->GetMappedReadSpan(); // Recheck backend health without reading memory.
			alignment = std::max(alignment, std::uint64_t(4));
			const std::uint64_t padding = (alignment - (used & (alignment - 1))) & (alignment - 1);
			const std::uint64_t remaining = GetCapacity() - used;
			if (padding > remaining || size > remaining - padding)
			{
				return std::nullopt;
			}
			const std::uint64_t offset = used + padding;
			used = offset + size;
			return ReadbackSlice(batch, offset, size);
		}

		// Always clears the span on pending/error. Invalid/foreign/stale slices throw.
		// GPU copies and a final HostRead transition must precede the frame signal.
		ReadbackStatus TryGetData(const ReadbackSlice& slice, std::span<const std::byte>& data)
		{
			data = {};
			if (slice.batch.lock() != batch)
			{
				throw std::invalid_argument("Readback slice does not belong to this live batch");
			}
			buffer->GetMappedReadSpan();
			if (!timeline)
			{
				return ReadbackStatus::NotSubmitted;
			}
			if (timeline->GetCompletedValue() < completionValue)
			{
				return ReadbackStatus::NotReady;
			}
			if (!invalidated)
			{
				buffer->InvalidateMappedReads(0, used);
			}
			const auto mapped = buffer->GetMappedReadSpan();
			data = mapped.subspan(static_cast<std::size_t>(slice.offset), static_cast<std::size_t>(slice.size));
			invalidated = true;
			return ReadbackStatus::Ready;
		}

		// Destination remains untouched unless the entire requested slice is ready.
		ReadbackStatus TryRead(const ReadbackSlice& slice, std::span<std::byte> destination)
		{
			if (destination.size() != slice.GetSize())
			{
				throw std::invalid_argument("Readback destination must match the slice size");
			}
			std::span<const std::byte> data;
			const auto status = TryGetData(slice, data);
			if (status == ReadbackStatus::Ready)
			{
				std::memcpy(destination.data(), data.data(), data.size());
			}
			return status;
		}

		// Explicitly discard completed results, or an unsubmitted batch whose
		// recorded commands the caller has discarded. In-flight reset returns false.
		bool TryReset()
		{
			buffer->GetMappedReadSpan();
			if (timeline && timeline->GetCompletedValue() < completionValue)
			{
				return false;
			}
			auto replacement = std::make_shared<ReadbackSlice::Batch>();
			replacement->Resource = buffer.get();
			batch = std::move(replacement);
			timeline.reset();
			completionValue = 0;
			used = 0;
			invalidated = false;
			return true;
		}

		std::uint64_t GetCapacity() const { return buffer->GetDesc().Size; }
		std::uint64_t GetUsedBytes() const { return used; }

	private:
		friend class FrameContextRing;

		explicit ReadbackArena(std::shared_ptr<Buffer> buffer)
			: buffer(std::move(buffer)), batch(std::make_shared<ReadbackSlice::Batch>())
		{
			batch->Resource = this->buffer.get();
		}

		void ValidateSubmission() const
		{
			if (timeline || used == 0)
			{
				throw std::logic_error("Frame submission requires a nonempty unsubmitted readback batch");
			}
			buffer->GetMappedReadSpan();
		}

		void CommitSubmission(const std::shared_ptr<Timeline>& submittedTimeline, std::uint64_t value) noexcept
		{
			timeline = submittedTimeline;
			completionValue = value;
		}

		std::shared_ptr<Buffer> buffer;
		std::shared_ptr<ReadbackSlice::Batch> batch;
		std::shared_ptr<Timeline> timeline;
		std::uint64_t completionValue = 0;
		std::uint64_t used = 0;
		bool invalidated = false;
	};

} // namespace Swim::Rhi
