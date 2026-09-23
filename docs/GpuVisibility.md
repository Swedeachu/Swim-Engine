# GPU visibility and indirect draw generation

This covers critical-path items **49** (GPU frustum culling), **52** (LOD selection with hysteresis), **53** (visible compaction), **54** (material/pass binning), **55** (indirect command/count generation) and **57** (visibility diagnostics and benchmark). Items **50/51** (HZB and occlusion) wait for the final reverse-Z/depth convention, and item **56** (removing the CPU visible list from normal world rendering) waits for the modern renderer to draw the sandbox.

```text
Renderer/Visibility     GpuVisibility, RunVisibilityReference, VisibilityMath, GpuViewRecord
        |               (Shaders/Slang/GpuScene/GpuVisibility.slang)
Renderer/GpuScene       GpuInstanceRecord / GpuTransformRecord rows
Renderer/Geometry       GpuMeshMetadata (LODs), GpuSubmeshRecord (draw ranges), index pages
Renderer/RenderGraph    passes, transient buffers, staged uploads, readbacks
RHI                     DrawIndexedIndirect / DrawIndexedIndirectCount
```

`Renderer/Visibility` compiles with the backend-neutral renderer sources. It includes no backend, EnTT, scene, residency, asset, IO or job header, and RenderGraph/Resources/Geometry/GpuScene never include it; `scripts/verify-build-layout.py` enforces both directions.

## RHI: indexed indirect draws

`Rhi::CommandList` gained two calls, with `DrawIndexedIndirectCommand` (20 bytes: `IndexCount`, `InstanceCount`, `FirstIndex`, `VertexOffset`, `FirstInstance`) as the argument layout:

| Call | Validation (Vulkan backend) |
| --- | --- |
| `DrawIndexedIndirect(args, offset, drawCount, stride = 20)` | inside rendering with a bound pipeline, an index buffer and all vertex bindings; `args` has `BufferUsage::Indirect`; offset 4-byte aligned; stride ≥ 20 and a multiple of 4; the last command fits in the buffer; `drawCount ≤ maxDrawIndirectCount`. `drawCount == 0` records nothing. |
| `DrawIndexedIndirectCount(args, offset, count, countOffset, maxDrawCount, stride = 20)` | the same, plus `count` has `Indirect` usage and a 4-byte aligned, in-range `countOffset`; `maxDrawCount` bounds the range check. Requires `GraphicsCapabilities::IndirectCount`. |

The Vulkan device now always enables `multiDrawIndirect` and `drawIndirectFirstInstance`, which the draw-slot scheme below relies on.

## Views

`BuildGpuViewRecord(RenderViewDesc)` produces the 192-byte `GpuViewRecord`:

- `ViewProjection` (row-major; `MultiplyRowMajor`, `OrthographicRowMajor`, `PerspectiveRowMajor` help build it);
- six frustum planes extracted with Gribb–Hartmann for clip depth `[0, 1]`, so the same code works for ordinary and reverse-Z projections (a degenerate plane becomes the always-inside `(0, 0, 0, 1)`);
- camera position, `LodScale` (pixels per world unit at distance 1, for example `viewportHeight / (2 tan(fovY / 2))`), `LodPixelError`, `LodHysteresis`;
- `Flags`: `DisableFrustumCulling`, `ResetLodHistory` (set on camera cuts and teleports).

## Rules

`VisibilityMath.h` states each rule once. `RunVisibilityReference` uses it on the CPU, and `GpuVisibility.slang` mirrors it line for line.

1. **Drawable.** A row is tested only when `Live | HasMesh | Visible` are all set (`NotDrawable` counts the rest).
2. **Frustum.** The local bounds become a world sphere (center through the current transform, radius = local half-extent length × largest axis scale). A sphere is culled when it is fully behind any plane. Bounds with an extent ≥ `RenderBounds::Unbounded / 2` are never culled.
3. **LOD.** Projected error per world unit is `LodScale / max(distance − radius, 1e-4)`. The threshold is `LodPixelError × exp2(instance.LodBias)`. The chosen LOD is the coarsest whose `GpuMeshLod::Error × scale ≤ threshold`. A mesh with `LodCount == 0` is one LOD covering its submesh range.
4. **Hysteresis.** Each row keeps `GpuLodState {Generation, Lod}` in a persistent buffer. With valid history (same instance generation, no reset flag), a row moves coarser only when the LOD is still acceptable at `threshold × (1 − h)`, and finer only when the current LOD fails at `threshold × (1 + h)`. Reused rows (new generation), the first frame and `ResetLodHistory` start without history.
5. **Binning.** The material set goes through the material-bin table (`SetMaterialBin`; unmapped or out-of-range sets go to bin 0). The mesh's index page selects the page slot in `VisibilityFrameDesc::IndexPages`. Bin = `materialBin × pageSlots + slot`. A mesh whose index page is not listed counts as `OtherPage` and is not drawn in this view. Each bin therefore needs exactly one index buffer, and vertices are pulled from the vertex page by the draw shader.
6. **Compaction and commands.** For every submesh of the chosen LOD, the shader `InterlockedAdd`s the bin's counter. Slots below the bin's capacity get one command, `{IndexCount, 1, FirstIndex, VertexOffset, FirstInstance = bin.First + slot}`, plus a `GpuDrawRecord {InstanceRow, SubmeshRow}` at the same slot. Overflowing draws are counted as `Dropped`. The count buffer keeps the attempted count, and `DrawIndexedIndirectCount` clamps it to the bin's `maxDrawCount`. The order within a bin is unspecified.

