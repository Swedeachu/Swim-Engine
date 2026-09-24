# Clustered Forward+

This covers critical-path items **66** (opaque Clustered Forward+) and **67** (transparent Clustered Forward+). Item 69's light-count benchmarks are described in [Clustered lighting](ClusteredLighting.md#scaling-item-69).

Clustered Forward+ is the modern renderer's standard lighting path. It draws GPU Scene objects GPU-driven from [GPU visibility](GpuVisibility.md)'s indirect commands and shades them with:

- the [standard material](Materials.md), through the GPU material table and bindless textures;
- every directional light plus the pixel's [cluster](ClusteredLighting.md) list of point and spot lights from the [GPU light buffer](Lights.md);
- [image-based lighting](Environment.md);
- [shadows](Shadows.md), through each light's shadow record.

No CPU visible list, per-object light list or draw list exists anywhere in the frame.

```text
Renderer/ForwardPlus
  StandardVertex             the 48-byte vertex (the cooked static-mesh layout) and its layout id
  ForwardPlusRecords         ForwardViewRecord, bins, debug modes, sort entries
  ForwardPlusBindings        descriptor contract of the draw and sort programs, bindless space
  ForwardPlusReference       CPU definition: material bins, transforms, shading frame,
                             face culling, sort keys/order, Shade, debug color, blending
  ForwardPlusRenderer        pipeline states + Record: opaque, sort, transparent passes
  ForwardPlusGraphResources  what one Record scheduled
Shaders/Slang/ForwardPlus
  ForwardPlusRecords.slang       mirrors the records and reference helpers
  ClusteredForward.slang         vertex + fragment; compiled as SwimForwardOpaque and
                                 SwimForwardTransparent (FORWARD_TRANSPARENT=1)
  ForwardTransparentSort.slang   the back-to-front bitonic sort
```

`Renderer/ForwardPlus` is the consumer layer. It sits above Visibility, GpuScene, Geometry, GpuMaterials, Environment, Lights and ClusteredLighting. None of those layers, nor Residency, include it, and it never reaches residency, assets, IO or jobs. `scripts/verify-build-layout.py` enforces this.

## A frame

```text
GpuScene / GeometryHeap / GpuMaterialTable / GpuLightBuffer imports
      |
GpuVisibility::Record ---- commands, counts, draw records (bins: Opaque, Transparent)
ClusteredLightAssigner ---- grid, records, indices
EnvironmentBuilder -------- prefiltered cube, SH irradiance, BRDF LUT (optional)
      |
ForwardPlusRenderer::Record
  1. opaque       every page slot's Opaque bin -> color (RGBA16F), object id (R32F), velocity (RG16F),
                  normal + roughness (RGBA16F), indirect (RGBA16F), depth (D32)
  2. sort         one group per page slot: Transparent bin back to front -> sorted commands
  3. transparent  sorted draws, premultiplied blending over the opaque color (and transmittance into
                  the indirect target), depth-tested
```

### Material bins

`GpuVisibility` bins by material set. Forward+ uses two bins (`ForwardPlusBinCount`):

- **Opaque:** opaque, alpha-masked and double-sided materials.
- **Transparent:** materials with `StandardPbr::FlagAlphaBlend` (glTF `BLEND`).

`ForwardPlusRenderer::RouteMaterial` routes a material set with `ForwardPlus::MaterialBin`. `VisibilityBinCapacities(opaque, transparent)` sizes the bins. Unrouted sets land in the opaque bin.

### Vertices and page slots

Vertices are pulled from the GeometryHeap vertex page as `StandardVertex`: position, normal, tangent (w = bitangent sign) and one UV set, 48 bytes. That is exactly the cooked static mesh the StaticModelCompiler writes, and `StandardVertexLayoutId()` equals the `VertexLayout` the residency layer computes for it (tested).

Each index-page slot of the visibility frame names its index and vertex page (`ForwardPlusPageSlot`). Every mesh binned into a slot must keep its 32-bit indices and its vertices in those pages. The renderer binds one space-0 table per slot.

### Shading (ClusteredForward.slang = ForwardPlus::Shade)

