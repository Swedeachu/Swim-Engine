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
- **Depth prepass (optional, on in the runtime):** with `ForwardPlusRendererDesc::DepthPrepass` and `OpaquePrepassed` set, the Opaque bin is drawn twice. `SwimForwardDepth` (`FORWARD_DEPTH_ONLY`) writes depth only, discarding culled faces and alpha-masked texels. `SwimForwardOpaquePrepassed` (`FORWARD_PREPASSED`) then shades with `[earlydepthstencil]`, GreaterEqual and no depth writes and never discards, so only the visible surface runs the lighting loop. The vertex position math is `precise` in all variants so both passes rasterize identical depths.
- **Surface:** `StandardPbrResolve` over bindless texels (alpha mask, normal map through the vertex tangent frame).
- **Radiance:** directional lights + the pixel's cluster list, each times its shadow factor (`ForwardDirect`) + `Ambient × baseColor × occlusion` + split-sum IBL (when an environment is bound) + emission.
- **Opaque output:** color with alpha 1, `ObjectId + 1` as a float (exact below 2²⁴; 0 = nothing drawn), a motion vector, the shading normal + roughness, the indirect radiance, the specular reflectance, the specular IBL radiance, and depth with the canonical reverse-Z compare.
- **Debug:** `ForwardPlusDebugMode::ClusterHeatmap` replaces opaque colors with the pixel's cluster heatmap color (black where the cluster is empty). Cluster light sets are bitmasks and never truncated, so no cluster is magenta.

The object-id target is `R32Float` because the RHI clears only float and normalized attachments.

### Motion vectors and jitter (item 75)

- **View record:** `ForwardViewRecord` (208 bytes) keeps `ViewProjection` unjittered and adds `PreviousViewProjection` and `Jitter`. `ForwardPlusView::PreviousViewProjection` defaults to the current matrix, so a first frame or a camera cut reports no camera motion. Both must be finite.
- **Jitter:** an NDC offset (`Temporal::JitterNdc`, x right, y up). The vertex stage adds `Jitter × w` to the clip position; nothing else sees it. Shading, cluster lookup and motion vectors are unaffected.
- **Velocity:** the vertex stage also outputs the unjittered clip position and the previous one: the previous transform (`GpuTransformRecord::Previous`, which `GpuScene` updates on the first move of a frame) through `PreviousViewProjection`. The opaque fragment stage writes `(now − before) × (0.5, −0.5)` to `SV_Target2`: UV of this frame minus UV of the previous one, y down, so `previousUv = uv − velocity`. `ForwardPlus::MotionVector` is the CPU definition.
- **Deforming meshes (item 78):** the previous local position is read from the same vertex page, `GpuInstanceRecord::PreviousVertexOffset` vertices after the current one (GPU-skinned output meshes store it there; [Animation and skinning](Animation.md)). An offset of 0 reads the vertex's own position again, so rigid meshes are unchanged. The previous position is fetched in the same load loop as the vertex (one GPU-AV instrumentation site); the opaque program is at 73 of 75 instrumented accesses. The five-argument `ForwardPlus::MotionVector(view, current, previous, local, previousLocal)` is the CPU definition. The Forward+ smoke switches its gold cube to a mesh whose previous positions are shifted from its vertices for the moved and 10k-light frames, and compares those pixels against it.
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

`ForwardPlusRenderer::PipelineDesc(bin, program, layout)` returns the pipeline state (`DepthPrepassPipelineDesc` and `PrepassedPipelineDesc` for the prepass pair: no color targets with depth writes, and the Opaque state without depth writes); callers compile the programs and create the pipelines:

| | Opaque | Transparent |
| --- | --- | --- |
| Color targets | RGBA16Float, R32Float (object id), RG16Float (velocity), RGBA16Float (normal), RGBA16Float (indirect), RGBA16Float (reflectance), RGBA16Float (specular) | RGBA16Float, RGBA16Float (indirect), RGBA16Float (reflectance), RGBA16Float (specular) |
| Blend | off | premultiplied (One, OneMinusSourceAlpha for color and alpha), on all four attachments |
| Depth | D32Float, GreaterEqual, write | D32Float, GreaterEqual, no write |
| Cull | none (shader culls single-sided back faces) | none (same) |

Both programs share the bindless space `ForwardPlusBindlessSpace(textures, samplers)`, so one `BindlessResourceTable` serves them.

