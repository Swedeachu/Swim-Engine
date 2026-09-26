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
  ClusterBindings         binding slots and group sizes of the programs
  ClusterGraphResources   the graph handles one view's assignment produced
  ClusteredLightAssigner  records the four compute passes (+ optional heatmap)
Shaders/Slang/ClusteredLighting
  ClusterGrid.slang       mirrors ClusterGrid (+ view-light/record/stats structs)
  ClusterLightCull / ClusterBounds / ClusterAssign / ClusterScan / ClusterHeatmap
  ClusteredLighting.slang ClusteredShade: directional lights + the cluster's bitmask
```

`Renderer/ClusteredLighting` uses only RenderGraph, the RHI contract, `Renderer/Lights` and `StandardPbr`. It never includes visibility, environment, the material table, residency or assets, and none of the layers below it include it. `scripts/verify-build-layout.py` enforces this.

## The grid (item 64)

A `ClusterGridDesc` sets the viewport, `TileSize` (pixels per tile edge, default 64), `SliceCount` (default 24; the runtime uses 32), `Near`/`Far` (default 0.1 / 500 m), `MaxLightsPerCluster` (64; only the heatmap's full scale) and `LightCapacity` (1024; the local lights the bitmasks address, at most 2¹⁶). `ComputeClusterGridLayout` rounds tiles up, so edge tiles end at the viewport edge. It throws for zero sizes, `Near`/`Far` out of order, zero limits or more than 2²⁰ clusters.

- **Slices are logarithmic.** Slice *k* covers view depths [N·(F/N)^(k/S), N·(F/N)^((k+1)/S)), so every slice has the same depth ratio. The slice of a depth is ⌊log(d)·scale + bias⌋, clamped. Slice 0 reaches back to the eye and the last slice ends at `Far`.
- **Cluster index:** x fastest, then y (tiles from the top-left), then slice.
- **View-relative.** `MakeClusterGridRecord(desc, {View, Projection})` stores the world→view rows and the projection terms the passes need. Any perspective depth mapping works: forward, reverse-Z or infinite far. It throws for a non-perspective projection or a non-affine view.
- **Depth decoding.** `ClusterViewDepthFromNdc` recovers view depth d = P23 / (z + P22) from a stored depth value. The heatmap and Forward+ use it to find a pixel's cluster.
- **Resizing** is a new desc. The record is rebuilt and uploaded every frame, so nothing is cached across resolutions.

`ClusterGridRecord` is 128 bytes (std430); `Limits` holds the viewport, `MaxLightsPerCluster` and the mask word count `ceil(LightCapacity / 32)`. `ClusterRecord` is 16 bytes (`Offset`, `Count`, `RawCount`). `ClusterStats` is 32 bytes.

## Assignment: per-cluster light bitmasks (item 65)

`ClusteredLightAssigner::Record(graph, lights.Import(graph), gridDesc, view)` schedules four compute passes. `Clustering::AssignLights` reproduces the result exactly.

| Pass | Work | Output |
| --- | --- | --- |
| 1. Cull | Each local light's `LightBoundingSphere` goes to view space and is tested against the clustered volume's AABB | `ViewLights` (center, radius; negative radius = culled) |
| 2. Bounds | Each cluster's view-space AABB: the tile's four edge rays at the slice's near and far depths | `Bounds` |
| 3. Masks | One thread per (cluster, mask word) tests its 32 lights' spheres against the cluster's AABB and writes the word; the first thread clears `ClusterStats` | mask words |
| 4. Summary | One thread per cluster sets the occupancy words (which mask words hold lights) and the record; atomics fold the statistics | occupancy words, `Offset`, `Count`, `ClusterStats` |

Each cluster owns a fixed block of `ClusterBlockWords = OccupancyWords + MaskWords` uints at `Offset = cluster × ClusterBlockWords` in the light-index buffer (`Indices`):

- bit *i* of mask word *w* is local light 32·*w* + *i*;
- bit *j* of occupancy word *o* marks a non-zero mask word 32·*o* + *j*, so shading walks only the words that hold lights (`Clustering::ForEachClusterLight`, `ClusteredShade` and `ForwardDirect` visit lights in increasing index order).

- **Nothing is ever truncated.** Every light whose sphere touches a cluster is in its set, however many there are. The old list layout capped each cluster at `MaxLightsPerCluster` and dropped the rest in index order, so neighbouring clusters kept different subsets of a dense light swarm and the screen showed square, darker tiles. Memory is bounded instead: clusters × `LightCapacity` / 8 bytes (16 K clusters and 4096 lights: 8 MiB). The runtime sets `LightCapacity` to the frame's local light count rounded up to 256; `Record` and `AssignLights` throw when more lights are bound than the masks address.
- **Point and spot lights** are both assigned through their bounding spheres. Spot spheres are the tight cone bounds from item 63.
- **Directional lights** stay outside the clusters. `ClusteredShade` loops them separately.
- **No per-object CPU light list** exists anywhere in the path.

`ClusterStats` reports visible lights, the summed light references (`RequestedIndices` = `WrittenIndices`), clusters above the heatmap scale (`OverflowClusters`, informational), the maximum per-cluster count and the non-empty clusters. `DroppedIndices` is always 0 and kept for the buffer layout.

## Heatmap (item 68)

`RecordHeatmap(graph, clusters, depth)` takes a viewport-sized sampled `D32Float` or `R32Float` depth. It writes an `RGBA8Unorm` image that colors each pixel by its cluster's light count:

- a ramp of `Count / MaxLightsPerCluster` from blue through green to red;
- **magenta** where a list was truncated (never, since the bitmask assignment);
- transparent where the cluster is empty or the depth is past `Far`, which includes the sky.

`Clustering::HeatmapPixel` is the CPU definition.

## Tests

| Suite | What it proves |
| --- | --- |
| `Render.ClusterGrid` (3) | Layout rounding and validation. Logarithmic slices with equal ratios. Depth decoding for forward and reverse-Z. Every pixel's view point lies in its own cluster's AABB, and the AABBs tile the volume |
| `Render.ClusteredLights` (4) | Assignment is conservative (each lit point's light is in its cluster's set) and exactly the sphere/AABB hits; occupancy words mark exactly the non-zero mask words; clustered shading equals `ShadeAllLights`. A dense 3,000-light swarm keeps every light in every cluster (no truncation, whatever the heatmap scale), and more lights than `LightCapacity` is rejected. Heatmap colors. The item 69 scaling scenarios (below) |
| `Render.ClusteredLightAssigner` (2) | On the mock device: four passes in order with the right bindings, the mask pass's row pitch and the dispatch sizes, the light-capacity check; heatmap validation |
| `ShaderCompiler.ClusterLayout` (1) | Every program's bindings and group size match `ClusterBindings.h`. The grid, record and stats layouts equal the C++ structs |
| Native `ClusteredLightingMatchesTheCpuReference` | Three frames over 2 directional + 1,500 local lights. See below |

The native smoke compares the view lights, every cluster AABB, list and record, the stats, a 1,024-point shading probe (clustered vs brute force vs `ShadeAllLights`) and a full heatmap with the CPU definitions. Its three frames are:

- 1280×720;
- the same view with a heatmap scale of 4 lights: many clusters above it, identical light sets and no magenta;
- 800×450 after 300 lights moved.

The CPU assignment is fed the GPU's own view lights and bounds, so only lights grazing an AABB within float rounding may differ.

## Scaling (item 69)

The mask pass tests every local light against every cluster (one thread per 32 lights and cluster), so its cost grows with lights × clusters; a culled light costs a load and a skip. Shading cost grows with the lights in the pixel's cluster, which the bitmask never caps. The benchmarks characterize the Phase 15 scenarios:

- **CPU (`Render.ClusteredLights.ScalingScenariosKeepStatisticsConsistentAndBounded`):** 0 lights, 1k and 10k spread through the view, 10k behind the camera, and 1k packed into a 4 m ball, on a 640×360 grid. Every `ClusterStats` field is checked against its definition. The empty and off-screen scenes write nothing, 10k exceeds the heatmap scale in places with every light kept, and the dense ball fills its clusters.
- **Native (`RHI.Vulkan.Smoke.ClusteredLightingScalesToTensOfThousandsOfLights`):** 0, 1k, 10k and 32k lights through a 1280×720 × 24-slice grid, plus 10k off-screen and 10k dense. Each scenario runs three frames. The last frame's GPU timestamps are printed per pass, and its statistics are checked against the CPU cull and `ClusterStats`' identities.
- **Forward+ (`ClusteredForwardPlusMatchesTheCpuReference`, last frame):** 10,000 lights rendered and compared, with the cluster, opaque, sort and transparent pass times printed.

Budgets (`LightCapacity`, tile size, slice count) should be set from the desktop numbers these print. The validation records keep them.
