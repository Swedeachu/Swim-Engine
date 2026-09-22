# RenderGraph

`Swim::Render::RenderGraph` owns the modern renderer's pass declarations and synchronization plan. It lives above the backend-neutral RHI in `Source/Engine/Systems/Renderer/RenderGraph`; it has no Vulkan, SDL, scene or EnTT dependencies. Its sources compile directly into `SwimEngine` and `SwimTests`, preserving the consolidated project layout. The `SwimRenderGraphPublicHeaders` target checks the public include boundary.

## Define, compile, execute

Create transient buffers/textures or import persistent RHI objects. Declare passes in logical resource-version order. Each write produces the version seen by later reads. `Compile()` validates the complete graph, constructs dependency edges, culls unused work, orders the remaining passes, assigns physical resources and synthesizes barriers. It performs no GPU work. The compiled snapshot owns names, descriptions and callbacks and survives destruction or later mutation of the original graph.

```cpp
using namespace Swim;
using namespace Swim::Render;
using State = Rhi::ResourceState;

RenderGraph graph;
auto input = graph.ImportBuffer(upload, State::HostWrite);
auto working = graph.CreateBuffer({
    byteCount,
    Rhi::BufferUsage::TransferDestination | Rhi::BufferUsage::Storage,
    Rhi::MemoryPreference::DeviceLocal,
    "Working data"
});
graph.AddPass("Upload", Rhi::QueueType::Transfer,
    [&](RenderGraphBuilder& b) {
        b.Read(input, State::CopySource);
        b.Write(working, State::CopyDestination);
    },
    [=](RenderCommandContext& c) {
        c.Commands().CopyBuffer(c.Get(input), c.Get(working), {0, 0, byteCount});
    });
graph.Export(working, State::ShaderRead);

auto compiled = graph.Compile();
auto dump = compiled.Dump();
RenderGraphExecutor executor(device);
auto completion = executor.Execute(compiled);
executor.Wait();
auto& gpuBuffer = executor.GetExported(working);
```

The example assumes a host-written `upload` buffer with transfer-source usage. Keep `device`, imports, and any externally captured pipelines, samplers or other objects alive through execution completion. Setup callbacks run immediately; execution callbacks run when `Execute()` records commands. Capture graph handles by value when a callback outlives its defining scope.

## Declaration and compilation rules

- `Read` requires initialized contents. `Write` promises to initialize the entire declared range; use `ReadWrite` for load/preserve, partial writes, blending, or shader read-modify-write. A shader that only writes some pixels/elements cannot initialize a whole resource without an earlier initializing pass.
- Buffer hazards cover the whole buffer. Texture hazards and initialization track each mip/layer; depth and stencil remain coupled, matching the current RHI. Default ranges cover the whole texture. Multiple disjoint declarations in one pass are supported; overlapping declarations reject and should become one `ReadWrite` declaration with the appropriate state.
- State, usage, access and pass-role mismatches reject. Storage-image reads use `ShaderRead | ShaderWrite` with `Read`, because that is the RHI's storage-image layout vocabulary. Buffer read-modify-write requires both shader read and write access.
- Reads before the first write, reads of undefined imports, uninitialized exports, foreign/invalid handles, duplicate imports of one object, invalid ranges and conflicting export states reject. Validation also covers culled passes. Import a persistent object once and share its handle.
- RAW, WAR and WAW hazards constrain ordering. `DependsOn` and `AddDependency` add explicit edges, including forward edges after both passes exist. Contradictions and cycles fail with pass names. Otherwise ties resolve in declaration order.
- Exports, writes to imported resources, and explicit `SideEffect()` passes are roots. Data producers and explicit prerequisites remain live. An overwritten transient write can be culled when it contributes no live data or side effect; scheduling-only hazard edges do not keep it alive.
- Imported resources start in the caller-declared state. Unexported imports return to their initial state. Initially undefined imports that are used require an explicit export state. Exports cover the whole resource, pin its lifetime, and establish its final state. Reusing a compiled graph requires reestablishing its declared import states.

## Execution, pooling and lifetime

The initial scheduler uses **one graphics queue for graphics, compute and transfer passes**. Pass type describes supported work, not a request for a dedicated queue. This follows Phase 10's correctness-first scheduling order. Imported resources must already belong to the graphics family; passing another owner rejects. Swapchain graphics/present sharing remains inside the RHI. Dedicated-family release/acquire transfers, async compute and overlapping frame execution remain later scheduler work; this checkpoint does not claim those features.

