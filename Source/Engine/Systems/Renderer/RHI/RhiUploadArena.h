#pragma once

#include "Engine/Systems/Renderer/RHI/RhiContracts.h"

#include <algorithm>
#include <cstring>
#include <optional>

namespace Swim::Rhi
{

	struct UploadArenaDesc
	{
		std::uint64_t Capacity = 0;
		BufferUsage Usage = BufferUsage::TransferSource;
		std::string_view DebugName = "Swim upload arena";
	};

	struct UploadSlice
	{
		Buffer* Resource = nullptr;
		std::uint64_t Offset = 0;
		std::span<std::byte> Bytes;
	};

	// Single host owner, fixed capacity, no hidden waits or growth. All slices
	// share one submission lifetime. Reset only after every GPU use completes
	// (or after discarding all unsubmitted uses). Reset invalidates old slices.
	class UploadArena
	{
	public:
		static std::unique_ptr<UploadArena> Create(Device& device, const UploadArenaDesc& desc)
		{
			constexpr BufferUsage allowed = BufferUsage::TransferSource | BufferUsage::Vertex |
				BufferUsage::Index | BufferUsage::Uniform | BufferUsage::Storage | BufferUsage::Indirect;
			if (desc.Capacity == 0 || desc.Capacity > std::numeric_limits<std::size_t>::max() ||
				desc.Usage == BufferUsage::None || (static_cast<std::uint32_t>(desc.Usage) & ~static_cast<std::uint32_t>(allowed)) != 0)
			{
				return nullptr;
			}

			// Four bytes covers buffer-copy and index/indirect offsets. Shader
			// buffer slices additionally satisfy the adapter's descriptor limits.
			std::uint64_t alignment = 4;
			const auto& caps = device.GetAdapterInfo().Capabilities;
			if (HasUsage(desc.Usage, BufferUsage::Uniform))
			{
				alignment = std::max(alignment, caps.MinUniformBufferOffsetAlignment);
			}
			if (HasUsage(desc.Usage, BufferUsage::Storage))
			{
				alignment = std::max(alignment, caps.MinStorageBufferOffsetAlignment);
			}
			if (!IsPowerOfTwo(alignment))
			{
				return nullptr;
			}

			auto buffer = device.CreateBuffer({ desc.Capacity, desc.Usage,
				MemoryPreference::CpuToGpu, desc.DebugName, true });
			if (!buffer || buffer->GetMappedWriteSpan().size() != desc.Capacity)
			{
				return nullptr;
			}
			return std::unique_ptr<UploadArena>(new UploadArena(std::move(buffer), alignment));
		}

		UploadArena(const UploadArena&) = delete;
		UploadArena& operator=(const UploadArena&) = delete;

		// Alignment is a buffer offset constraint, not a typed CPU-pointer promise.
		// Empty optional means capacity exhaustion; invalid sizes/alignment throw.
		// Failed allocation leaves the cursor and all earlier bytes unchanged.
		std::optional<UploadSlice> Allocate(std::uint64_t size, std::uint64_t alignment = 4)
		{
			if (size == 0 || !IsPowerOfTwo(alignment))
			{
				throw std::invalid_argument("Upload allocation requires nonzero size and power-of-two alignment");
			}
			const auto mapped = buffer->GetMappedWriteSpan();
			alignment = std::max(alignment, minimumAlignment);
			const std::uint64_t padding = (alignment - (used & (alignment - 1))) & (alignment - 1);
			const std::uint64_t remaining = GetCapacity() - used;
			if (padding > remaining || size > remaining - padding)
			{
				return std::nullopt;
			}
			const std::uint64_t offset = used + padding;
			const auto bytes = mapped.subspan(static_cast<std::size_t>(offset), static_cast<std::size_t>(size));
			used = offset + size;
			return UploadSlice{ buffer.get(), offset, bytes };
		}

		std::optional<UploadSlice> Write(std::span<const std::byte> data, std::uint64_t alignment = 4)
		{
			auto slice = Allocate(data.size(), alignment);
			if (slice)
			{
				std::memcpy(slice->Bytes.data(), data.data(), data.size());
			}
			return slice;
		}

		// Flush the used prefix on every call: callers may have written through a
		// previously returned span since the last flush. Padding is harmless.
		void Flush()
		{
			buffer->FlushMappedWrites(0, used);
		}

		void Reset()
		{
			used = 0;
		}

		std::uint64_t GetUsedBytes() const
		{
			return used;
		}

		std::uint64_t GetCapacity() const
		{
			return buffer->GetDesc().Size;
		}

	private:
		UploadArena(std::unique_ptr<Buffer> buffer, std::uint64_t minimumAlignment)
			: buffer(std::move(buffer)), minimumAlignment(minimumAlignment)
		{
		}

		static bool HasUsage(BufferUsage value, BufferUsage mask)
		{
			return (static_cast<std::uint32_t>(value) & static_cast<std::uint32_t>(mask)) != 0;
		}

		static bool IsPowerOfTwo(std::uint64_t value)
		{
			return value != 0 && (value & (value - 1)) == 0;
		}

		std::unique_ptr<Buffer> buffer;
		std::uint64_t minimumAlignment = 4;
		std::uint64_t used = 0;
	};

} // namespace Swim::Rhi
