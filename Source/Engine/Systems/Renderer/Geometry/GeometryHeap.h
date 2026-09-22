#pragma once
#include "Engine/Systems/Renderer/Geometry/GeometryGraphResources.h"
#include "Engine/Systems/Renderer/Geometry/GeometryHeapDesc.h"
#include "Engine/Systems/Renderer/Geometry/GeometryHeapStats.h"
#include "Engine/Systems/Renderer/Geometry/GeometryMeshDesc.h"
#include "Engine/Systems/Renderer/Geometry/Internal/GeometryDirtyRows.h"
#include "Engine/Systems/Renderer/Geometry/Internal/GeometryMeshRecord.h"
#include "Engine/Systems/Renderer/Geometry/Internal/GeometryPage.h"
#include "Engine/Systems/Renderer/Resources/GpuResourceRegistry.h"

namespace Swim::Render
{
	class RenderGraph;

	// Paged device-local residency for compiled mesh geometry.
	//
	//   CreateMesh     allocate vertex/index/meshlet ranges, submesh rows and a stable metadata row
	//   Import(graph)  import pages/metadata and record batched upload passes
	//   CommitUploads  after the graph's successful Execute (or AbortUploads)
	//   Collect        Uploading -> Resident, and retire released meshes' ranges
	//   DestroyMesh    handle invalid now; ranges/row reused after the last GPU use
	//
	// Variable ranges live in large pages (one dedicated page for an oversized
	// stream); nothing is uploaded implicitly and no range is reused while the GPU
	// may read it. Every barrier comes from the RenderGraph. Externally
	// synchronized. Timelines supplied to CommitUploads/DestroyMesh must outlive
	// the heap's pending work (Drain first). The device must outlive the heap.
	class GeometryHeap
	{
	  public:
		GeometryHeap(Rhi::Device& device, const GeometryHeapDesc& desc = {});
		~GeometryHeap();
		GeometryHeap(const GeometryHeap&) = delete;
		GeometryHeap& operator=(const GeometryHeap&) = delete;

		// Throws std::length_error when mesh slots or pages are exhausted, and
		// std::runtime_error when page allocation fails; nothing changes on failure.
		GpuMeshHandle CreateMesh(const GeometryMeshDesc& desc);
		// lastUse must cover every submission that may read the mesh. While its
		// upload is in flight, a null or earlier point on the same timeline is
		// raised to the upload completion. Recorded meshes must first be committed
		// or aborted. Returns false for invalid/stale handles.
		bool DestroyMesh(GpuMeshHandle mesh, Rhi::TimelinePoint lastUse = {});

		// Import once per graph. Records at most one upload pass per touched page
		// plus one metadata pass; it never submits. Throws if a previous import is
		// still awaiting CommitUploads/AbortUploads.
		GeometryGraphResources Import(RenderGraph& graph);
		void CommitUploads(Rhi::TimelinePoint completion);
		void AbortUploads(); // Execution failed or the graph was discarded.

		// Nonblocking: promotes completed uploads and retires completed releases.
		std::size_t Collect();
		// Waits all pending uploads and retirements (shutdown/device teardown).
		void Drain();

		bool IsValid(GpuMeshHandle mesh) const { return meshes.IsValid(mesh); }

		GeometryResidency GetResidency(GpuMeshHandle mesh) const;
		const GpuMeshMetadata* GetMetadata(GpuMeshHandle mesh) const;
		Rhi::Buffer* GetPage(std::uint32_t page) const;

		std::uint32_t GetPageCount() const { return static_cast<std::uint32_t>(pages.size()); }

		Rhi::Buffer& GetMetadataBuffer() const { return *metadataBuffer; }

		Rhi::Buffer& GetSubmeshBuffer() const { return *submeshBuffer; }

		// CPU mirror of a live mesh's submesh rows (empty for invalid handles).
		std::span<const GpuSubmeshRecord> GetSubmeshes(GpuMeshHandle mesh) const;

		GeometryHeapStats GetStats() const;

		static Rhi::ResourceState GetRestingState(Internal::GeometryStream stream);

	  private:
		using Stream = Internal::GeometryStream;

		Internal::GeometryAllocation Allocate(Stream stream, std::uint64_t size, std::uint64_t alignment, std::string_view name);
		void Free(const Internal::GeometryAllocation& allocation);
		void Retire(Internal::GeometryMeshRecord& record);
		std::uint64_t PageSize(Stream stream) const;
		GeometryPoolStats PoolStats(Stream stream) const;

		Rhi::Device& device;
		GeometryHeapDesc desc;
		std::string name;
		std::unique_ptr<Rhi::Buffer> metadataBuffer;
		std::vector<Internal::GeometryPage> pages;
		std::vector<std::uint32_t> freePageSlots;
		std::unique_ptr<Rhi::Buffer> submeshBuffer;
		std::optional<GeometryRangeAllocator> submeshAllocator;
		std::vector<GpuMeshMetadata> metadata;
		std::vector<GpuSubmeshRecord> submeshRows;
		Internal::GeometryDirtyRows dirtyMetadata;
		Internal::GeometryDirtyRows dirtySubmeshes;
		GpuResourceRegistry<GpuMeshTag, Internal::GeometryMeshRecord> meshes;
		std::vector<GpuMeshHandle> recordedMeshes;
		std::vector<std::uint32_t> recordedRows;
		std::vector<std::uint32_t> recordedSubmeshRows;
		bool importPending = false;
	};
} // namespace Swim::Render