The compiler assigns one physical allocation to compatible transient resources with strictly nonoverlapping lifetimes. Compatibility includes the complete allocation description, including usage. Overlapping, incompatible, imported and still-exported resources cannot share a slot. Reuse preserves physical state and emits the required dependency barrier even when both logical resources use the same state. This is object pooling, not VMA memory aliasing between distinct image/buffer objects.

Each executor has one submission in flight. Before the next execution, it waits for its previous timeline signal, then recycles command storage, views/descriptors, query slots and compatible resource allocations. Unmatched allocations are released so the cache is bounded by the current graph. A failed wait leaves the prior result and resources intact. Recording/submission failures do not publish a result or advance the completion value; recording failures can be retried. Device loss still requires device recreation under the RHI's existing contract. Executors and pass contexts are not thread-safe or reentrant.

Use `RenderCommandContext::Get` only for declared resources/ranges. `CreateView` checks its declared range and retains the view; use `Retain` for descriptor tables or other objects created while recording. The executor retains these objects through completion. Callbacks must obey declarations, balance rendering scopes, and must not emit their own barriers or queue submissions. The raw command interface cannot prove arbitrary shader accesses or prohibit a callback from bypassing that contract.

`GetExported` returns the current execution's exported resource. Its reference is valid until the next `Execute`, `Trim`, or executor destruction. It does not wait: CPU reads require `Wait()` and a host-read export state. Copy or consume results before reuse. `Trim()` waits, clears graph-owned views and pooled resources, and invalidates results. Use it before destroying/resizing imported resources whose views were created by graph callbacks.

`Execute` accepts external binary/timeline waits and signals plus a completion fence through `SubmitDesc`; it owns the command-list span and its completion timeline. Acquire a swapchain image before graph construction, import it, export it to `Present`, supply the acquire/present semaphores, then present after submission. A skipped acquisition should skip graph execution. The render timeline does **not** retire presentation semaphore waits: keep per-image presentation semaphores alive until swapchain retirement or a presentation-queue idle. The desktop regression exercises this complete lifecycle, including swapchain replacement and rebuilding graph imports.

## Staged transfers (item 41)

Uploads and readbacks are graph resources whose storage comes from the executor instead of the caller:

```cpp
RenderGraph graph;
auto mesh = graph.ImportBuffer(meshBuffer, State::ShaderRead);        // already initialized
AddBufferUpload(graph, "Patch vertices", bytes, mesh, 4096);         // partial: preserves the rest
auto texture = graph.CreateTexture(desc);
AddTextureUpload(graph, "Albedo mip 0", pixels, texture, wholeMip0);  // whole subresource: Write
auto result = AddTextureReadback(graph, "Capture", texture, wholeMip0);

RenderGraphExecutor executor(device);                               // optional {uploadBytes, readbackBytes}
executor.Execute(graph.Compile());
std::vector<std::byte> out(graph.GetDesc(result.Buffer).Size);
if (executor.TryReadback(result.Buffer, out) == Rhi::ReadbackStatus::Ready) { /* consume */ }
```