- **Vertex stage:** transforms the position by the GPU Scene row. The normal is transformed by the cofactor matrix (exact under non-uniform scale), and the tangent by the linear part. For mirroring transforms (negative determinant) it flips the tangent sign and the triangle facing, so their normals, tangents and faces stay correct.
- **Faces:** both pipelines rasterize both faces. Single-sided materials discard back faces in the shader (`CullsFace`); double-sided ones shade them with the flipped normal. This keeps mirrored instances correct without a second pipeline.
- **Surface:** `StandardPbrResolve` over bindless texels (alpha mask, normal map through the vertex tangent frame).
- **Radiance:** directional lights + the pixel's cluster list, each times its shadow factor (`ForwardDirect`) + `Ambient × baseColor × occlusion` + split-sum IBL (when an environment is bound) + emission.
- **Opaque output:** color with alpha 1, `ObjectId + 1` as a float (exact below 2²⁴; 0 = nothing drawn), a motion vector, the shading normal + roughness, the indirect radiance, the specular reflectance, the specular IBL radiance, and depth with the canonical reverse-Z compare.
- **Debug:** `ForwardPlusDebugMode::ClusterHeatmap` replaces opaque colors with the pixel's cluster heatmap color (black where the cluster is empty), with truncated clusters in magenta.

The object-id target is `R32Float` because the RHI clears only float and normalized attachments.

### Motion vectors and jitter (item 75)

- **View record:** `ForwardViewRecord` (208 bytes) keeps `ViewProjection` unjittered and adds `PreviousViewProjection` and `Jitter`. `ForwardPlusView::PreviousViewProjection` defaults to the current matrix, so a first frame or a camera cut reports no camera motion. Both must be finite.
- **Jitter:** an NDC offset (`Temporal::JitterNdc`, x right, y up). The vertex stage adds `Jitter × w` to the clip position; nothing else sees it. Shading, cluster lookup and motion vectors are unaffected.
- **Velocity:** the vertex stage also outputs the unjittered clip position and the previous one: the previous transform (`GpuTransformRecord::Previous`, which `GpuScene` updates on the first move of a frame) through `PreviousViewProjection`. The opaque fragment stage writes `(now − before) × (0.5, −0.5)` to `SV_Target2`: UV of this frame minus UV of the previous one, y down, so `previousUv = uv − velocity`. `ForwardPlus::MotionVector` is the CPU definition.
- **Target:** `ForwardPlusTargets::Velocity` (`VelocityFormat` = RG16Float, `ColorAttachment`, grid-sized), cleared to 0 like the other targets. Without one the renderer creates a transient target; `ForwardPlusGraphResources::Velocity` names whichever was used. Pixels with no opaque surface keep the clear value (0). Transparent surfaces write no velocity.

### Normal and indirect targets (item 76)

`ForwardPlusTargets::Normal` holds the world shading normal (after normal mapping and back-face flips) with perceptual roughness in w. `ForwardPlusTargets::Indirect` holds ambient × base color × occlusion + IBL (`ForwardPlus::IndirectRadiance`), the part of Color that ambient occlusion may remove. Both are RGBA16Float; transient targets stand in when none are supplied, and pixels without an opaque surface keep 0.

The transparent pipeline has a second attachment, the indirect target. It receives (0, 0, 0, alpha) under the same premultiplied blend, so the indirect light is scaled by each layer's transmittance, as it is inside Color. The heatmap view writes 0 indirect light. See [Screen-space effects](ScreenSpace.md).

### Reflectance and specular targets (item 76, screen-space reflections)

A screen-space reflection must *replace* the specular IBL already added into Color, and a colored metal must tint what it reflects. Both need two more per-pixel terms (`ForwardPlus::SpecularEnvironment`):

- `ForwardPlusTargets::Reflectance`: the split-sum specular reflectance `(kS × A + B) × occlusion` (`StandardPbr::EnvironmentSpecularWeight`), the weight a reflected radiance is multiplied by;
- `ForwardPlusTargets::Specular`: the specular IBL radiance, `Prefiltered × Reflectance`, the part of Indirect a reflection replaces (0 without an environment).

Both are RGBA16Float with transient stand-ins, and the transparent pipeline scales them by each layer's transmittance exactly like Indirect (it now has four attachments). The heatmap view writes 0. With seven opaque color attachments the pass is above Vulkan's guaranteed minimum of 4; every desktop driver exposes 8, and the Vulkan backend rejects a pipeline above the device limit.

The reflectance needs the split-sum LUT's (A, B). `ForwardPlusFrame::BrdfLut` may therefore be supplied **without** an environment: the renderer binds it (the cube stays a stand-in), sets `ForwardViewFlagBrdfLut`, and the specular radiance is 0. With neither, the reflectance is 0 and reflections have no effect. An environment always sets `ForwardViewFlagBrdfLut` too. (Karis' analytic split-sum fit was tried as a LUT-free fallback and rejected: against this engine's height-correlated LUT it is off by up to 0.4.)

