# GPU resource residency

This covers critical-path items **42** (generational GPU resource registries), **43** (paged `GeometryHeap`), **44** (compiled `MeshAsset`/`TextureAsset` → asynchronous GPU residency) and **45** (bindless texture/sampler table and sampler residency). They live above the RHI and RenderGraph and below any scene/extraction code:

```text
Renderer/Residency  AssetResidencyService, TextureResidency, MeshGeometryPayload   (Assets + async IO + Jobs)
        |
Renderer/Geometry   GeometryHeap, GeometryRangeAllocator, GpuMeshMetadata, GpuSubmeshRecord
        |
Renderer/Resources  GpuHandle<Tag>, GpuResourceRegistry<Tag, Record>, GpuUploadState,
                    BindlessResourceTable, GpuSamplerCache
        |
Renderer/RenderGraph (staged uploads, transfer helpers)  ->  RHI contract
```

None of these folders includes a native backend, EnTT or scene header, and only `Residency/` may reach Assets, IO and Jobs; `scripts/verify-build-layout.py` enforces both directions. `Resources/` and `Geometry/` compile with RenderGraph's source list; `Residency/` has its own `SWIM_RENDER_RESIDENCY_SOURCES` list that joins `SwimTests` only where the IO/Platform foundation is compiled (never with offline dependency stubs). `SwimRenderResourcesPublicHeaders` compiles every public header alone.

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
GeometryHeapDesc heapDesc;               // page sizes, MaxMeshes, MaxSubmeshes, MaxPages
GeometryHeap heap(device, heapDesc);
GeometryMeshDesc mesh;
mesh.VertexStride = 32;
mesh.Vertices = vertexBytes;            // copied; spans may be released immediately
mesh.IndexFormat = Rhi::IndexType::Uint16;
mesh.Indices = indexBytes;
mesh.Submeshes = drawRanges;            // optional: {firstIndex, indexCount, vertexOffset, materialSlot}
mesh.Lods = lods;                       // optional, <= 8 submesh ranges
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

**Stable metadata.** Every mesh owns row `handle.Index` of a device-local `GpuMeshMetadata` buffer (192-byte std430-compatible rows: page ids, vertex offset/count/stride, first index/count/index size, layout id, first submesh/submesh count, up to eight LODs, meshlet page/offset/count, generation). The row is stable for the mesh lifetime and is not reused until retirement, which gives GPU culling/draw generation a fixed indirection. Destroying a mesh clears its row (uploaded with the next import).

**Submeshes and LODs.** Draw ranges are `GpuSubmeshRecord` rows (16 bytes: absolute first index, index count, absolute vertex offset, material slot) in a second device-local buffer of `MaxSubmeshes` rows. Each mesh takes a contiguous run allocated with the same range allocator and freed on retirement; `GetSubmeshes(handle)` exposes the CPU mirror. A mesh with indices and no explicit submeshes gets one submesh over all indices; non-indexed meshes have none. LODs are **submesh ranges** (`GeometryLodRange{FirstSubmesh, SubmeshCount, Error}`, stored absolute in `GpuMeshLod`), so each LOD keeps its material split. Submesh ranges must stay inside the mesh's indices and start at a vertex inside the mesh. *(Item 43 originally stored LODs as raw index ranges; that was replaced before any consumer existed.)*

