# GPU resource residency

This covers critical-path items **42** (generational GPU resource registries) and **43** (paged `GeometryHeap`). Both live above the RHI and RenderGraph and below any scene/extraction code:

```text
Renderer/Geometry   GeometryHeap, GeometryRangeAllocator, GpuMeshMetadata
        |
Renderer/Resources  GpuHandle<Tag>, GpuResourceRegistry<Tag, Record>
        |
Renderer/RenderGraph (staged uploads, transfer helpers)  ->  RHI contract
```

Neither folder includes a native backend, SDL/platform, EnTT or scene header; `scripts/verify-build-layout.py` enforces that, and RenderGraph may not include them back. Sources compile directly into `SwimEngine`/`SwimTests` (same source list as RenderGraph). `SwimRenderResourcesPublicHeaders` is the compile boundary gate.

## Generational handles and registries (item 42)

`GpuHandle<Tag>` is a 32-bit slot index plus a 32-bit generation (`Pack()`/`Unpack()` give one `uint64`). Aliases: `GpuMeshHandle`, `GpuTextureHandle`, `GpuSamplerHandle`, `GpuMaterialHandle`, `RenderObjectHandle`, `GpuSkinHandle`. The index is the dense shader-visible slot (metadata row, bindless element); handles never own or point at objects.

`GpuResourceRegistry<Tag, Record>` stores movable records (normally owning `unique_ptr` RHI objects):

- `Create`/`TryCreate` return a live handle; `MaxSlots` bounds live **plus retiring** slots (for example a bindless table size). `Create` throws `std::length_error` when full.
- `Release(handle, lastUse)` invalidates the handle immediately and bumps the slot generation, but the record stays alive and its index stays reserved until `lastUse` completes. A null timeline means no submitted GPU work references it. Stale/invalid handles return `false`.
- `CollectRetired(onRetired)` is nonblocking: completed records get the callback (for example returning GeometryHeap ranges), are destroyed, and their slots join a FIFO free list, so indices are reused oldest-first. A throwing callback leaves that entry pending. Different timelines retire independently.
- `Drain()` waits every pending point then collects; the destructor drains best-effort. A slot whose generation would wrap is retired permanently instead of reusing a generation.
- Externally synchronized. Timelines passed to `Release` must outlive their pending entries.

This is the "timeline-safe ID reuse" rule item 45's bindless table will rely on: an index is never re-pointed while work that read the old descriptor can still run.

## Paged GeometryHeap (item 43)

```cpp
GeometryHeap heap(device, { 64 << 20, 32 << 20, 16 << 20, /*MaxMeshes*/ 16384, /*MaxPages*/ 256 });
GeometryMeshDesc mesh;
mesh.VertexStride = 32;
mesh.Vertices = vertexBytes;            // copied; spans may be released immediately
mesh.IndexFormat = Rhi::IndexType::Uint16;
mesh.Indices = indexBytes;
mesh.Lods = lods;                       // optional, <= 8, relative to the mesh's indices
auto handle = heap.CreateMesh(mesh);    // PendingUpload

RenderGraph graph;
auto geometry = heap.Import(graph);     // imports pages + metadata, records uploads
// ... cull/draw passes read geometry.Pages[row.VertexPage] etc. in the same graph ...
auto completion = executor.Execute(graph.Compile());
heap.CommitUploads(completion);         // or heap.AbortUploads() on failure
// later, once per frame:
heap.Collect();                         // Uploading -> Resident, retire freed ranges
heap.DestroyMesh(handle, lastFrameUsingIt);
```

**Pages.** Each stream (vertex, index, meshlet) allocates variable ranges from large device-local pages with a best-fit `GeometryRangeAllocator` (any alignment, immediate coalescing). A stream larger than its page size gets a dedicated page of exactly that size, released when its mesh retires; the page id slot is then reused. Vertex ranges are aligned to `lcm(stride, 4)` so `VertexOffset` is a whole-vertex `DrawIndexed` base that is also four-byte aligned for storage-buffer vertex pulling. Index ranges are four-byte aligned (16- and 32-bit indices); meshlet ranges are 16-byte aligned. Page sizes are limited to 4 GiB because metadata offsets are 32-bit. Page usages include storage and transfer source so later GPU-driven passes, readback verification and future compaction can use them.

