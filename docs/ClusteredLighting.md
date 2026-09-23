# Clustered lighting

This covers three critical-path items:

- **item 64:** the cluster grid;
- **item 65:** GPU light assignment and compaction;
- **item 68:** the cluster heatmap and overflow diagnostics.

It consumes the [GPU light buffer](Lights.md) (item 63). [Clustered Forward+](ForwardPlus.md) (items 66–67) shades opaque and transparent surfaces from its lists through `ClusteredLighting.slang`, and the item 69 benchmarks below measure it.

```text
Renderer/ClusteredLighting
  ClusterGrid             grid desc/layout/record, log slices, pixel/depth -> cluster
  ClusterReference        CPU definition: view lights, cluster AABBs, assignment,
                          compaction, stats, clustered shading, heatmap colors
  ClusterBindings         binding slots and group sizes of the five programs
  ClusterGraphResources   the graph handles one view's assignment produced
  ClusteredLightAssigner  records the five compute passes (+ optional heatmap)
Shaders/Slang/ClusteredLighting
  ClusterGrid.slang       mirrors ClusterGrid (+ view-light/record/stats structs)
  ClusterLightCull / ClusterBounds / ClusterAssign / ClusterScan / ClusterHeatmap
  ClusteredLighting.slang ClusteredShade: directional lights + the cluster's list
```

`Renderer/ClusteredLighting` uses only RenderGraph, the RHI contract, `Renderer/Lights` and `StandardPbr`. It never includes visibility, environment, the material table, residency or assets, and none of the layers below it include it. `scripts/verify-build-layout.py` enforces this.

## The grid (item 64)

A `ClusterGridDesc` sets the viewport, `TileSize` (pixels per tile edge, default 64), `SliceCount` (default 24), `Near`/`Far` (default 0.1 / 500 m), `MaxLightsPerCluster` (64) and `IndexCapacity` (256 K). `ComputeClusterGridLayout` rounds tiles up, so edge tiles end at the viewport edge. It throws for zero sizes, `Near`/`Far` out of order, zero limits or more than 2²⁰ clusters.

- **Slices are logarithmic.** Slice *k* covers view depths [N·(F/N)^(k/S), N·(F/N)^((k+1)/S)), so every slice has the same depth ratio. The slice of a depth is ⌊log(d)·scale + bias⌋, clamped. Slice 0 reaches back to the eye and the last slice ends at `Far`.
- **Cluster index:** x fastest, then y (tiles from the top-left), then slice.
- **View-relative.** `MakeClusterGridRecord(desc, {View, Projection})` stores the world→view rows and the projection terms the passes need. Any perspective depth mapping works: forward, reverse-Z or infinite far. It throws for a non-perspective projection or a non-affine view.
- **Depth decoding.** `ClusterViewDepthFromNdc` recovers view depth d = P23 / (z + P22) from a stored depth value. The heatmap and Forward+ use it to find a pixel's cluster.
- **Resizing** is a new desc. The record is rebuilt and uploaded every frame, so nothing is cached across resolutions.

`ClusterGridRecord` is 128 bytes (std430). `ClusterRecord` is 16 bytes (`Offset`, `Count`, `RawCount`). `ClusterStats` is 32 bytes.

## Assignment and compaction (item 65)

`ClusteredLightAssigner::Record(graph, lights.Import(graph), gridDesc, view)` schedules five compute passes. There are no atomics, so the result is deterministic and `Clustering::AssignLights` reproduces it exactly.

| Pass | Work | Output |
| --- | --- | --- |
| 1. Cull | Each local light's `LightBoundingSphere` goes to view space and is tested against the clustered volume's AABB | `ViewLights` (center, radius; negative radius = culled) |
| 2. Bounds | Each cluster's view-space AABB: the tile's four edge rays at the slice's near and far depths | `Bounds` |
| 3. Count | One thread per cluster walks every local light in index order, 64 at a time through groupshared memory, and counts the spheres touching its AABB | `RawCount`, `Count = min(RawCount, MaxLightsPerCluster)` |
| 4. Scan | One 256-thread group prefix-sums `Count` into offsets in cluster order, clamps each list to `IndexCapacity` and reduces the stats | `Offset`, final `Count`, `ClusterStats` |
| 5. Write | The same walk writes the first `Count` hits' local indices at `Offset` | `Indices` |