- `CreateUpload(desc, writer)` / `CreateUpload(bytes, name)` declare a host-written, GPU-read-only buffer. It is initialized at graph start (`HostWrite`), may be read in any state its usage allows (transfer source, vertex, index, uniform, storage, indirect), can never be written by a pass and cannot be exported. The writer runs inside `Execute`, after the predecessor wait and before recording, once per execution of a compiled graph that still reads it; an unused upload is never staged. The span overload copies the bytes into the compiled definition.
- `CreateReadback(desc)` declares a GPU-written, host-read buffer that is implicitly exported to `HostRead`. One pass must initialize it with a whole `Write` in `CopyDestination`. After a successful execution, `TryReadback`/`TryGetReadback` are nonblocking (`NotReady` until the execution's completion value). Results remain valid until the next `Execute`, `Trim` or destruction. `GetExported` rejects staged buffers.
- Staged buffers are **suballocations**. `RenderCommandContext::GetRange` returns `{Buffer, Offset, Size}` for any declared buffer (whole allocations report offset zero); `Get` rejects staged buffers so an offset cannot be forgotten. They are never pooled or aliased with transient resources. Barriers on them are whole-buffer, which is conservative and harmless for these host-visible allocations.
- The executor owns one upload arena (usage covers every GPU-read upload usage, so uniform/storage offset limits always apply) and one readback arena. It resets them only after its predecessor wait, flushes used upload bytes before submission, validates the readback batch before submission and binds it to the submission's own completion value through `Rhi::ReadbackSubmission`. If the current graph needs more staging, the arena is replaced **between** submissions with the next power of two (minimum 64 KiB) that holds a conservative bound of the staged sizes plus alignment padding. There is no in-flight growth, per-allocation spill or ring wrap. `RenderGraphExecutorDesc` optionally pre-reserves capacity; `Trim` releases both arenas.
- Failures publish nothing: a throwing writer or allocation failure leaves `Execute` before recording; flush/validation/submission failures leave the completion value unchanged, and the next execution resets the uncommitted staging.
- `ReadWrite` with `CopyDestination` now means a **partial copy that preserves** the bytes/texels it does not overwrite. It requires initialized contents (an earlier writer or a defined import) exactly like any other read. Whole-resource copies should use `Write`. `RenderGraphTransfers.h` applies this rule automatically: a copy covering the whole destination buffer or texture subresource is a `Write`, anything narrower is a `ReadWrite`.
- Transfer helpers: `AddBufferUpload` (span or writer), `AddTextureUpload`, `AddBufferReadback`, `AddTextureReadback`, plus `GetTextureCopyBytes`. Texture transfers are tightly packed, single-sample, single-subresource copies of uncompressed color formats whose texel size is a power of two (`RGB32*` formats and compressed/depth formats reject). Buffer copies need nonempty in-bounds ranges.

Staging lives on the graph's single graphics queue. Dedicated transfer-queue ownership (release/acquire) and asynchronous upload scheduling are still the open Phase 10 queue-ownership follow-up; `ImportBuffer/ImportTexture` still reject non-graphics owners.

## Diagnostics and tests

`CompiledRenderGraph::Dump()` provides deterministic text listing scheduled/culled passes, dependency IDs, physical allocation slots, resource lifetimes, import/export flags, and per-subresource state transitions. State values use the RHI enum bit values. It requires no device or native handles and can be written to a file by a tool.

Every live pass records a GPU label and, on queues supporting timestamps, a start/end timestamp pair. `ReadTimings()` waits before reading the query reset/write results and returns named durations in nanoseconds. Unsupported timestamp hardware returns absent durations. Timings include graph barriers inside the pass region; they are not CPU-calibrated clocks or evidence of async overlap.

CPU suites under `Source/Tests/Suites/RenderGraph` cover staged upload/copy/readback round trips, partial preserving copies, staging growth and reuse, writer/submission failure recovery, suballocation offsets and alignment, texture subresource transfers, dependencies, cycles, initialization, culling, mip/layer states, same-state barriers, lifetimes, pooling, imports/exports, immutable compilation, callback access checks, allocation/recording/submission failures, and completion-safe reuse. They run in the ordinary `SwimTests` corpus. Opt-in Vulkan cases add:

- `RenderGraphOffscreenPostPresentAndReuse`: offscreen rendering, a sampled post pass, sampled presentation, exact pixel readback, repeated execution, per-pass timestamps, and swapchain replacement.
- `RenderGraphComputeStorageAndReadback`: graph-scheduled initialization, two dependent Slang compute passes with a same-state storage barrier, exact readback, and repeated pooled-buffer reuse.
- `RenderGraphStagedTransfersAndReadback`: executor-staged buffer uploads (whole and preserving partial) into an imported persistent buffer, a texture upload plus partial patch, buffer/texture readbacks compared byte-for-byte across four executions, and upload-arena growth between submissions.
- `GeometryHeapPagedUploadAndRetirement`: batched GeometryHeap page uploads, metadata rows and page ranges read back in the same graph, deferred destruction and range reuse (see [GPU residency](GpuResidency.md)).

Run `SwimTests --filter=RenderGraph` for CPU coverage. On Windows, use `scripts/run-debug-tests.ps1 -SkipBuild -Validation core -Filter 'RHI.Vulkan.Smoke.RenderGraph*'`, then repeat with `sync`, `gpu`, and `all`. Always run core before GPU-assisted profiles. The existing exact GPU-AV startup advisory policy is unchanged.

The sandbox still uses the transitional renderer. The item 40 checkpoint supplied the modern graph consumer and native reference frame; item 41 adds graph-scheduled staging, items 42–43 build GPU registries and the GeometryHeap on it, and item 44 streams compiled meshes/textures into them (`TextureResidency` records its mip uploads as graph passes too). Bindless tables (45), GPU Scene and the final sandbox renderer migration remain later critical-path items. Existing direct-RHI smoke tests intentionally retain explicit barriers to test the lower-level API independently.
