# Shadows

This covers critical-path items **70** (directional shadows), **71** (spot shadow atlas) and **72** (point shadow policy), the first Phase 16 checkpoint.

All shadowed lights share one `D32Float` atlas. Each frame the CPU plans which lights get shadows and where their tiles go. The GPU then culls casters per shadow view and renders every tile GPU-driven. [Clustered Forward+](ForwardPlus.md) samples the atlas with PCF through the light's `ShadowIndex`. Lighting never learns how a shadow was produced: it reads a record and its views.

```text
Renderer/Shadows
  ShadowRecords          GpuShadowRecord (64 B), GpuShadowView (96 B), ShadowKind
  ShadowMath             CPU definition: cascade splits/spheres/snapping, spot and cube-face
                         projections, projection to atlas pixels, view selection, ShadowFactor
  ShadowAtlasAllocator   power-of-two tiles in one square atlas: stable, budgeted, evicting
  ShadowPlanner          ShadowSettings + casters -> records, views, tiles, visibility views
  ShadowBindings         descriptor contract of ShadowDepth.slang, shared bindless space
  ShadowRenderer         pipeline state, material routing, Record: caster culls + depth pass
  ShadowGraphResources   what one Record scheduled
Shaders/Slang/Shadows
  ShadowRecords.slang    mirrors the records, view selection and ShadowFactor
  ShadowDepth.slang      vertex-pulled depth; compiled as SwimShadowDepth and
                         SwimShadowMasked (SHADOW_ALPHA_TEST=1)
```

`Renderer/Shadows` sits above Visibility, GpuScene, Geometry, GpuMaterials, Materials, Lights and RenderGraph, and below Forward+. Nothing below it (or Residency) includes it. It never reaches Forward+, clustering, environment, residency, assets, IO or jobs. `scripts/verify-build-layout.py` enforces this.

## A frame

```text
lights with LightFlags::CastsShadows + ShadowIndex
      |
PlanShadows(settings, camera, casters, atlas allocator)   (CPU, once per frame)
  budget per kind -> tile requests -> ShadowAtlasAllocator -> records, views, visibility views
      |
ShadowRenderer::Record
  1. per view: GpuVisibility::Record with GpuViewFlags::ShadowCasters | ResetLodHistory
  2. one depth pass: clear the atlas (reverse-Z far = 0); per view: viewport/scissor = tile,
     Opaque bin with SwimShadowDepth, Masked bin with SwimShadowMasked
      |
ForwardPlusFrame::Shadows = &shadowResources
  ClusteredForward.slang: every light with a ShadowIndex and CastsShadows x ShadowFactor
```

## Records

`GpuLightRecord::ShadowIndex` names a slot in the records buffer (`ShadowSettings::MaxSlots`, default 64). The light must also have `LightFlags::CastsShadows`. The planner fills every slot each frame; unplanned, over-budget and evicted slots are `ShadowKind::None`, which shading treats as lit.

| `GpuShadowRecord` field | Meaning |
| --- | --- |
| `Kind` | None, Directional, Spot or Point |
| `FirstView`, `ViewCount` | its views: cascades, 1, or 6 cube faces |
| `PcfRadius` | 0: one texel; R ≥ 1: a (2R + 1)-texel box, bilinearly weighted ((2R + 2)² comparisons) |
| `CascadeBlend` | directional: the far fraction of each cascade's depth range that cross-fades into the next (the last one fades to lit) |
| `NormalBias`, `SlopeBias`, `DepthBias` | the receiver-side bias (below) |
| `CascadeFar[4]` | directional: camera view depth where cascade i ends |
| `LightPosition` | point: cube-face selection, and perspective texel size |

A `GpuShadowView` holds the row-major reverse-Z `ViewProjection`, the tile (`AtlasRect` in pixels), `TexelWorldSize` (world size of one texel; per unit of distance for perspective views) and `Perspective`.

## Directional shadows (item 70)

