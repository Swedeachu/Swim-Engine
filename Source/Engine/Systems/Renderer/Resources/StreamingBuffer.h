#pragma once
#include "Engine/Systems/Renderer/RenderGraph/RenderGraph.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace Swim::Render
{

	struct StreamingBufferDesc
	{
		std::uint64_t Capacity = 0;
		// Every range starts and ends on this many bytes (a power of two, at least 4).
		std::uint64_t Alignment = 16;
		// Usage on top of Storage | TransferSource | TransferDestination.
		Rhi::BufferUsage Usage = Rhi::BufferUsage::None;
		std::string DebugName = "Streaming buffer";
	};

	// A range of a StreamingBuffer. Its offset can change when the buffer defragments.
	// Default-constructed ranges are never valid.
	struct StreamingRange
	{
		std::uint32_t Index = 0xFFFFFFFFu;
		std::uint32_t Generation = 0;

		bool operator==(const StreamingRange&) const = default;
	};

	struct StreamingBufferStats
	{
		std::uint64_t Capacity = 0;
		std::uint64_t AllocatedBytes = 0;
		std::uint64_t LargestFreeRange = 0;
		std::uint32_t Ranges = 0;
		std::uint32_t FreeRanges = 0;
		std::uint64_t PendingUploadBytes = 0;
		std::uint64_t UploadedBytes = 0; // Last Import.
		std::uint64_t MovedBytes = 0;	 // Last Import (defragmentation).
		std::uint32_t Moves = 0;		 // Last Import.
	};

	// One large persistent device-local buffer that a caller suballocates and streams
	// into. Allocate and Free manage ranges on the CPU (best fit, coalescing). Write
	// copies the bytes and returns at once; Import, once per frame, records the queued
	// writes as graph copy passes up to an upload budget (at least one write per frame,
	// so oversized writes still progress) and, while the free space is split, moves the
	// highest ranges down into holes within a move budget. Moves are reported through the
	// relocation callback before Import returns, so the caller updates every offset it
	// gives the GPU for that frame. All work is on the graph's queue, ordered against the
	// frame's other passes by the graph, so a freed range may be reused at once.
	//
	// When the graph is abandoned instead of executed, its writes go back to the queue
	// (IsUploaded turns false again) and its moves stay in effect on the CPU side: the next
	// Import copies those ranges first, before anything else in that frame can reuse the
	// space they left, so offsets the caller already updated stay right.
	class StreamingBuffer
	{

	  public:

		using RelocationCallback = std::function<void(StreamingRange range, std::uint64_t oldOffset, std::uint64_t newOffset)>;

		StreamingBuffer(Rhi::Device& device, StreamingBufferDesc desc);

		StreamingBuffer(const StreamingBuffer&) = delete;

		StreamingBuffer& operator=(const StreamingBuffer&) = delete;

		// Empty when no free range is large enough (defragmentation may make room later).
		std::optional<StreamingRange> Allocate(std::uint64_t size);

		// Pending writes to the range are dropped. A stale range throws.
		void Free(StreamingRange range);

		bool IsValid(StreamingRange range) const;

		std::uint64_t GetOffset(StreamingRange range) const;

		std::uint64_t GetSize(StreamingRange range) const;

		// Queues bytes for [offset, offset + size) of the range (copied now).
		void Write(StreamingRange range, std::span<const std::byte> bytes, std::uint64_t offset = 0);

		// No queued writes for the range: its contents are on the GPU for every pass
		// recorded after the Import that sent the last one.
		bool IsUploaded(StreamingRange range) const;

		void SetRelocationCallback(RelocationCallback callback) { relocated = std::move(callback); }

		// Bytes moved per Import while the free space is fragmented; 0 turns moves off.
		void SetDefragmentBudget(std::uint64_t bytes) { defragmentBudget = bytes; }

		// Imports the buffer (ShaderRead outside this frame's copies) and records this
		// frame's moves and uploads. Call once per frame before the passes that read it.
		GraphBuffer Import(RenderGraph& graph, std::uint64_t uploadBudget);

		Rhi::Buffer& GetBuffer() const { return *buffer; }

		const StreamingBufferStats& GetStats() const { return stats; }

	  private:

		struct Slot
		{
			std::uint64_t Offset = 0;
			std::uint64_t Size = 0;
			std::uint32_t Generation = 0;
			bool Live = false;
		};

		struct PendingWrite
		{
			StreamingRange Range;
			std::uint64_t Offset = 0;
			std::vector<std::byte> Bytes;
		};

		struct Move
		{
			std::uint64_t From = 0;
			std::uint64_t To = 0;
			std::uint64_t Size = 0;
		};

		// A move recorded into a graph that was abandoned: the next Import makes the copy.
		struct Carried
		{
			StreamingRange Range;
			Move Copy;
		};

		const Slot& Get(StreamingRange range) const;

		std::optional<std::uint64_t> TakeFree(std::uint64_t size);

		void ReleaseFree(std::uint64_t offset, std::uint64_t size);

		void Defragment(std::vector<Move>& moves);

		StreamingBufferDesc desc;
		std::unique_ptr<Rhi::Buffer> buffer;
		std::vector<Slot> slots;
		std::vector<std::uint32_t> freeSlots;
		std::map<std::uint64_t, std::uint64_t> freeByOffset;
		std::multimap<std::uint64_t, std::uint64_t> freeBySize;
		std::map<std::uint64_t, std::uint32_t> rangesByOffset;
		std::deque<PendingWrite> pending;
		std::vector<Carried> carried;
		RelocationCallback relocated;
		std::uint64_t defragmentBudget = 4ull << 20;
		StreamingBufferStats stats;

	};

} // namespace Swim::Render
