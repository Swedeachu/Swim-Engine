#include "Engine/Systems/Renderer/Geometry/GeometryHeap.h"

#include <algorithm>
#include <numeric>

namespace Swim::Render
{
	namespace
	{
		using Stream = Internal::GeometryStream;

		Rhi::BufferUsage PageUsage(Stream stream)
		{
			constexpr auto common = Rhi::BufferUsage::Storage | Rhi::BufferUsage::TransferDestination | Rhi::BufferUsage::TransferSource;
			switch (stream)
			{
			case Stream::Vertex:
				return common | Rhi::BufferUsage::Vertex;
			case Stream::Index:
				return common | Rhi::BufferUsage::Index;
			default:
				return common;
			}
		}

		bool IsComplete(const Rhi::TimelinePoint& point)
		{
			return !point.Semaphore || point.Semaphore->GetCompletedValue() >= point.Value;
		}

		Internal::GeometryPayload Copy(std::span<const std::byte> bytes)
		{
			return bytes.empty() ? nullptr : std::make_shared<const std::vector<std::byte>>(bytes.begin(), bytes.end());
		}

		std::uint64_t PayloadBytes(const Internal::GeometryMeshRecord& record)
		{
			std::uint64_t total = 0;
			for (const auto* payload : { &record.VertexBytes, &record.IndexBytes, &record.MeshletBytes })
			{
				total += *payload ? (*payload)->size() : 0;
			}
			return total;
		}
	} // namespace

	GeometryHeap::GeometryHeap(Rhi::Device& device, const GeometryHeapDesc& desc)
		: device(device), desc(desc), name(desc.DebugName.empty() ? "GeometryHeap" : desc.DebugName),
		  meshes({ desc.MaxMeshes ? desc.MaxMeshes : 1, "GeometryHeap meshes" })
	{
		for (auto size : { desc.VertexPageSize, desc.IndexPageSize, desc.MeshletPageSize })
		{
			if (!size || size > UINT32_MAX)
			{
				throw std::invalid_argument("GeometryHeap page sizes must be between 1 byte and 4 GiB");
			}
		}
		if (!desc.MaxMeshes || !desc.MaxSubmeshes || !desc.MaxPages || desc.MaxPages == GpuMeshMetadata::InvalidPage ||
			desc.MaxSubmeshes > std::uint32_t(INT32_MAX))
		{
			throw std::invalid_argument("GeometryHeap needs mesh slots, submesh rows and pages");
		}

		const auto metadataName = name + " metadata";
		metadataBuffer = device.CreateBuffer({ std::uint64_t(desc.MaxMeshes) * sizeof(GpuMeshMetadata),
			Rhi::BufferUsage::Storage | Rhi::BufferUsage::TransferDestination | Rhi::BufferUsage::TransferSource,
			Rhi::MemoryPreference::DeviceLocal, metadataName });
		if (!metadataBuffer)
		{
			throw std::runtime_error("GeometryHeap metadata buffer allocation failed");
		}
		const auto submeshName = name + " submeshes";
		submeshBuffer = device.CreateBuffer({ std::uint64_t(desc.MaxSubmeshes) * sizeof(GpuSubmeshRecord),
			Rhi::BufferUsage::Storage | Rhi::BufferUsage::TransferDestination | Rhi::BufferUsage::TransferSource,
			Rhi::MemoryPreference::DeviceLocal, submeshName });
		if (!submeshBuffer)
		{
			throw std::runtime_error("GeometryHeap submesh buffer allocation failed");
		}
		submeshAllocator.emplace(desc.MaxSubmeshes);
		metadata.resize(desc.MaxMeshes);
		submeshRows.resize(desc.MaxSubmeshes);
		dirtyMetadata.Resize(desc.MaxMeshes);
		dirtySubmeshes.Resize(desc.MaxSubmeshes);
	}

	GeometryHeap::~GeometryHeap()
	{
		try
		{
			Drain();
		}
		catch (...)
		{
		}
	}