**Stable metadata.** Every mesh owns row `handle.Index` of a device-local `GpuMeshMetadata` buffer (192-byte std430-compatible rows: page ids, vertex offset/count/stride, first index/count/index size, layout id, up to eight absolute LOD ranges, meshlet page/offset/count, generation). The row is stable for the mesh lifetime and is not reused until retirement, which gives GPU culling/draw generation a fixed indirection. Destroying a mesh clears its row (uploaded with the next import).

**Explicit residency.** `GeometryResidency` is separate from asset validity: `PendingUpload` → `Recorded` (upload passes added by `Import`) → `Uploading` (committed with the graph's completion point) → `Resident` (observed by `Collect`). `Import` records one staging suballocation and one transfer pass **per touched page** (every mesh chunk is a preserving partial copy, `ReadWrite` + `CopyDestination`) and one metadata pass copying contiguous dirty row runs. All barriers come from the graph. Pages and the metadata buffer are imported in their resting read states (`VertexBuffer|ShaderRead`, `IndexBuffer|ShaderRead`, `ShaderRead`) and return there; new pages are imported the same way because unallocated bytes are never read and buffers have no layout. `Import` throws while a previous import awaits `CommitUploads`/`AbortUploads`; an import with nothing to upload needs no commit. `AbortUploads` returns meshes to `PendingUpload` and re-dirties their rows.

**Deferred frees.** `DestroyMesh(handle, lastUse)` invalidates the handle immediately; ranges and the metadata row return to the allocators only when the registry retires the record. If the mesh's upload is still in flight, a null or earlier same-timeline `lastUse` is raised to the upload completion. Recorded (uncommitted) meshes must be committed or aborted first. `Drain` waits every pending upload and retirement; the destructor drains best-effort.

**Metrics.** `GetStats()` reports, per stream, pages/dedicated pages, reserved/allocated/free bytes, largest free range, free-range count and fragmentation (`1 - largest / free`), plus pending/recorded/uploading/resident/retiring mesh counts, pending upload bytes and dirty metadata rows.

**Failure behaviour.** Invalid descriptions throw `std::invalid_argument`; exhausted mesh slots or page slots throw `std::length_error`; page allocation failure throws `std::runtime_error`. `CreateMesh` rolls back every range it allocated before throwing.

## Not in this checkpoint

- Asynchronous asset residency (`MeshAsset`/`TextureAsset` → heap/textures through async IO), item **44**.
- Bindless texture/sampler tables built on `GpuResourceRegistry`, item **45**.
- Relocation/compaction and page trimming (metrics exist; policy is later). Normal pages are retained once created.
- Dedicated transfer-queue ownership for uploads (Phase 10 follow-up); uploads run on the graph's graphics queue.

## Tests

- `Render.GpuResourceRegistry.*` — generations, packing, deferred destruction, per-timeline retirement, FIFO reuse, capacity including retiring slots, drain failure/retry, throwing callbacks, destructor waits.
- `Render.GeometryRangeAllocator.*` — alignment (including non-power-of-two strides), best fit, coalescing, double-free rejection, exhaustion without state change, fragmentation.
- `Render.GeometryHeap.*` — metadata rows/LODs/16-bit indices, shared and dedicated pages, per-page batched graph uploads whose bytes land at the described offsets (via the mock command stream), residency transitions, abort/re-record, exclusive pending import, deferred range/row reuse, raised retirement points, limits and rollback, fragmentation metrics.
- Opt-in native `RHI.Vulkan.Smoke.GeometryHeapPagedUploadAndRetirement` — three rounds of batched uploads verified by same-graph readback of page ranges and metadata, with deferred destruction and reuse.