- **Splits:** `CascadeSplits` blends uniform and logarithmic splits (`SplitLambda`) from the camera near plane to `MaxDistance`. There are 1–4 cascades (`CascadeSettings::Count`, default 3).
- **Bounds:** each slice is enclosed by the smallest sphere centered on the view axis (`CascadeSphere`). The radius depends only on the slice and the projection, never on the camera's orientation, so rotating the camera never resizes a cascade.
- **Snapping:** the light view looks along the light direction from the origin. The sphere center is rounded to whole texels in light x/y. A moving camera therefore moves each cascade in whole-texel steps, and static geometry stays on the same texels (no shimmer).
- **Casters behind the camera:** the orthographic depth range extends `CasterExtension` (default 50 m) toward the light, so off-screen casters still shadow the slice.
- **Resolution:** `CascadeResolution` per cascade tile (default 2048), all cascades one size; `MaxDistance` 70 m, `SplitLambda` 0.8.
- **Selection:** a point uses the first cascade whose `CascadeFar` exceeds its camera view depth. Beyond the last cascade it is unshadowed.
- **Blending:** over the far `ShadowSettings::CascadeBlend` fraction (default 0.2) of its depth range, a cascade cross-fades into the next one; the last cascade fades to lit over the same band. Quality therefore changes smoothly with distance instead of popping at the splits.

## Spot shadows and the atlas (item 71)

A spot light gets one square perspective view (infinite reverse-Z far plane, `Near` default 0.05) along its axis. The field of view is the outer cone angle, capped at 2.6 rad (`MaxSpotShadowFov`); outside it, the spot is unshadowed.

`ShadowAtlasAllocator` places power-of-two tiles from `MinTile` up to the atlas size:

1. **Stable:** every request whose requested size and count are unchanged keeps last frame's tiles, downgraded or not. New lights never move existing tiles.
2. **Priority:** the others are placed in priority order (then key order, so ties are deterministic). Placement is first-fit in Morton order at the requested size, halving down to `MinTile` when the atlas is full (**downgrade**).
3. **Displacement:** a request that does not fit even at `MinTile` displaces kept tiles of lower-priority requests, lowest first. Those are placed again in their turn.
4. **Eviction:** when nothing lower is left to displace, the request is evicted: the light is unshadowed this frame.
5. **Recovery:** kept downgraded tiles move to a larger size once one is free again.

All tiles of one light (cascades, cube faces) share one size and are placed or evicted together. `ShadowAtlasStats` reports requests, placed, reused, downgraded, evicted and used texels. `Reset()` forgets the placement, for example after a resize.

## Point shadow policy (item 72)

- A point light renders six 90° cube faces (+X, −X, +Y, −Y, +Z, −Z) into six atlas tiles of `PointResolution` (default 256), not a cube map, so the same atlas, depth pass and sampling code serve all three kinds.
- A point is looked up in the face of its major axis (ties go to X, then Y).
- **Cost controls:** point shadows cost six views each. Only lights with `CastsShadows` and a slot are considered. `MaxPointShadows` (default 2) keeps the highest-priority ones. `MaxSpotShadows` (16) and `MaxDirectionalShadows` (1) budget the other kinds. `ShadowCasterDesc::Resolution` overrides a light's size, and the atlas downgrades or evicts as above.
- `ShadowPlanStats` reports casters, over-budget, placed, reused, downgraded, evicted and views.

## Rendering casters

- **Culling:** each view records one `GpuVisibility` pass over the GPU Scene. `GpuViewFlags::ShadowCasters` adds `RenderObjectFlags::CastShadows` to the drawable mask, so only visible casters inside the view's frustum are drawn. `ResetLodHistory` keeps shadow views from disturbing LOD history. The instance uses three material bins (`ShadowBin`):
  - **Opaque:** depth only.
  - **Masked:** `StandardPbr::FlagAlphaMask`, drawn with the alpha-tested variant.
  - **Excluded:** `FlagAlphaBlend`. Blended surfaces cast no shadow.
  
  `ShadowRenderer::RouteMaterial` routes a material set.