	Rhi::ResourceState GeometryHeap::GetRestingState(Stream stream)
	{
		using S = Rhi::ResourceState;
		switch (stream)
		{
		case Stream::Vertex:
			return S::VertexBuffer | S::ShaderRead;
		case Stream::Index:
			return S::IndexBuffer | S::ShaderRead;
		default:
			return S::ShaderRead;
		}
	}

	std::uint64_t GeometryHeap::PageSize(Stream stream) const
	{
		switch (stream)
		{
		case Stream::Vertex:
			return desc.VertexPageSize;
		case Stream::Index:
			return desc.IndexPageSize;
		default:
			return desc.MeshletPageSize;
		}
	}

	Internal::GeometryAllocation GeometryHeap::Allocate(Stream stream, std::uint64_t size, std::uint64_t alignment, std::string_view label)
	{
		const bool dedicated = size > PageSize(stream);
		if (!dedicated)
		{
			for (std::uint32_t p = 0; p < pages.size(); ++p)
			{
				auto& page = pages[p];
				if (page.Buffer && !page.Dedicated && page.Stream == stream)
				{
					if (auto range = page.Allocator->Allocate(size, alignment))
					{
						return { p, *range };
					}
				}
			}
		}
		if (dedicated && size > UINT32_MAX)
		{
			throw std::length_error("GeometryHeap mesh stream exceeds the 4 GiB page limit");
		}

		std::uint32_t slot = 0;
		if (!freePageSlots.empty())
		{
			slot = freePageSlots.back();
		}
		else if (pages.size() < desc.MaxPages)
		{
			slot = static_cast<std::uint32_t>(pages.size());
		}
		else
		{
			throw std::length_error(name + " page limit reached");
		}

		const auto capacity = dedicated ? size : PageSize(stream);
		const auto pageName = name + " " + std::string(label) + (dedicated ? " dedicated page" : " page");
		auto buffer = device.CreateBuffer({ capacity, PageUsage(stream), Rhi::MemoryPreference::DeviceLocal, pageName });
		if (!buffer)
		{
			throw std::runtime_error(name + " page allocation failed");
		}

		Internal::GeometryPage page;
		page.Stream = stream;
		page.Dedicated = dedicated;
		page.Buffer = std::move(buffer);
		page.Allocator.emplace(capacity);
		const auto range = page.Allocator->Allocate(size, alignment);
		if (!range)
		{
			throw std::logic_error("GeometryHeap fresh page could not hold its allocation");
		}
		if (slot == pages.size())
		{
			pages.push_back(std::move(page));
		}
		else
		{
			pages[slot] = std::move(page);
			freePageSlots.pop_back();
		}
		return { slot, *range };
	}

	void GeometryHeap::Free(const Internal::GeometryAllocation& allocation)
	{
		if (!allocation.IsValid())
		{
			return;
		}
		auto& page = pages[allocation.Page];
		page.Allocator->Free(allocation.Range);
		if (page.Dedicated)
		{
			// Retirement already proved the last GPU use completed.
			page.Buffer.reset();
			page.Allocator.reset();
			freePageSlots.push_back(allocation.Page);
		}
	}

	void GeometryHeap::Retire(Internal::GeometryMeshRecord& record)
	{
		Free(record.Vertex);
		Free(record.Index);
		Free(record.Meshlet);
		if (record.Submeshes.Size)
		{
			submeshAllocator->Free(record.Submeshes);
		}
		record.Vertex = record.Index = record.Meshlet = {};
		record.Submeshes = {};
	}