### Transparency (item 67)

- **Sort key:** the object's world bounds center along the camera's forward axis. Farther draws first; ties go to the lower instance row, then the lower submesh row.
- **Sort:** `ForwardTransparentSort.slang` fills a scratch buffer from the bin's compacted draws, runs a bitonic network in one 256-thread group per page slot, writes the commands in draw order and zeroes the rest.
  - The draw order therefore never depends on visibility's atomic compaction order.
  - The zeroing also makes the no-`IndirectCount` fallback path work.
- **Draw:** the transparent pipeline blends premultiplied color with `One / OneMinusSourceAlpha`. It tests depth against the opaque result without writing it, and draws each page slot's sorted commands with `DrawIndexedIndirectCount`.
- **Limits:**
  - At most `ForwardTransparentSortBindings::MaxDraws` (65,536) transparent draws per page slot.
  - Order is per object, not per triangle. Intersecting or self-overlapping transparent meshes are not sorted within themselves.
  - Order is exact within a page slot. With several index pages, slots draw in slot order.

### Environment

With `ForwardPlusFrame::Environment` and `BrdfLut`, the view sets `ForwardViewFlagEnvironment` (and `ForwardViewFlagBrdfLut`) and the shader performs `EnvironmentLookup`. Without them the renderer binds 1×1 zero stand-ins and IBL is skipped; a `BrdfLut` alone is still bound for the specular reflectance (see above).

### Shadows (items 70–72)

With `ForwardPlusFrame::Shadows` (from `ShadowRenderer::Record`), the view sets `ForwardViewFlagShadows`. The atlas binds at 15 (through a depth-aspect view), the shadow records at 16 and the views at 17.