- **Depth pass:** one pass renders the whole atlas. Per view it sets the viewport and scissor to the tile, then draws each page slot's Opaque and Masked bins GPU-driven (`DrawIndexedIndirectCount`, or the zero-filled fallback).
- **Programs:** `SwimShadowDepth` has an empty fragment stage. `SwimShadowMasked` samples base-color alpha through the bindless table and discards below the cutoff. Its bindless space is defined exactly like Forward+'s (`ShadowBindlessSpace`), so one `BindlessResourceTable` serves both.
- **Pipeline:** `ShadowRenderer::PipelineDesc` gives D32Float, GreaterEqual with writes, and both faces rasterized. Thin and single-sided casters shadow from either side.
- **Bindings (`ShadowDepthBindings`):** Instances 0, Transforms 1, DrawRecords 2, Vertices 3, Views 4, Materials 5. The push constant holds `{ViewIndex, MaterialCount}`.

## Sampling (ShadowRecords.slang = Shadows::ShadowFactor)

The RHI has no rasterizer depth-bias state, so bias is applied when sampling:

1. **Texel size:** the view's `TexelWorldSize`, times the distance to the light for perspective views.
2. **Offset:** the receiver moves along its normal by `texel × (NormalBias + SlopeBias × (1 − N·L)) × (PcfRadius + 1)`. Wider kernels compare texels farther away, so they get more offset.
3. **View:** spot and point lights select the view again for the offset point, which can cross into another cube face. Directional lights sample their cascade, and inside the blend band also the next cascade (each with its own texel size), and interpolate.
4. **PCF (`SampleShadowView`):** the offset point is projected into its tile and compared with exact `Load`s, clamped to the tile: a texel is lit where `depth + DepthBias ≥ stored` (reverse-Z). Radius 0 compares the one texel under the point (hard edges). Radius R ≥ 1 slides a (2R + 1)-texel box continuously over the texel grid: the (2R + 2)² texels it touches count fully, the outer ring by the fraction the box covers (bilinear PCF), so edges are smooth gradients instead of texel stair steps. When every tap agrees the factor is exactly 0 or 1.

The GPU version keeps one sampling site (a two-iteration loop over the cascades) so the Forward+ programs stay within the GPU-assisted validation budget.

Defaults: `NormalBias` 1, `SlopeBias` 1.5, `DepthBias` 0, `PcfRadius` 1. Unknown or None records, points outside every view and a missing atlas are lit.

Exact `Load`s (no hardware comparison filtering) keep the CPU definition bit-for-bit reproducible, so the native smoke can shade with the GPU's own atlas.

## Forward+ integration

- **Bindings:** `ForwardPlusFrame::Shadows` binds the atlas (15, `Texture2D<float>` through a depth-aspect view), the records (16) and the views (17), and sets `ForwardViewFlagShadows`.
- **Shading:** every directional and clustered light whose `ShadowIndex` is valid and that has `CastsShadows` is scaled by its shadow factor, using the surface normal and the camera view depth along `CameraForward`.
- **No shadows:** without a shadow frame, 1×1 stand-ins are bound (`ForwardPlusGraphResources::ShadowFallback`) and the flag stays clear.

## Readback

`Rhi::GetTransferTexelBytes` adds `D32Float` to the buffer/image transfer formats, copying its depth aspect as packed floats. `AddTextureReadback` can therefore read the atlas back (the Vulkan backend selects the depth aspect). Packed depth/stencil formats remain rejected.

## Not yet

- Receiver-plane depth bias, EVSM/VSM/PCSS and contact shadows.
- Cascade blending across split boundaries.
- Caching static shadow views between frames; every planned view is re-rendered each frame.
- Per-view HZB occlusion for casters (culling is frustum + flags + LOD).
- Engine wiring (item 56): the sandbox does not plan shadows yet.

## Tests

