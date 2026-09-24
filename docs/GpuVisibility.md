# GPU visibility and indirect draw generation

This covers critical-path items **49** (GPU frustum culling), **50** (depth/HZB build on the final reverse-Z convention), **51** (occlusion culling with history invalidation), **52** (LOD selection with hysteresis), **53** (visible compaction), **54** (material/pass binning), **55** (indirect command/count generation) and **57** (visibility diagnostics and benchmarks). Item **56** (removing the CPU visible list from normal world rendering) waits for the modern renderer to draw the sandbox.

```text
Renderer/Visibility     GpuVisibility, HzbBuilder, RunVisibilityReference, HzbReference,
        |               VisibilityMath, GpuViewRecord, DepthConvention
        |               (Shaders/Slang/GpuScene/GpuVisibility.slang, HzbReduce.slang)
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

## Depth convention (the item 50 gate)

The modern renderer's depth convention is now final (`DepthConvention.h`):

| | Canonical: `DepthConvention::ReverseZ` | `Forward` (opt-in per view) |
| --- | --- | --- |
| Near / far plane | depth 1 / depth 0 (infinite far → 0) | depth 0 / depth 1 |
| Depth format | `CanonicalDepthFormat` = `D32Float` | any |
| Clear value | `DepthClearValue` = 0 | 1 |
| Depth test | `DepthCompareOp` = `GreaterEqual` | `LessEqual` |
| "Farther" (what the HZB keeps) | the minimum | the maximum |

`OrthographicReverseZRowMajor` and `PerspectiveReverseZRowMajor` (infinite far plane: depth = near / distance) build canonical projections. Clip space stays right-handed with depth in [0, 1] and +Y up; the Vulkan backend flips the viewport, never the projection. `RenderViewDesc::Depth` records the convention, and `BuildGpuViewRecord` sets `GpuViewFlags::ForwardDepth` only for Forward views. Frustum culling is convention-agnostic (both planes come from the same Gribb–Hartmann rows); only the HZB reduction and the occlusion test read the flag. The transitional renderer keeps its own depth setup until it is retired.

## Views

`BuildGpuViewRecord(RenderViewDesc)` produces the 192-byte `GpuViewRecord`:

- `ViewProjection` (row-major; `MultiplyRowMajor`, `OrthographicRowMajor`, `PerspectiveRowMajor` help build it);
- six frustum planes extracted with Gribb–Hartmann for clip depth `[0, 1]`, so the same code works for ordinary and reverse-Z projections (a degenerate plane becomes the always-inside `(0, 0, 0, 1)`);
- camera position, `LodScale` (pixels per world unit at distance 1, for example `viewportHeight / (2 tan(fovY / 2))`), `LodPixelError`, `LodHysteresis`;
- `Flags`: `DisableFrustumCulling`, `ResetLodHistory`, `ResetOcclusionHistory` (`CameraCut` sets both, for camera cuts and teleports), `ForwardDepth`, `DisableOcclusion` (debug: the late phase skips the HZB test), `ShadowCasters` (a shadow view: only rows with `RenderObjectFlags::CastShadows` are drawable; see [Shadows](Shadows.md#rendering-casters)).

## Rules

`VisibilityMath.h` states each rule once. `RunVisibilityReference` uses it on the CPU, and `GpuVisibility.slang` mirrors it line for line.

1. **Drawable.** A row is tested only when `Live | HasMesh | Visible` are all set, plus `CastShadows` in a `ShadowCasters` view (`NotDrawable` counts the rest).
2. **Frustum.** The local bounds become a world sphere (center through the current transform, radius = local half-extent length × largest axis scale). A sphere is culled when it is fully behind any plane. Bounds with an extent ≥ `RenderBounds::Unbounded / 2` are never culled.
3. **LOD.** Projected error per world unit is `LodScale / max(distance − radius, 1e-4)`. The threshold is `LodPixelError × exp2(instance.LodBias)`. The chosen LOD is the coarsest whose `GpuMeshLod::Error × scale ≤ threshold`. A mesh with `LodCount == 0` is one LOD covering its submesh range.
4. **Hysteresis.** Each row keeps `GpuLodState {Generation, Lod}` in a persistent buffer. With valid history (same instance generation, no reset flag), a row moves coarser only when the LOD is still acceptable at `threshold × (1 − h)`, and finer only when the current LOD fails at `threshold × (1 + h)`. Reused rows (new generation), the first frame and `ResetLodHistory` start without history.
5. **Binning.** The material set goes through the material-bin table (`SetMaterialBin`; unmapped or out-of-range sets go to bin 0). The mesh's index page selects the page slot in `VisibilityFrameDesc::IndexPages`. Bin = `materialBin × pageSlots + slot`. A mesh whose index page is not listed counts as `OtherPage` and is not drawn in this view. Each bin therefore needs exactly one index buffer, and vertices are pulled from the vertex page by the draw shader.
6. **Compaction and commands.** For every submesh of the chosen LOD, the shader `InterlockedAdd`s the bin's counter. Slots below the bin's capacity get one command, `{IndexCount, 1, FirstIndex, VertexOffset, FirstInstance = bin.First + slot}`, plus a `GpuDrawRecord {InstanceRow, SubmeshRow}` at the same slot. Overflowing draws are counted as `Dropped`. The count buffer keeps the attempted count, and `DrawIndexedIndirectCount` clamps it to the bin's `maxDrawCount`. The order within a bin is unspecified.

Draw shaders use `SV_VulkanInstanceID` (which includes `FirstInstance`) as the draw slot and read `DrawRecords[slot]` to find the GPU Scene row and submesh. `Shaders/Slang/RhiSmoke/GpuDrivenDraw.slang` is the reference consumer. As with every RHI draw, clip space is +Y up (the Vulkan backend flips the viewport), so framebuffer row 0 is NDC y = +1.

## HZB (item 50)

`HzbBuilder` (with `HzbReduce.slang`) turns a sampled depth texture (`D32Float` read through a depth-aspect view, or an `R32Float` copy) into a transient `R32Float` pyramid, one graph compute pass per mip:

- Mip *i* is depth level *i* + 1. Each level halves the previous one, rounding up, down to 1×1 (`ComputeHzbMips`).
- Texel (x, y) of a level keeps the farthest depth of its 2×2 footprint in the level below, clipped at odd edges. A texel of level L therefore bounds exactly the depth texels [x·2^L, (x+1)·2^L) in each axis.
- Pass *i* reads mip *i*−1 (or the depth) as `ShaderRead` and writes mip *i* as `ShaderWrite` through single-mip views. The graph tracks each mip separately, so no manual barriers are needed.

`HzbGraphResources` carries the pyramid, the depth size, the mip count and the convention. `HzbReference` is the CPU definition (`Build`, `Reduce`, `FromMips` for GPU readbacks, `Fetch`).

## Two-phase occlusion (item 51)

With occlusion enabled, a frame records `GpuVisibility` twice, both times on the same graph and view (`VisibilityPhase`):

```text
Early cull  -> draw (clear color/depth) -> HzbBuilder -> Late cull -> draw (load color/depth)
```

- **Per-row history.** `GpuVisibility` keeps an occlusion history buffer (`MaxObjects` uints). A row's entry holds its generation when the late phase last found it visible, else 0. A reused row (new generation) therefore starts without history.
- **Early phase.** It draws the in-frustum rows that were visible last frame. Every other in-frustum row is counted as `Deferred`.
- **Late phase.** It tests every in-frustum row against this frame's HZB, built from the early depth, and rewrites the history. Rows already drawn early are counted as `AlreadyDrawn`. Hidden rows are counted as `Occluded`. The remaining rows are drawn.
- **Occlusion test** (`VisibilityMath::OccludedByHzb`, mirrored by the shader):
  - Project the world sphere's AABB.
  - Never occlude an object that crosses the near plane, lies behind the camera, is entirely off screen, or is unbounded.
  - Pick the finest level whose footprint spans at most 2×2 texels.
  - The object is occluded only if the farthest stored depth is strictly nearer than the object's nearest point.
- **Correctness for newly visible objects.** The late test uses the current frame's depth, so a newly visible or teleported object is never hidden by stale data; at worst it is drawn a phase later. Objects visible last frame are drawn early whether or not they are still visible (conservative).
- **Camera cuts and first frames.** A camera cut (`ResetOcclusionHistory`, part of `CameraCut`) makes the early phase draw everything in the frustum. The first frame is equally safe: its history is zero, so the early phase draws nothing, the HZB is empty and the late phase draws everything.
- **Sharing and binding.** The two phases share one import of the persistent buffers per graph. The late phase binds the whole pyramid (binding 13). The other phases bind a 1×1 stand-in that `GpuVisibility` owns.

A `Single` phase keeps the pre-occlusion behavior (no HZB, history untouched).

## Drawing the bins, and the no-count fallback

`DrawVisibilityBin(list, commands, counts, bins, bin, path)` (`VisibilityDraws.h`) issues one bin after the caller has bound the pipeline, descriptors and the bin's index page. `SelectVisibilityDrawPath(capabilities)` chooses the path, and a device only gets the fallback when it genuinely lacks `IndirectCount`:

| Path | Draw call | Frame setting |
| --- | --- | --- |
| `IndirectCount` (fast path) | `DrawIndexedIndirectCount` over the bin's range, limited by the GPU count | none |
| `ZeroFilledIndirect` (fallback) | `DrawIndexedIndirect` over the bin's whole capacity | `VisibilityFrameDesc::ZeroUnusedCommands = NeedsZeroedCommands(path)` |

With `ZeroUnusedCommands`, the phase's clear pass also zeroes the whole command buffer, and the cull pass declares it `ReadWrite` so that zeroed slots survive. Slots the cull does not write therefore draw zero instances. The cost is one staged clear of `capacity × 20` bytes per phase, so the flag stays off on the fast path.

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
| Zero the occlusion history and upload the 1×1 stand-in HZB | the first frame |
| Cull/LOD/bin/compact compute pass, `ceil(RowCount / 64)` groups | every phase |
| Statistics readback (`VisibilityGraphResources::StatsReadback`) | when `frame.ReadStats` |

The view record, bin ranges and page table are small per-frame uploads, so the CPU cost of a frame does not depend on the number of objects. The returned `VisibilityGraphResources` carry transient `Commands` (Indirect | Storage), `DrawRecords`, `Counts` (Indirect | Storage, one uint per bin), `Stats`, the cull pass and a pointer to the bin layout. Later passes read `Commands`/`Counts` as `IndirectArgument` and `DrawRecords` as `ShaderRead`.

`Record` throws `std::length_error` when the scene has more rows than `MaxObjects`, and `std::invalid_argument` when more index pages are listed than slots. The binding contract is `GpuVisibilityBindings` (0–14: Instances, Transforms, Meshes, Submeshes, View, MaterialBins, BinRanges, IndexPages, LodState, Commands, DrawRecords, Counts, Stats, Hzb, OcclusionHistory; 32-byte push constants: row count, bin count, material set count, page slot count, phase, HZB width/height/mip count). `Record` also rejects a late phase without an HZB of the view's convention. `ShaderCompiler.GpuSceneLayout.VisibilityProgramMatchesItsCppContract` checks the compiled shader's bindings, element sizes, field offsets, thread group size and push constants against the C++ side.

## Diagnostics (item 57)

`VisibilityStats` (20 uints): `Tested`, `FrustumCulled`, `NotDrawable`, `Visible`, `Draws`, `Dropped`, `OtherPage`, `Occluded`, `Deferred`, `AlreadyDrawn`, two reserved slots and `LodCounts[8]`. It is written with atomics and read back through the executor's readback arena without stalling the frame. Every tested row lands in exactly one bucket per phase:

- Single: `FrustumCulled + NotDrawable + Visible == Tested`.
- Early: the same, plus `Deferred`.
- Late: the same, plus `AlreadyDrawn + Occluded`.

`Dropped > 0` means a bin capacity is too small.

`Render.Visibility.HundredThousandObjectBenchmarkKeepsStatisticsConsistent` runs the CPU definition over 100k rows and prints its timing (about 10 ms per frame in Debug). `Render.Occlusion.HundredThousandObjectBenchmarkShowsDrawSavings` runs the two-phase pipeline on the CPU over 90k objects with a wall covering about 36% of the view. In steady state it draws 57,960 objects and occludes 32,041 (35.6% of draws saved), with every visible object still drawn once. The native smokes check the GPU against both definitions.

## Tests

| Suite | What it proves |
| --- | --- |
| `Render.Visibility` (7) | plane extraction for both projections, culling/drawability counts, the `ShadowCasters` drawable rule, the LOD hysteresis sequence including reset, generation change and bias, binning/capacity/other-page, the 100k benchmark, and the two-phase sequence (fresh history, steady state, teleported occluder revealing objects in the same frame, camera cut, `DisableOcclusion`, reused rows) on a CPU depth rasterizer |
| `Render.GpuVisibility` (5) | pass and upload counts per frame (persistent tables only when needed), the 15 bindings, indirect usages, rejection of invalid configurations and frames, record layouts; early + late phases in one graph sharing the history, the late phase binding the whole pyramid, phase/HZB push constants; draw-path selection, the command clear of the no-count fallback and `DrawVisibilityBin` on both paths |
| `Render.DepthConvention` (1), `Render.Hzb` (2), `Render.Occlusion` (3) | the canonical convention and reverse-Z projections; mip sizes and brute-force footprint equality of every HZB texel (both conventions); the builder's per-mip passes, views and constants; the occlusion test is conservative against full-resolution depth for 6,000 random spheres; near-plane, off-screen, unbounded and partial-coverage objects are never occluded; the 90k-object benchmark |
| `RHI.Vulkan.IndirectDraw` (2) | argument forwarding and every validation rule for both calls |
| `ShaderCompiler.GpuSceneLayout` (+2) | C++ ↔ Slang layout of every visibility record and binding; the HZB program's bindings, `r32f` storage format, 8×8 groups and push constants |
| `RHI.Vulkan.Smoke.GpuVisibilityCullsBinsAndDrawsIndirect` (opt-in) | a 64×64 grid of two-LOD quads on a real device over three frames (camera moves, hidden/destroyed/moved/re-binned objects, a camera cut): the commands, draw records, counts and statistics match the CPU reference; an overflowing bin clamps; both LODs are used; every drawn object's pixels carry its id and nothing culled, hidden, dropped or destroyed appears; a fourth frame repeats the view on the no-count fallback, whose unwritten slots must be zero-instance commands |
| `RHI.Vulkan.Smoke.GpuOcclusionTwoPhaseHzbRevealsNewlyVisibleObjects` (opt-in) | a 20×20 grid behind a wall, reverse-Z depth, the full early → draw → HZB → late → draw pipeline over five frames (fresh history, steady state, the wall teleporting away, the wall back with a camera cut): early and late statistics and draw sets equal the CPU reference fed with the GPU's own HZB; every HZB mip equals the CPU reduction of the mip below; 36 covered objects stop being drawn; revealed objects are drawn in the same frame; no object is ever drawn twice or missing from the image |

## Not yet done

- A GPU timing benchmark for occlusion on real scenes, per-view histories for several views of one scene (one `GpuVisibility` per view today), and meshlet/cone culling.
- The engine runtime constructing `GpuScene` + `GpuVisibility` and drawing the world from them (item 56 then retires `SceneBVH` visibility and the CPU visible list).
- Shadow views share one `GpuVisibility` instance with its own material bins (Opaque, Masked, Excluded) and reset LOD history per view (`ResetLodHistory`); occlusion culling for shadow casters is not implemented.