## Not yet

- Hardware back-face culling split by winding.
- Consuming the HZB/visibility late phase for opaque draws beyond what `GpuVisibility` already culls.
- Packing the thin G-buffer (octahedral normals, one reflectance/specular target) to save bandwidth.
- Engine wiring (item 56). The color, depth, normal, indirect, reflectance and specular targets feed [screen-space AO, reflections and fog](ScreenSpace.md) (item 76); that output, the depth and the velocity feed [temporal anti-aliasing](TemporalAntiAliasing.md) (item 75), whose output feeds [post-processing](PostProcess.md) (items 73–74).
- Velocity for the background (sky) from camera motion; it keeps the cleared 0.
- Importing glTF `alphaMode` into `FlagAlphaBlend`.

## Tests

| Suite | What it proves |
| --- | --- |
| `Render.ForwardPlus.Reference` (9) | Material bins and face culling. Normals stay perpendicular and outward under non-uniform scale and mirroring (400 random transforms). Tangent-sign and mirrored facing. The transparent order is back to front with stable tie-breaks and independent of compaction order. Clustered shading equals brute force and decomposes into lights + ambient + IBL + emission, also when clusters exceed the heatmap scale (bitmask sets are never truncated). Heatmap debug colors and blending. View-record validation (an environment implies `ForwardViewFlagBrdfLut`; a LUT alone sets only it). `IndirectRadiance` is exactly ambient + IBL; `SpecularEnvironment` is the LUT-weighted reflectance and `Prefiltered ×` it, part of the indirect light, and without an environment uses a supplied LUT or is 0. Motion vectors for object, camera and perspective motion, never jitter. Shadowed lights are scaled by exactly their shadow factor, and only with the view flag, the light flag and an atlas |
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

## Multisample anti-aliasing

`RenderSettings::MsaaSamples` (1, 2, 4, 8 or 16) multisamples the scene geometry. The count is lowered to the largest one in `GraphicsCapabilities::SampleCounts` at or below it: the counts both colour and depth attachments support and that can be sampled as `Texture2DMS` (Vulkan: the framebuffer and sampled-image limits; D3D12: `MULTISAMPLE_QUALITY_LEVELS` for every target format). `FrameRenderer::GetSupportedMsaaSamples` returns that mask for a settings menu; `RenderStats::MsaaSamples` reports the count a frame used (1 when the multisampled pipelines could not be made, logged once).

- **Passes.** The sky, depth prepass and opaque pass draw into multisampled copies of the seven opaque targets and depth (`ForwardPlusTargets::Samples`), through a second `ForwardPlusRenderer` with multisampled pipelines. `ForwardMsaaResolve.slang` then writes the single-sample targets in one fullscreen pass. Colour, indirect, reflectance and specular are averaged with 1/(1+luma) weights, so a bright sample does not swamp an edge after tone mapping. Depth, normal, velocity and object id come from the nearest sample, never a blend of two surfaces. The main renderer then runs the back depth and the transparent layers at one sample (`ForwardPlusFrame::Opaque = false`).
- **Downstream.** Particles, render features after the opaque pass, SSAO, SSR, TAA and post processing all read the resolved targets, so MSAA works with TAA on or off and with every `AntiAliasing` mode.
- **Alpha to coverage.** When multisampled, the view sets `ForwardViewFlagAlphaToCoverage`. Alpha-masked materials then output a sharpened coverage, `saturate((alpha - cutoff) / fwidth(alpha) + 0.5)`, as colour target 0's alpha, and their pipelines enable `GraphicsPipelineDesc::AlphaToCoverage`. The depth prepass uses the `ForwardDepthCoverage` variant, which declares `SV_Target0` for it. Feature geometry opts in with `RenderGeometryState::AlphaToCoverage`; its pipelines are made per sample count (`RenderFeatureDrawContext::Samples`).
- **RHI.** `DescriptorBindingDesc::Multisampled` marks a `Texture2DMS` binding, reflected by the shader compiler. D3D12 creates `TEXTURE2DMS` views; Vulkan checks the image's sample count against the binding.
- **Not multisampled:** transparent layers, particles, features drawn after the resolve, shadow maps, and the back depth. Deferred local lights are off; the opaque pass runs the full light loop. `BeforeOpaque` gets no prepass depth, so feature occlusion against it is skipped.
- **Memory.** The multisampled targets are 52 bytes per pixel per sample, on top of the single-sample targets: about 215 MB at 2x, 430 MB at 4x, 860 MB at 8x and 1.7 GB at 16x at 1920×1080. At 16x the resolve loads 16 samples of seven targets per pixel.