| Suite | What it proves |
| --- | --- |
| `Render.Shadows.Math` (5) | Splits blend uniform and logarithmic. Cascade spheres contain their slices and ignore camera rotation. Cascades move in whole texels under sub-texel camera motion and cover off-screen casters. Spot and cube-face views cover their lights, and faces are selected by major axis. Projection maps tiles top-down and inverts exactly |
| `Render.Shadows.Atlas` (10) | Mixed sizes pack without overlap. Unchanged requests keep their tiles across frames. Full atlases downgrade, then evict the lowest priority. Downgrades halve until they fit. High-priority newcomers displace the lowest kept tiles. Downgraded tiles are kept, then grow back. Groups are placed or evicted together. Ties are deterministic. Twenty random frames stay valid and mostly stable. Invalid input is rejected |
| `Render.Shadows.Planner` (4) | Records, views, tiles, texel sizes and visibility flags for a sun, a spot and a point light, and reuse on the next frame. Per-kind budgets by priority. Small atlases downgrade and evict to None. Validation |
| `Render.Shadows.Sampling` (6) | CPU shadow maps are ray cast by `Tests/Fixtures/ShadowFixture.h`. The sampled factor must equal exact ray-cast visibility (0 or 1) wherever the answer is uniform across the PCF footprint, for cascades, spots and every cube face. Lit caster faces show no acne under all three lights. Non-casters, None records, out-of-range slots and depths beyond the last cascade are lit. PCF radius 0 gives hard edges and radius 2 soft ones |
| `Render.ShadowRenderer` (4) | Pipeline state and material bins. On the mock device: one cull per view, then per view and variant the tile's viewport and scissor, one count draw per page slot over that view's commands, push constants, and bindless only for the masked variant, plus every binding. Empty plans still clear the atlas, the zero-filled fallback path, and rejected frames |
| `ShaderCompiler.ShadowLayout` (2) | Both depth variants reflect `ShadowDepthBindings` with the 16-byte push block, and the masked bindless space matches Forward+'s. Forward+'s shadow bindings and the record/view layouts equal the C++ structs |
| `Render.Visibility`, `Render.ForwardPlus.Reference`, `RenderGraph.Transfers`, `RHI.Vulkan.Transfer` (1 each) | The `ShadowCasters` drawable rule; `Shade` scales exactly the shadowed lights; D32 readback |
| `ShaderCompiler.GpuAvBudget` (1) | Forward+, sort and shadow programs stay at or below GPU-AV's 75 instrumented buffer accesses (see [Forward+](ForwardPlus.md#gpu-assisted-validation-budget)) |
| Native `ShadowedForwardPlusMatchesTheCpuReference` | See below |

### Native smoke

`ShadowedForwardPlusMatchesTheCpuReference` renders 480×270. The scene has:

- a ground plane;
- an opaque cube;
- an alpha-masked cube that survives its cutoff (drawn by the masked variant);
- an alpha-masked cube that is cut away entirely;
- a blended glass quad;
- a floating cube without `CastShadows`.

It is lit by a cascaded sun, a spot and a point light (slots 0–2) and 40 unshadowed clustered lights.

1. **Atlas:** the D32 atlas is read back. Every texel whose 3×3 neighborhood sees the same surface is compared with the CPU shadow map, within 10⁻⁵ + 10⁻⁴ relative, and at most 0.1 % may differ. The opaque and masked-kept cubes must appear; the masked-away cube, the glass and the non-caster must not.
2. **Image:** every interior pixel is compared with an exact CPU ray cast shaded by `ForwardPlus::Shade`, which samples the GPU's own atlas through `ShadowFactor`. At most 1 % outliers are allowed, with a mean relative error below 5·10⁻³. More than 1,000 pixels must be visibly darker than unshadowed shading.
3. **Budget:** a 1024 atlas with a second point light. The three cascades and the spot fill it, so the first point light is evicted and the second is over the one-point budget. Both are unshadowed on the GPU and CPU alike, and the image must still match.

Pass timings (caster culls, depth, opaque) are printed.