**Explicit residency.** `GeometryResidency` is separate from asset validity: `PendingUpload` → `Recorded` (upload passes added by `Import`) → `Uploading` (committed with the graph's completion point) → `Resident` (observed by `Collect`). `Import` records one staging suballocation and one transfer pass **per touched page** (every mesh chunk is a preserving partial copy, `ReadWrite` + `CopyDestination`) and one metadata pass and one submesh pass, each copying contiguous dirty row runs. All barriers come from the graph. Pages and the metadata buffer are imported in their resting read states (`VertexBuffer|ShaderRead`, `IndexBuffer|ShaderRead`, `ShaderRead`) and return there; new pages are imported the same way because unallocated bytes are never read and buffers have no layout. `Import` throws while a previous import awaits `CommitUploads`/`AbortUploads`; an import with nothing to upload needs no commit. `AbortUploads` returns meshes to `PendingUpload` and re-dirties their rows.

**Deferred frees.** `DestroyMesh(handle, lastUse)` invalidates the handle immediately; ranges and the metadata row return to the allocators only when the registry retires the record. If the mesh's upload is still in flight, a null or earlier same-timeline `lastUse` is raised to the upload completion. Recorded (uncommitted) meshes must be committed or aborted first. `Drain` waits every pending upload and retirement; the destructor drains best-effort.

**Metrics.** `GetStats()` reports, per stream, pages/dedicated pages, reserved/allocated/free bytes, largest free range, free-range count and fragmentation (`1 - largest / free`), plus pending/recorded/uploading/resident/retiring mesh counts, pending upload bytes, dirty metadata/submesh rows and submesh-row occupancy.

**Failure behaviour.** Invalid descriptions throw `std::invalid_argument`; exhausted mesh slots, submesh rows or page slots throw `std::length_error`; page allocation failure throws `std::runtime_error`. `CreateMesh` rolls back every range it allocated before throwing.

## Compiled assets to GPU residency (item 44)

### Off-thread decode

`Assets::DecodeSasset(bytes)` (in `SassetDecodedAsset.h`) parses the container, validates chunk and content hashes and decodes mesh, texture, sampler and material-template payloads **without touching an `AssetSystem`**, so it runs on a job worker. `Assets::PublishSasset(assets, decoded)` then binds the logical path and publishes on the owner thread. Material instances and models resolve `AssetHandle`s while decoding; they decode to `std::monostate` (`RequiresOwnerThreadDecode()`) and keep using `LoadSasset`. `LoadSasset` itself is unchanged.

### MeshAsset → GeometryHeap

`BuildMeshGeometryPayload(mesh)` produces a `MeshGeometryPayload` for `GeometryHeap::CreateMesh`: vertex streams interleaved into one packed stride (a single stream is copied as-is), a stable FNV-1a layout id over stride and packed attribute offsets/semantics/formats (identical packed layouts hash identically regardless of stream split), primitives as submeshes with their material slots, `MeshLod` primitive ranges as submesh-range LODs (screen coverage stored as the LOD metric; at most eight levels, finest first), and meshlets packed behind a 32-byte `GpuMeshletPayloadHeader` with 16-byte-aligned descriptor, meshlet-vertex and triangle sections. Inconsistent streams, attributes outside their stream, and LODs without primitives throw `std::invalid_argument`.

### TextureResidency

`TextureResidency` gives `TextureAsset`s the same lifecycle as the heap: `CreateTexture` creates the RHI texture (sampled + transfer usage) and a full-chain view immediately, repacks every mip tightly at texel alignment and stays `PendingUpload`; `Import` records one staging suballocation and one transfer pass per pending texture that writes every mip in full, then exports it to `ShaderRead` so later passes in the same graph can sample it through `TextureGraphResources`; `CommitUploads`/`AbortUploads`/`Collect`/`DestroyTexture`/`Drain` match the heap, with records in a `GpuResourceRegistry<GpuTextureTag, …>` so RHI objects and slots retire after their last use. `SelectPayload` picks the first **uncompressed native-mip 2D** variant (R8, RG8, RGBA8, RGBA8 sRGB, RGBA16F). KTX2, supercompressed and block-compressed variants are rejected explicitly because the RHI's buffer/image copies are not yet block-aware; cubes, arrays and 3D textures are rejected likewise. Mip chains are validated against the texture extent.

### AssetResidencyService

`AssetResidencyService(assets, io, jobs, geometryHeap, textureResidency, desc)` implements the Phase 11 state machine per requested asset:

```text
Unloaded -> Queued -> Reading -> Decoding -> WaitingForGpuUpload -> Uploading -> Resident
                                                                                (or Failed + AssetError)
```

- `RequestMesh`/`RequestTexture` take current `AssetHandle`s. If the CPU asset is already resident the request starts at `WaitingForGpuUpload`; otherwise it is `Queued` and the AssetSystem record is marked `Queued`/`Loading` as work progresses. Requests are idempotent; a `Failed` request is retried by requesting it again.
- `Update()` (owner thread, once per frame after `AsyncIoService::PumpCompletions`) polls read completion (no callbacks capture the service), starts at most `MaxConcurrentReads` `ReadFileAsync` calls using `desc.ResolveCookedPath(AssetId)`, hands completed reads to at most `MaxConcurrentDecodes` `JobSystem` jobs running `DecodeSasset` (inline when no job system is supplied), publishes decoded assets after checking that the object carries the requested id and type, and stages waiting assets into `GeometryHeap`/`TextureResidency` in request order within `UploadBudgetBytes` per update (at least one asset always advances). Slot/row exhaustion is backpressure: the asset stays waiting.
- By default the CPU asset is unloaded as soon as its GPU copy is staged (`RetainCpuAssets = false`), so CPU validity and GPU residency are independent; re-requesting a released asset reads it again.
- `Import(graph)` records heap and texture uploads into one graph (`GpuResidencyGraphResources`); `CommitUploads(completion)` after a successful `Execute`, `AbortUploads()` otherwise. `Uploading → Resident` is observed by `Update` once the completion value is reached.
- `ReleaseMesh`/`ReleaseTexture(handle, lastUse)` cancel queued/reading work, abandon running decode jobs (their shared slot keeps the bytes alive, nothing is published), reset the CPU asset's `Queued`/`Loading` bookkeeping, or retire GPU data after `lastUse` (raised to the upload completion while in flight). Releasing while an import awaits commit throws and leaves the request intact. `ReleaseAll(lastUse)` releases everything; the destructor cancels reads and waits abandoned decodes but leaves GPU data to the heap/residency `Drain`.
- Errors map to `Assets::AssetError`: missing path → `NotFound`, IO failure → `Io`, cancellation → `Cancelled`, bad container/payload or wrong id/type → `InvalidData`. GPU staging failures (for example a compressed-only texture) fail the request with `InvalidData` **without** invalidating the valid CPU asset.
- `GetStats()` reports per-state counts, abandoned decodes, total bytes read, bytes staged in the last update and in total.
- `ResolveRenderMesh(mesh)` returns a Resident mesh's `GpuMeshHandle` together with the local bounds recorded from its `MeshAsset` when it was staged. Those bounds stay valid after the CPU asset unloads. This is the mesh resolver the GPU Scene's `RenderExtractor` uses; see [GPU Scene](GpuScene.md).

## Bindless textures and samplers (item 45)

### RHI contract

- **Runtime-sized arrays.** Slang reflects `Texture2D<float4> Textures[]` and `SamplerState Samplers[]` with `elementCount` 0. The shader compiler converts sampler and sampled-texture arrays like these to bindings with `Count = 0`. Other resource classes still reject when runtime-sized.
- **Explicit spaces.** `PipelineLayoutDesc::DescriptorSpaces` supplies whole spaces that replace the program's reflected layout for those spaces.
  - Each reflected binding must reappear with the same type, sampled class/dimension and storage format. Its stage mask must cover the reflected stages. Fixed arrays keep their count. A runtime-sized binding is only accepted when the explicit space gives it a capacity with `PartiallyBound` and `UpdateAfterBind`.
  - Explicit bindings may be visible to stages the program lacks, and a space the program never reflects may be supplied.
  - `PipelineLayout::GetInterface()` reports the merged result.
- **Bindless bindings.** `UpdateAfterBind` requires `PartiallyBound`, a sampler or sampled-texture binding, and `GraphicsCapabilities::BindlessDescriptors`.
  - That capability is the optional Vulkan `descriptorBindingUpdateUnusedWhilePending` feature, on top of the required descriptor-indexing features.
  - Vulkan builds such bindings with the partially-bound, update-after-bind and update-unused-while-pending flags, in an update-after-bind set layout and pool.
  - Plain descriptor limits count plain sets only. Once a layout has a bindless set, the update-after-bind limits count every set.
- **Tables.** Partially bound elements need no write before binding.
  - After the first bind, only update-after-bind bindings accept writes, and a mixed batch is rejected before any native write.
  - A table binds to any pipeline whose layout defines its space identically, so one bindless table serves every program built with the same explicit space.

### BindlessResourceTable

`Render::BindlessResourceTable(device, BindlessTableDesc)` owns the descriptor table of one such space. The space must hold exactly two bindings: a float `Texture2D` array and a sampler array, both bindless. Their counts are the capacities.

- Separate `BindlessTextureHandle` and `BindlessSamplerHandle` index spaces sit on two `GpuResourceRegistry` instances, so any texture pairs with any sampler. A shader receives `GetIndex(handle)`: the array element, or `FallbackIndex` (0) for invalid, stale or released handles.
- The caller's fallback texture and sampler permanently occupy element 0 and cannot be released.
- `RegisterTexture`/`RegisterSampler` (and the `TryRegister*` forms, which return empty when full) write the element immediately. Any submission recorded afterwards can index it, even while earlier submissions are still pending. When the RHI rejects the resource, the call throws and the element is freed at the next `Collect`.
- `Release(handle, lastUse)` invalidates the handle at once. The element keeps its resource until `lastUse` completes. `Collect()` then rewrites it to the fallback, so a stale index samples something valid, and returns it to the FIFO free list. `Drain()` waits for pending releases first.
- Registered views and samplers must outlive their release. Destroy the table only after the last submission that bound it has completed.

### GpuSamplerCache

Sampler identity is the `SamplerDesc`, with names ignored.

- `Acquire(desc)` returns the existing `GpuSamplerHandle` for an equal description or creates a sampler. When a table is supplied, the new sampler is registered with it (`GetBindlessIndex`).
- References are counted. The last `Release` retires the sampler, and its bindless element, after the latest `lastUse` reported by any holder. All releases to one cache must use one timeline.
- Call `BindlessResourceTable::Collect` before `GpuSamplerCache::Collect` so elements point at the fallback before their samplers are destroyed. Either order is safe for the GPU, since both points have completed.

### Explicit residency operation

With `AssetResidencyDesc::Bindless` set, `AssetResidencyService::Update` registers a texture's view when it observes the texture `Resident`. This is the explicit operation that makes a texture bindless; loading an asset alone never does it.

- `GetBindlessIndex(texture)` returns the fallback element until the texture is registered.
- While the table is full, the texture stays Resident and registration is retried each update (`AssetResidencyStats::BindlessPending`).
- If the RHI refuses the view, the texture stays Resident and usable through `GetGpuTexture`, keeps the fallback index, and `GetError` records why.
- `ReleaseTexture(texture, lastUse)` releases the element with the same `lastUse` before destroying the texture.

## Not in this checkpoint

- `ModelAsset` graphs (and material instances) are not requested through the service yet; they still load through `LoadSasset`, and their meshes/textures can be requested individually.
- Block-compressed/KTX2 texture upload (needs block-aware RHI copies and/or runtime Basis transcoding) and texture streaming by mip.
- Memory budgets and eviction (the service budgets bytes staged per update, not resident bytes); `.spack` packages (item 81).
- Layout-free bindless tables, bindless storage buffers/images, variable descriptor counts, and GPU material records carrying bindless ids (item 59). The engine does not construct a `BindlessResourceTable` yet.
- The engine runtime does not construct the service yet; the sandbox still renders through the transitional renderer.
- Relocation/compaction and page trimming (metrics exist; policy is later). Normal pages are retained once created.
- Dedicated transfer-queue ownership for uploads (Phase 10 follow-up); uploads run on the graph's graphics queue.

## Tests

- `Render.GpuResourceRegistry.*` — generations, packing, deferred destruction, per-timeline retirement, FIFO reuse, capacity including retiring slots, drain failure/retry, throwing callbacks, destructor waits.
- `Render.GeometryRangeAllocator.*` — alignment (including non-power-of-two strides), best fit, coalescing, double-free rejection, exhaustion without state change, fragmentation.
- `Render.GeometryHeap.*` — metadata rows, submesh rows and submesh-range LODs, 16-bit indices, shared and dedicated pages, per-page batched graph uploads whose bytes land at the described offsets (via the mock command stream), residency transitions, abort/re-record, exclusive pending import, deferred range/row reuse, raised retirement points, limits and rollback, fragmentation metrics.
- Opt-in native `RHI.Vulkan.Smoke.GeometryHeapPagedUploadAndRetirement` — three rounds of batched uploads verified by same-graph readback of page ranges and metadata, with deferred destruction and reuse.
- `Render.MeshGeometryPayload.*` — stream interleaving, layout ids, primitive/LOD mapping and truncation, meshlet packing, rejection of inconsistent tables.
- `Render.TextureResidency.*` — payload selection, graph mip uploads with exact texel bytes, residency transitions, invalid payload/limit/creation-failure rollback, abort/re-commit, upload-aware retirement.
- `Render.AssetResidency.*` — resident CPU assets through GPU residency, per-update budget ordering, NotFound/Io/InvalidData failures and retry, compressed-texture failure that keeps the CPU asset, capacity backpressure, release rules around pending imports.
- `AssetCompiler.SassetDecode.*` and `AssetCompiler.AssetResidency.*` (built where `SwimAssetCompiler` exists, because they cook real `.sasset` objects) — off-thread decode/owner-thread publish, hash failure, owner-thread-only types, and end-to-end streaming of cooked meshes and textures through async IO and job decodes into GPU pages/textures, wrong id/type rejection and release during reads.
- Opt-in native `RHI.Vulkan.Smoke.TextureResidencyMipChainUploadAndRetirement` — a five-level RGBA8 chain uploaded by `TextureResidency` and read back per mip in the same graph, then retired.
- `ShaderCompiler.DescriptorArrays.RuntimeSizedSamplerAndTextureArraysKeepCountZeroForTheLayout` — runtime-sized sampler/texture arrays convert; buffer, uniform and storage-image arrays still reject.
- `RHI.Vulkan.Bindless.*` (Vulkan dispatch capture) — explicit-space merging and every rejection rule, binding flags and update-after-bind set/pool flags, update-after-bind limits, post-bind writes limited to update-after-bind bindings, binding across identically defined spaces, and the compiled `Bindless.slang` reflection.
- `Render.Bindless.*` — fallback elements, immediate writes, full tables, handle invalidation, timeline-gated retirement with fallback rewrite, FIFO reuse, drain, rejected writes, layout validation.
- `Render.SamplerCache.*` — description identity, reference counts, latest-use retirement, one-timeline rule, bindless and table-free use.
- `Render.AssetResidency.ResidentTexturesBecomeBindlessExplicitlyAndRetireTheirElementWithTheTexture` — registration at residency, full-table retry, element retirement with the texture, rejected views.
- Opt-in native `RHI.Vulkan.Smoke.BindlessTableTimelineSafeReuse` — a compute shader samples texture/sampler pairs through nonuniform indices in one shared bindless space. It checks nearest versus linear filtering and the fallbacks, writes a new element while earlier work may still be pending, releases an element against that work's timeline point, samples the fallback through the stale index after `Collect`, and then samples the element's reuse.