	GpuMeshHandle GeometryHeap::CreateMesh(const GeometryMeshDesc& mesh)
	{
		if (!mesh.VertexStride || mesh.Vertices.empty() || mesh.Vertices.size() % mesh.VertexStride != 0 ||
			mesh.Vertices.size() / mesh.VertexStride > UINT32_MAX)
		{
			throw std::invalid_argument("GeometryHeap mesh needs whole vertices with a nonzero stride");
		}
		const std::uint32_t indexBytes = mesh.IndexFormat == Rhi::IndexType::Uint16 ? 2 : 4;
		if (mesh.Indices.size() % indexBytes != 0 || mesh.Indices.size() / indexBytes > UINT32_MAX)
		{
			throw std::invalid_argument("GeometryHeap index data must contain whole indices");
		}
		const auto vertexCount = static_cast<std::uint32_t>(mesh.Vertices.size() / mesh.VertexStride);
		const auto indexCount = static_cast<std::uint32_t>(mesh.Indices.size() / indexBytes);
		if (!indexCount && !mesh.Submeshes.empty())
		{
			throw std::invalid_argument("GeometryHeap submeshes need indices");
		}
		// Empty submesh lists with indices mean one submesh over every index.
		const GeometrySubmesh implicitSubmesh{ 0, indexCount, 0, 0 };
		const std::span<const GeometrySubmesh> submeshes =
			mesh.Submeshes.empty() && indexCount ? std::span<const GeometrySubmesh>(&implicitSubmesh, 1) : mesh.Submeshes;
		if (submeshes.size() > desc.MaxSubmeshes)
		{
			throw std::length_error(name + " mesh has more submeshes than the heap has rows");
		}
		for (const auto& submesh : submeshes)
		{
			if (!submesh.IndexCount || submesh.FirstIndex > indexCount || submesh.IndexCount > indexCount - submesh.FirstIndex ||
				submesh.VertexOffset < 0 || std::uint32_t(submesh.VertexOffset) >= vertexCount)
			{
				throw std::invalid_argument("GeometryHeap submesh range exceeds the mesh indices or vertices");
			}
		}
		const auto submeshCount = static_cast<std::uint32_t>(submeshes.size());
		if (mesh.Lods.size() > GpuMeshMetadata::MaxLods || (!submeshCount && !mesh.Lods.empty()))
		{
			throw std::invalid_argument("GeometryHeap LODs need submeshes and at most GpuMeshMetadata::MaxLods entries");
		}
		for (const auto& lod : mesh.Lods)
		{
			if (!lod.SubmeshCount || lod.FirstSubmesh > submeshCount || lod.SubmeshCount > submeshCount - lod.FirstSubmesh)
			{
				throw std::invalid_argument("GeometryHeap LOD range exceeds the mesh submeshes");
			}
		}
		if (mesh.Meshlets.empty() != (mesh.MeshletCount == 0))
		{
			throw std::invalid_argument("GeometryHeap meshlet bytes and count must be supplied together");
		}

		const auto slots = meshes.GetStats();
		if (!slots.FreeSlots && slots.SlotHighWater >= slots.MaxSlots)
		{
			throw std::length_error(name + " has no free mesh slots");
		}

		Internal::GeometryMeshRecord record;
		try
		{
			// Vertex ranges start on a whole vertex (DrawIndexed vertexOffset) that is
			// also four-byte aligned for storage-buffer vertex pulling.
			record.Vertex = Allocate(Stream::Vertex, mesh.Vertices.size(), std::lcm<std::uint64_t>(mesh.VertexStride, 4), "vertex");
			if (indexCount)
			{
				record.Index = Allocate(Stream::Index, mesh.Indices.size(), 4, "index");
			}
			if (mesh.MeshletCount)
			{
				record.Meshlet = Allocate(Stream::Meshlet, mesh.Meshlets.size(), 16, "meshlet");
			}
			if (submeshCount)
			{
				const auto rows = submeshAllocator->Allocate(submeshCount, 1);
				if (!rows)
				{
					throw std::length_error(name + " has no contiguous free submesh rows");
				}
				record.Submeshes = *rows;
			}
		}
		catch (...)
		{
			Retire(record);
			throw;
		}

		record.VertexBytes = Copy(mesh.Vertices);
		record.IndexBytes = Copy(mesh.Indices);
		record.MeshletBytes = Copy(mesh.Meshlets);

		GpuMeshMetadata row;
		row.VertexPage = record.Vertex.Page;
		row.VertexOffset = static_cast<std::uint32_t>(record.Vertex.Range.Offset / mesh.VertexStride);
		row.VertexCount = vertexCount;
		row.VertexStride = mesh.VertexStride;
		row.VertexLayout = mesh.VertexLayout;
		if (indexCount)
		{
			row.IndexPage = record.Index.Page;
			row.FirstIndex = static_cast<std::uint32_t>(record.Index.Range.Offset / indexBytes);
			row.IndexCount = indexCount;
			row.IndexBytes = indexBytes;
		}
		if (submeshCount)
		{
			row.FirstSubmesh = static_cast<std::uint32_t>(record.Submeshes.Offset);
			row.SubmeshCount = submeshCount;
			for (std::uint32_t i = 0; i < submeshCount; ++i)
			{
				const auto& submesh = submeshes[i];
				submeshRows[row.FirstSubmesh + i] = { row.FirstIndex + submesh.FirstIndex, submesh.IndexCount,
					static_cast<std::int32_t>(row.VertexOffset + std::uint32_t(submesh.VertexOffset)), submesh.MaterialSlot };
			}
			dirtySubmeshes.MarkRange(row.FirstSubmesh, submeshCount);
			if (mesh.Lods.empty())
			{
				row.LodCount = 1;
				row.Lods[0] = { row.FirstSubmesh, submeshCount, 0.0f, 0 };
			}
			else
			{
				row.LodCount = static_cast<std::uint32_t>(mesh.Lods.size());
				for (std::size_t i = 0; i < mesh.Lods.size(); ++i)
				{
					row.Lods[i] = { row.FirstSubmesh + mesh.Lods[i].FirstSubmesh, mesh.Lods[i].SubmeshCount, mesh.Lods[i].Error, 0 };
				}
			}
		}
		if (mesh.MeshletCount)
		{
			row.MeshletPage = record.Meshlet.Page;
			row.MeshletOffset = static_cast<std::uint32_t>(record.Meshlet.Range.Offset);
			row.MeshletCount = mesh.MeshletCount;
		}

		auto handle = meshes.TryCreate(std::move(record));
		if (!handle)
		{
			throw std::logic_error("GeometryHeap mesh slot disappeared after its capacity check");
		}

		row.Generation = handle->Generation;
		metadata[handle->Index] = row;
		dirtyMetadata.Mark(handle->Index);
		return *handle;
	}