## Cube map sky

`FrameRenderer::SetSkyCubemap(size, faces)` replaces the procedural sky background with six sRGB RGBA8 faces (+X, -X, +Y, -Y, +Z, -Z, a left-handed cube whose +Z is the default forward, world -Z). `SkyCubemap` (`SkyBackground.slang` with `SKY_CUBEMAP=1`) draws it in the sky pass of the main view (any sample count), reflection probe faces and planar captures, scaled by `RenderSettings::SkyCubemapIntensity` and rotated by `EnvironmentRotation`. Fog and the screen-space passes see it like the procedural sky (depth 0); image-based lighting stays procedural. `ClearSkyCubemap` returns to the procedural sky. The cube is drawn only while `RenderSettings::SkyBackground` is on (off: the clear colour, as without a cube); it replaces the whole procedural background (gradient, ground and sun disc), and `RenderStats::SkyCubemap` says whether a frame drew it. Features that draw over sky pixels (sun, moon, stars, clouds) are the caller's to turn off.

## Deferred local lights (Phase 23 performance work)

With the depth prepass, `ForwardPlusRendererDesc::OpaqueDeferred` (SwimForwardOpaqueDeferred: the prepassed opaque program with `FORWARD_DEFERRED_LOCAL=1`) and `LocalLightsPipeline` (ForwardLocalLights.slang), a frame with `ForwardPlusFrame::DeferLocalLights` and a Storage-capable colour target is drawn in two steps:

1. the opaque pass shades directional lights, ambient, IBL and emission as before and writes an eighth target, base colour + metalness (`MaterialFormat`; alpha −1 in debug views);
2. a compute pass (`<name> local lights`, 8 x 8 threads) reconstructs each opaque pixel's position from depth and adds its cluster's point and spot lights, with shadows, to the colour target, before the transparent pass. The wave walks the union of its pixels' cluster masks (`WaveActiveBitOr`) so loop control and light loads stay uniform.

The per-light work is exactly the fragment loop's, so the image is the same (animated foliage aside, captures differ in 0.4 % of pixels by more than 8/255); it runs without the helper lanes of partly covered quads and in a small, high-occupancy program. In the sandbox's Sponza view (256 lights) the opaque pass went from 3.9 ms to 1.0 ms plus 1.4 ms for the lights. `RenderSettings::DeferredLocalLights` (switch `lighting.deferred-local-lights`) turns it off. Transparent draws keep the fragment loop.

## Register pressure (measured with `SWIM_PIPELINE_STATS=1`)

Driver statistics for the opaque fragment program (RTX 4070 Laptop, subgroup 32), measured by swapping shader variants into the forward+ smoke:

- Opaque with local lights in the loop (the prepassed program, used whenever deferred local lights are off): **80 registers**. The deferred-local program: **64**. Transparent: 72.
- Deferred local lights are skipped for any frame with opaque external draws (`DrawsIn(External, Opaque)`): an application that draws its world geometry through a render feature (terrain, for example) always runs the 80-register program, and that feature's own shader, which includes `ForwardFrame.slang` and calls `ForwardLightSurface`, pays the same light loop.
- What does not lower the 80: compiling out the glint or the cluster heatmap (binary 28.8 KB to 23.4 KB, registers unchanged), sampling emission after the light loop (95), splitting the directional and local loops, scalarising the cascade blend, `[loop]` on the PCF.
- What does: the light loop is the pressure (48 without it), and the directional cascade path of `ShadowFactor` is most of it. With the directional shadow replaced by one texture read (local shadows kept) the program drops to **71** (about 24 to 28 resident warps). The deferred-local program stays at 64 either way.
- A screen-space directional shadow mask resolved from prepass depth would get that 71, but deferred for now: the prepass has no shading normals, so the mask's normal-offset and slope bias would differ from the in-loop lookup (visible at normal-mapped surfaces and silhouettes), and feature shaders would need their own mask variant. Revisit it with the path-traced shadow target, as one shared mask both can write.