- **Point and spot lights** are both assigned through their bounding spheres. Spot spheres are the tight cone bounds from item 63.
- **Directional lights** stay outside the lists. `ClusteredShade` loops them separately.
- **Lists are compact** and in light-index order. The index buffer holds exactly the requested indices, up to `IndexCapacity`.
- **No per-object CPU light list** exists anywhere in the path.

### Overflow is bounded and visible

There are two overflow limits:

- **`MaxLightsPerCluster`** truncates a long list to its first lights. The cluster counts in `OverflowClusters`, and `RawCount` keeps the true count.
- **`IndexCapacity`** clamps offsets and counts, so the last clusters receive shorter or empty lists. The missing entries are counted in `DroppedIndices`.

Nothing is ever written out of bounds, and a truncated cluster only loses light: it never gains any. `ClusterStats` also reports visible lights, requested and written indices, the maximum raw count and the number of non-empty clusters.

## Heatmap (item 68)

`RecordHeatmap(graph, clusters, depth)` takes a viewport-sized sampled `D32Float` or `R32Float` depth. It writes an `RGBA8Unorm` image that colors each pixel by its cluster's light count:

- a ramp of `Count / MaxLightsPerCluster` from blue through green to red;
- **magenta** where the list was truncated;
- transparent where the cluster is empty or the depth is past `Far`, which includes the sky.

`Clustering::HeatmapPixel` is the CPU definition.

## Tests

| Suite | What it proves |
| --- | --- |
| `Render.ClusterGrid` (3) | Layout rounding and validation. Logarithmic slices with equal ratios. Depth decoding for forward and reverse-Z. Every pixel's view point lies in its own cluster's AABB, and the AABBs tile the volume |
| `Render.ClusteredLights` (4) | Assignment is conservative (each lit point's light is in its cluster's list), compact and ordered. Clustered shading equals `ShadeAllLights`. Truncation and capacity overflow are bounded, counted and only remove light. Heatmap colors. The item 69 scaling scenarios (below) |
| `Render.ClusteredLightAssigner` (2) | On the mock device: five passes in order with the right bindings, push modes and dispatch sizes; heatmap validation |
| `ShaderCompiler.ClusterLayout` (1) | Every program's bindings and group size match `ClusterBindings.h`. The grid, record and stats layouts equal the C++ structs |
| Native `ClusteredLightingMatchesTheCpuReference` | Three frames over 2 directional + 1,500 local lights. See below |

The native smoke compares the view lights, every cluster AABB, list and record, the stats, a 1,024-point shading probe (clustered vs brute force vs `ShadeAllLights`) and a full heatmap with the CPU definitions. Its three frames are:

- 1280×720 with room for every list;
- the same view at 4 lights per cluster and 4,096 index slots, which forces truncation, capacity overflow and a magenta heatmap;
- 800×450 after 300 lights moved.

The CPU assignment is fed the GPU's own view lights and bounds, so only lights grazing an AABB within float rounding may differ.

## Scaling (item 69)

The count and write passes walk every local light for every cluster, so their cost grows with lights × clusters. A light the cull pass rejected (off-screen) costs one groupshared load and a skip; a visible one costs a sphere/AABB test. The benchmarks characterize the Phase 15 scenarios:

- **CPU (`Render.ClusteredLights.ScalingScenariosKeepStatisticsConsistentAndBounded`):** 0 lights, 1k and 10k spread through the view, 10k behind the camera, and 1k packed into a 4 m ball, on a 640×360 grid. Every `ClusterStats` field is checked against its definition. The empty and off-screen scenes write nothing, 10k overflows both limits (bounded and counted), and the dense ball overflows its clusters.
- **Native (`RHI.Vulkan.Smoke.ClusteredLightingScalesToTensOfThousandsOfLights`):** 0, 1k, 10k and 32k lights through a 1280×720 × 24-slice grid, plus 10k off-screen and 10k dense. Each scenario runs three frames. The last frame's GPU timestamps are printed per pass, and its statistics are checked against the CPU cull and `ClusterStats`' identities.
- **Forward+ (`ClusteredForwardPlusMatchesTheCpuReference`, last frame):** 10,000 lights rendered and compared, with the cluster, opaque, sort and transparent pass times printed.

Budgets (`MaxLightsPerCluster`, `IndexCapacity`, slice count) should be set from the desktop numbers these print. The validation records keep them.