	bool GeometryHeap::DestroyMesh(GpuMeshHandle mesh, Rhi::TimelinePoint lastUse)
	{
		auto* record = meshes.Get(mesh);
		if (!record)
		{
			return false;
		}
		if (record->State == GeometryResidency::Recorded)
		{
			throw std::logic_error("GeometryHeap mesh upload is recorded; commit or abort it before destruction");
		}
		if (record->State == GeometryResidency::Uploading && !IsComplete(record->Upload))
		{
			if (!lastUse.Semaphore)
			{
				lastUse = record->Upload;
			}
			else if (lastUse.Semaphore == record->Upload.Semaphore)
			{
				lastUse.Value = std::max(lastUse.Value, record->Upload.Value);
			}
		}
		metadata[mesh.Index] = {}; // Culling/draw code sees an empty row from the next upload on.
		dirtyMetadata.Mark(mesh.Index);
		return meshes.Release(mesh, lastUse);
	}

	std::size_t GeometryHeap::Collect()
	{
		std::size_t changed = 0;
		meshes.ForEach(
			[&](GpuMeshHandle, Internal::GeometryMeshRecord& record)
			{
				if (record.State == GeometryResidency::Uploading && IsComplete(record.Upload))
				{
					record.State = GeometryResidency::Resident;
					record.Upload = {};
					++changed;
				}
			});
		return changed +
			meshes.CollectRetired(
				[&](GpuMeshHandle, Internal::GeometryMeshRecord& record)
				{
					Retire(record);
				});
	}