Draw shaders use `SV_VulkanInstanceID` (which includes `FirstInstance`) as the draw slot and read `DrawRecords[slot]` to find the GPU Scene row and submesh. `Shaders/Slang/RhiSmoke/GpuDrivenDraw.slang` is the reference consumer. As with every RHI draw, clip space is +Y up (the Vulkan backend flips the viewport), so framebuffer row 0 is NDC y = +1.

## Bins

`VisibilityBinLayout(materialBinCapacities, indexPageSlots)` gives each (material bin, page slot) pair a fixed `VisibilityBinRange {First, Capacity}`: the prefix sum of the material bin's capacity repeated per page slot. The command and draw-record buffers hold `GetTotalCapacity()` entries, so memory is bounded regardless of scene size. An empty capacity list or a zero capacity is rejected.

## GpuVisibility

`GpuVisibilityDesc`: `CullPipeline` + `Layout` (from `GpuVisibility.slang`), descriptor `Space`, `MaxObjects`, `MaxMaterialSets`, `MaterialBinCapacities`, `IndexPageSlots`, `DebugName`. The object owns two persistent device buffers: the material-bin table and the LOD history (`MaxObjects` entries).

`Record(graph, sceneResources, geometryResources, frame)` adds, in order:

| Pass | When |
| --- | --- |
| Clear counts and statistics (copies from a zero upload) | every frame |
| Upload the material-bin table | only after `SetMaterialBin` changed a value |
| Zero the LOD history | the first frame |
| Cull/LOD/bin/compact compute pass, `ceil(RowCount / 64)` groups, 16-byte push constants (row count, bin count, material set count, page slot count) | every frame |
| Statistics readback (`VisibilityGraphResources::StatsReadback`) | when `frame.ReadStats` |

The view record, bin ranges and page table are small per-frame uploads, so the CPU cost of a frame does not depend on the number of objects. The returned `VisibilityGraphResources` carry transient `Commands` (Indirect | Storage), `DrawRecords`, `Counts` (Indirect | Storage, one uint per bin), `Stats`, the cull pass and a pointer to the bin layout. Later passes read `Commands`/`Counts` as `IndirectArgument` and `DrawRecords` as `ShaderRead`.

`Record` throws `std::length_error` when the scene has more rows than `MaxObjects`, and `std::invalid_argument` when more index pages are listed than slots. The binding contract is `GpuVisibilityBindings` (0–12: Instances, Transforms, Meshes, Submeshes, View, MaterialBins, BinRanges, IndexPages, LodState, Commands, DrawRecords, Counts, Stats). `ShaderCompiler.GpuSceneLayout.VisibilityProgramMatchesItsCppContract` checks the compiled shader's bindings, element sizes, field offsets, thread group size and push constants against the C++ side.

## Diagnostics (item 57)

`VisibilityStats` (16 uints): `Tested`, `FrustumCulled`, `NotDrawable`, `Visible`, `Draws`, `Dropped`, `OtherPage`, a reserved slot, and `LodCounts[8]`. It is written with atomics and read back through the executor's readback arena without stalling the frame. Every tested row is counted exactly once (`FrustumCulled + NotDrawable + Visible == Tested`). `Dropped > 0` means a bin capacity is too small.

`Render.Visibility.HundredThousandObjectBenchmarkKeepsStatisticsConsistent` runs the CPU definition over 100k rows and prints its timing (about 10 ms per frame in Debug). The native smoke checks the GPU against it.

## Tests

| Suite | What it proves |
| --- | --- |
| `Render.Visibility` (5) | plane extraction for both projections, culling/drawability counts, the LOD hysteresis sequence including reset, generation change and bias, binning/capacity/other-page, the 100k benchmark |
| `Render.GpuVisibility` (3) | pass and upload counts per frame (persistent tables only when needed), the 13 bindings, indirect usages, rejection of invalid configurations and frames, record layouts |
| `RHI.Vulkan.IndirectDraw` (2) | argument forwarding and every validation rule for both calls |
| `ShaderCompiler.GpuSceneLayout` (+1) | C++ ↔ Slang layout of every visibility record |
| `RHI.Vulkan.Smoke.GpuVisibilityCullsBinsAndDrawsIndirect` (opt-in) | a 64×64 grid of two-LOD quads on a real device over three frames (camera moves, hidden/destroyed/moved/re-binned objects, a camera cut): the commands, draw records, counts and statistics match the CPU reference; an overflowing bin clamps; both LODs are used; every drawn object's pixels carry its id and nothing culled, hidden, dropped or destroyed appears |

## Not yet done

- HZB build and occlusion culling (items 50/51) after the depth-convention gate.
- The engine runtime constructing `GpuScene` + `GpuVisibility` and drawing the world from them (item 56 then retires `SceneBVH` visibility and the CPU visible list).
- Per-pass bins (shadow/depth-only views), meshlet/cone culling, and a non-count fallback for devices without `IndirectCount`.