A directional or clustered light is shadowed when its `ShadowIndex` names a record and it has `LightFlags::CastsShadows`. Its radiance is then scaled by `ShadowFactor` (PCF with normal-offset bias; see [Shadows](Shadows.md#sampling-shadowrecordsslang--shadowsshadowfactor)). `ForwardPlus::LightShadow` is the CPU definition.

Without shadows, a 1×1 atlas and empty records are bound (`ShadowFallback`) and the flag stays clear.

### GPU-assisted validation budget

GPU-AV instruments every storage/uniform-buffer load, store and atomic and warns (`GPUAV-Compile-time-general-buffer`) above 75 per module; the smokes fail on that warning. `ClusteredForward.slang` therefore walks directional and clustered lights in one loop (one inlined shadow lookup), loads shadow-view members individually and copies the transform and vertex once through loops (Slang otherwise re-loads at each use). `ShaderCompiler.GpuAvBudget` counts the accesses in every renderer program's SPIR-V (opaque 72, transparent 67 today, including the previous-transform and previous-projection copies of item 75) and fails above 75.

## Pipelines

`ForwardPlusRenderer::PipelineDesc(bin, program, layout)` returns the pipeline state; callers compile the programs and create the pipelines:

| | Opaque | Transparent |
| --- | --- | --- |
| Color targets | RGBA16Float, R32Float (object id), RG16Float (velocity), RGBA16Float (normal), RGBA16Float (indirect), RGBA16Float (reflectance), RGBA16Float (specular) | RGBA16Float, RGBA16Float (indirect), RGBA16Float (reflectance), RGBA16Float (specular) |
| Blend | off | premultiplied (One, OneMinusSourceAlpha for color and alpha), on all four attachments |
| Depth | D32Float, GreaterEqual, write | D32Float, GreaterEqual, no write |
| Cull | none (shader culls single-sided back faces) | none (same) |

Both programs share the bindless space `ForwardPlusBindlessSpace(textures, samplers)`, so one `BindlessResourceTable` serves them.

## Not yet

- A depth prepass and hardware back-face culling split by winding.
- Consuming the HZB/visibility late phase for opaque draws beyond what `GpuVisibility` already culls.
- Packing the thin G-buffer (octahedral normals, one reflectance/specular target) to save bandwidth.
- Engine wiring (item 56). The color, depth, normal, indirect, reflectance and specular targets feed [screen-space AO, reflections and fog](ScreenSpace.md) (item 76); that output, the depth and the velocity feed [temporal anti-aliasing](TemporalAntiAliasing.md) (item 75), whose output feeds [post-processing](PostProcess.md) (items 73–74).
- Velocity for the background (sky) from camera motion; it keeps the cleared 0.
- Importing glTF `alphaMode` into `FlagAlphaBlend`.

## Tests

| Suite | What it proves |
| --- | --- |
| `Render.ForwardPlus.Reference` (9) | Material bins and face culling. Normals stay perpendicular and outward under non-uniform scale and mirroring (400 random transforms). Tangent-sign and mirrored facing. The transparent order is back to front with stable tie-breaks and independent of compaction order. Clustered shading equals brute force and decomposes into lights + ambient + IBL + emission; truncation only removes light. Heatmap debug colors and blending. View-record validation (an environment implies `ForwardViewFlagBrdfLut`; a LUT alone sets only it). `IndirectRadiance` is exactly ambient + IBL; `SpecularEnvironment` is the LUT-weighted reflectance and `Prefiltered ×` it, part of the indirect light, and without an environment uses a supplied LUT or is 0. Motion vectors for object, camera and perspective motion, never jitter. Shadowed lights are scaled by exactly their shadow factor, and only with the view flag, the light flag and an atlas |
| `Render.ForwardPlus.Fixture` (1) | The test meshes are CCW-outward with exact tangents, and the analytic ray caster agrees with triangle intersection under rotated, scaled and mirrored transforms |
| `Render.ForwardPlus` (1, RenderResidency) | `StandardVertexLayoutId` is the residency layer's layout of a cooked static mesh |
| `Render.ForwardPlusRenderer` (5) | Pipeline states. On the mock device: per-slot opaque count draws over the visibility commands, one sort dispatch with its push constants, per-slot transparent draws over the sorted commands, every binding of the last table, the no-`IndirectCount` fallback, the environment and shadow stand-ins, a BRDF LUT bound without an environment, the transient or supplied velocity, normal, indirect, reflectance and specular targets, seven opaque and four transparent attachments, and every rejected input |
| `ShaderCompiler.ForwardPlusLayout` (2) | Both variants reflect identical bindings matching `ForwardPlusDrawBindings` (including the shadow atlas, records and views); the view record (208 bytes, including the previous matrix and jitter) and sort entry equal the C++ structs; the sort's group size and push constants |
| Native `ClusteredForwardPlusMatchesTheCpuReference` | A lit scene compared pixel by pixel with an exact CPU ray cast. See below |
| Native `ClusteredLightingScalesToTensOfThousandsOfLights` | Item 69 (see [Clustered lighting](ClusteredLighting.md#scaling-item-69)) |
| Native `ShadowedForwardPlusMatchesTheCpuReference` | Forward+ with the shadow atlas (see [Shadows](Shadows.md#native-smoke)) |

### Native smoke

`ClusteredForwardPlusMatchesTheCpuReference` renders 480×270. The scene has:

- a ground plane;
- a gold cube (textured metallic-roughness);
- a normal-mapped cube with non-uniform scale;
- a mirrored cube;
- an alpha-masked cube that must vanish;
- an occluded, emissive cube;
- five blended quads, created out of depth order: three overlapping, one partly behind a cube, and one single-sided and facing away.

It is lit by a sun, 300 clustered lights and the GPU-built sky environment. Every pixel whose 3×3 neighborhood sees the same surfaces is compared with `Tests/Fixtures/ForwardPlusFixture.h`'s exact ray cast of the same shapes. The cast is shaded by `ForwardPlus::Shade` over the GPU's own cluster lists and environment maps.

- **Object ids:** at most 0.2 % of interior pixels may differ, and the masked cube never appears.
- **Color:** at most 0.5 % outliers (0.01 + 3 %), and a mean relative error < 5·10⁻³. Transparent layers are composited with `ForwardPlus::Over` in the GPU's sorted order.
- **Sort:** the GPU order must equal `SortTransparentDraws`, with unused commands zeroed. Reversing the order must visibly change at least 50 pixels, so the order is observable.
- **Normal, indirect, reflectance and specular:** every compared opaque pixel's normal + roughness must match the resolved surface within 0.02, and its indirect radiance `IndirectRadiance`, specular reflectance and specular IBL radiance (`SpecularEnvironment`) times the transmittance of the layers in front (0.01 + 3 %); at most 0.5 % outliers each.
- **Velocity:** every interior opaque pixel's motion vector must equal `ForwardPlus::MotionVector` at the ray-cast point (within 10⁻⁴ + 0.2 %; at most 0.2 % outliers). The first frame must have no motion, and the moved frame must move more than a quarter of the image.

Frames:

1. lit;
2. the cluster heatmap;
3. a moved camera with the red quad moved in front of the green one (the order must flip), without an environment (stand-ins), jittered by (0.37, −0.21) pixels; the CPU ray cast follows the jitter;
4. 10,000 lights, every 7th pixel compared, with pass timings printed.