	void GeometryHeap::Drain()
	{
		meshes.ForEach(
			[&](GpuMeshHandle, Internal::GeometryMeshRecord& record)
			{
				if (record.State == GeometryResidency::Uploading && !IsComplete(record.Upload) &&
					!record.Upload.Semaphore->Wait(record.Upload.Value))
				{
					throw std::runtime_error("GeometryHeap upload wait failed");
				}
			});
		meshes.Drain(
			[&](GpuMeshHandle, Internal::GeometryMeshRecord& record)
			{
				Retire(record);
			});
		Collect();
	}

	GeometryResidency GeometryHeap::GetResidency(GpuMeshHandle mesh) const
	{
		const auto* record = meshes.Get(mesh);
		return record ? record->State : GeometryResidency::Invalid;
	}

	const GpuMeshMetadata* GeometryHeap::GetMetadata(GpuMeshHandle mesh) const
	{
		return meshes.IsValid(mesh) ? &metadata[mesh.Index] : nullptr;
	}

	std::span<const GpuSubmeshRecord> GeometryHeap::GetSubmeshes(GpuMeshHandle mesh) const
	{
		if (!meshes.IsValid(mesh))
		{
			return {};
		}
		const auto& row = metadata[mesh.Index];
		return std::span<const GpuSubmeshRecord>(submeshRows).subspan(row.FirstSubmesh, row.SubmeshCount);
	}

	Rhi::Buffer* GeometryHeap::GetPage(std::uint32_t page) const
	{
		return page < pages.size() ? pages[page].Buffer.get() : nullptr;
	}

	GeometryPoolStats GeometryHeap::PoolStats(Stream stream) const
	{
		GeometryPoolStats stats;
		for (const auto& page : pages)
		{
			if (!page.Buffer || page.Stream != stream)
			{
				continue;
			}
			++stats.Pages;
			stats.DedicatedPages += page.Dedicated;
			stats.ReservedBytes += page.Allocator->GetCapacity();
			stats.AllocatedBytes += page.Allocator->GetAllocatedBytes();
			stats.FreeBytes += page.Allocator->GetFreeBytes();
			stats.LargestFreeRange = std::max(stats.LargestFreeRange, page.Allocator->GetLargestFreeRange());
			stats.FreeRanges += page.Allocator->GetFreeRangeCount();
		}
		if (stats.FreeBytes)
		{
			stats.Fragmentation = 1.0 - double(stats.LargestFreeRange) / double(stats.FreeBytes);
		}
		return stats;
	}

	GeometryHeapStats GeometryHeap::GetStats() const
	{
		GeometryHeapStats stats;
		stats.Vertex = PoolStats(Stream::Vertex);
		stats.Index = PoolStats(Stream::Index);
		stats.Meshlet = PoolStats(Stream::Meshlet);
		meshes.ForEach(
			[&](GpuMeshHandle, const Internal::GeometryMeshRecord& record)
			{
				switch (record.State)
				{
				case GeometryResidency::PendingUpload:
					++stats.PendingMeshes;
					stats.PendingUploadBytes += PayloadBytes(record);
					break;
				case GeometryResidency::Recorded:
					++stats.RecordedMeshes;
					stats.PendingUploadBytes += PayloadBytes(record);
					break;
				case GeometryResidency::Uploading:
					++stats.UploadingMeshes;
					break;
				case GeometryResidency::Resident:
					++stats.ResidentMeshes;
					break;
				default:
					break;
				}
			});
		stats.RetiringMeshes = meshes.GetStats().Retiring;
		stats.DirtyMetadataRows = static_cast<std::uint32_t>(dirtyMetadata.Rows.size());
		stats.DirtySubmeshRows = static_cast<std::uint32_t>(dirtySubmeshes.Rows.size());
		stats.SubmeshRowsAllocated = submeshAllocator->GetAllocatedBytes();
		stats.SubmeshRowCapacity = submeshAllocator->GetCapacity();
		return stats;
	}
} // namespace Swim::Render
