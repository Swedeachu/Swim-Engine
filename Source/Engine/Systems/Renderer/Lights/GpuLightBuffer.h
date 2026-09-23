#pragma once
#include "Engine/Systems/Renderer/GpuScene/Internal/GpuDirtyRowSet.h"
#include "Engine/Systems/Renderer/Lights/GpuLightBufferDesc.h"
#include "Engine/Systems/Renderer/Lights/GpuLightGraphResources.h"
#include "Engine/Systems/Renderer/Lights/GpuLightRecord.h"
#include "Engine/Systems/Renderer/Lights/LightDesc.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraph.h"
#include "Engine/Systems/Renderer/Resources/GpuHandle.h"

#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace Swim::Render
{
	// The persistent GPU light buffer (critical-path item 63). Lights live in two
	// dense row ranges of one device-local GpuLightRecord buffer, directional rows
	// first and local (point/spot) rows from FirstLocalRow, so shaders and the
	// clustered assignment (item 64+) iterate [0, count) without holes; a small header
	// buffer carries the live counts.
	//
	// - Handles are generational and stable; the row behind a handle may move:
	//   Release swaps the last row of its range into the hole (both rows re-upload).
	//   GPU readers never see a stale row because uploads are graph copies ordered
	//   after earlier frames on the queue.
	// - Update re-encodes a light, moving it between ranges when its type changes.
	// - Import records one batched transfer pass for the changed rows (plus the header
	//   when counts changed). CommitUploads after a successful Execute; AbortUploads
	//   marks them dirty again.
	//
	// Owner thread, externally synchronized.
	class GpuLightBuffer
	{
	  public:
		// Throws std::invalid_argument for zero capacities.
		GpuLightBuffer(Rhi::Device& device, GpuLightBufferDesc desc);
		~GpuLightBuffer();
		GpuLightBuffer(const GpuLightBuffer&) = delete;
		GpuLightBuffer& operator=(const GpuLightBuffer&) = delete;

		// Empty when the light's range is full. Invalid descs throw std::invalid_argument.
		std::optional<GpuLightHandle> TryCreate(const LightDesc& desc);
		// As above, but a full range throws std::length_error.
		GpuLightHandle Create(const LightDesc& desc);
		// False for stale handles. Throws std::invalid_argument for invalid descs and
		// std::length_error when a type change targets a full range (the light keeps
		// its previous state).
		bool Update(GpuLightHandle light, const LightDesc& desc);
		bool Release(GpuLightHandle light);
		bool IsValid(GpuLightHandle light) const;
		const LightDesc* Find(GpuLightHandle light) const;
		// The light's current buffer row (it moves when other lights are released).
		std::optional<std::uint32_t> GetRow(GpuLightHandle light) const;

		GpuLightGraphResources Import(RenderGraph& graph);
		void CommitUploads();
		void AbortUploads();

		// The CPU mirror of the GPU contents (valid up to the header's counts).
		std::span<const GpuLightRecord> GetRecords() const { return mirror; }

		const GpuLightHeader& GetHeader() const { return header; }

		Rhi::Buffer& GetBuffer() const { return *buffer; }

		Rhi::Buffer& GetHeaderBuffer() const { return *headerBuffer; }

		GpuLightBufferStats GetStats() const;

	  private:
		struct Slot
		{
			std::uint32_t Generation = 0;
			bool Live = false;
			LightDesc Desc;
			std::uint32_t Row = 0;
		};

		Slot* Resolve(GpuLightHandle light);
		const Slot* Resolve(GpuLightHandle light) const;
		// Appends a record to its range; false when full.
		bool Insert(std::uint32_t slot, const GpuLightRecord& record, bool directional);
		void Remove(std::uint32_t slot);
		void WriteRow(std::uint32_t row, const GpuLightRecord& record, std::uint32_t owner);

		std::uint32_t directionalCapacity;
		std::uint32_t localCapacity;
		std::string name;
		std::unique_ptr<Rhi::Buffer> buffer;
		std::unique_ptr<Rhi::Buffer> headerBuffer;
		std::vector<Slot> slots;
		std::vector<std::uint32_t> freeSlots;
		std::vector<GpuLightRecord> mirror;
		std::vector<std::uint32_t> rowOwners; // Slot per row.
		GpuLightHeader header;
		Internal::GpuDirtyRowSet dirty;
		bool headerDirty = true;
		std::vector<std::uint32_t> recorded;
		bool recordedHeader = false;
		bool pending = false;
		std::uint32_t lastRows = 0;
		std::uint32_t lastRuns = 0;
		std::uint64_t lastBytes = 0;
		bool lastHeader = false;
	};
} // namespace Swim::Render
