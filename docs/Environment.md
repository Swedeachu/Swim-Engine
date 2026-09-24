# Environment and image-based lighting

This covers critical-path items **61** (environment/IBL) and **62** (the PBR image-regression gallery). It builds on [Materials](Materials.md) (items 58–60). Tone mapping, exposure and the post stack are item 73; HDR environment *assets* (importing `.hdr`/`.exr` panoramas) are not implemented yet.

```text
Renderer/Environment
  EnvironmentMath        cube-face table, texel solid angles, Hammersley, GGX sampling,
                         split-sum BRDF integral, prefilter lod, SH9, environment rotation
  ProceduralSky          analytic HDR sky (built-in source) + its push constants
  CubeImage, Image2D     CPU cube/2D images with Vulkan-exact sampling (seamless trilinear)
  EnvironmentReference   CPU definition of every GPU pass + EnvironmentProbe (the lookup)
  EnvironmentBuilder     graph-scheduled compute passes (below)
Shaders/Slang/Environment
  EnvironmentCommon      mirrors EnvironmentMath line for line
  EnvironmentSky / Downsample / Prefilter / Irradiance / BrdfLut   one program per pass
  EnvironmentLighting    the shader-side lookup feeding StandardPbrShadeResolved
Renderer/Materials/StandardPbr   Resolve / EvaluateEnvironment / ShadeResolved (split-sum IBL)
```

`Renderer/Environment` sits above RenderGraph and Materials. It never includes the GPU Scene, visibility, the GPU material table, residency or assets, and none of those layers include it. `scripts/verify-build-layout.py` enforces this.

## Conventions

- **Cube faces** follow the Vulkan major-axis table. Layers 0..5 are +X, −X, +Y, −Y, +Z, −Z. Face coordinates `(s, t)` lie in [−1, 1], and `t` grows down the image rows (`CubeFaceDirection`, `DirectionToCube`). When magnitudes tie, the face is chosen X first, then Y.
- **Directions** are world space with +Y up. The view vector points toward the camera.
- **Roughness** is glTF perceptual roughness, clamped to `StandardPbr::MinPerceptualRoughness`; alpha = roughness².
- **Format:** every map is `RGBA16Float`.
  - Source cubes have mips down to 4×4 (`EnvironmentSourceMipCount`), so filtered importance sampling never reads the 2×2 and 1×1 levels, which are dominated by corner texels.
  - Prefiltered mip *m* of *M* holds roughness *m* / (*M* − 1). Mip 0 is the mirror reflection, a single lod-0 sample.

## GPU passes (`EnvironmentBuilder`)

| Pass | Program | Output | Dispatch |
| --- | --- | --- | --- |
| `RecordSky` | `EnvironmentSky.slang` | source cube mip 0: the sky at each texel center | one pass, 6 faces |
| `RecordMips` | `EnvironmentDownsample.slang` | mips 1.. as 2×2 box averages | one pass per mip, 6 faces |
| `RecordFromSource` prefilter | `EnvironmentPrefilter.slang` | GGX-prefiltered cube, N = V = R, filtered importance sampling (Colbert & Křivánek) | one pass per mip, 6 faces |
| `RecordFromSource` irradiance | `EnvironmentIrradiance.slang` | 9 × `float4` SH coefficients of irradiance / π | one 64-thread group |
| `RecordBrdfLut` | `EnvironmentBrdfLut.slang` | split-sum (A, B) LUT, height-correlated Smith | one pass |

`Record(graph, sky, map)` runs sky, mips, prefilter and irradiance in order.

- **External sources.** `RecordFromSource` accepts any complete source cube. For an uploaded environment, write mip 0 and call `RecordMips` first.
- **Persistent outputs.** `EnvironmentTargets` lets callers pass imported textures or buffers in place of the transient outputs.
- **The LUT** is environment-independent. Build it once.

Each program has a binding contract in `EnvironmentBindings.h`. `ShaderCompiler.EnvironmentLayout` proves the compiled reflection matches it: bindings, sampled dimensions (cube, 2D array), `rgba16f` storage formats, thread groups and push-constant sizes.

**RHI change: cube storage.** Storage textures may now be cube-compatible: square, with 6*n* layers, queried with `VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT`. Shaders still write one mip of one face through a 2D view.

## Shading

`StandardPbr` is split so image-based lighting can use the shading normal after normal mapping:

- `Resolve(parameters, texels, frame)` → `ResolvedSurface`, or empty when alpha masking discards the pixel.
- `EvaluateEnvironment(surface, view, terms)` computes the split-sum image-based lighting with the roughness-dependent Fresnel of Fdez-Agüera:
  - E = k<sub>S</sub>·A + B, where k<sub>S</sub> = F0 + (max(1 − r, F0) − F0)(1 − N·V)⁵;
  - specular = prefiltered × E;
  - diffuse = irradiance × diffuseColor × (1 − E);
  - both terms are scaled by occlusion.
- `ShadeResolved(surface, lighting, terms*)` adds direct light, constant ambient, image-based lighting and emission.

`Shade` is `Resolve` + `ShadeResolved(…, nullptr)`, bit for bit. `StandardPbr.slang` mirrors all of this (`StandardPbrResolve`, `StandardPbrEvaluateEnvironment`, `StandardPbrShadeResolved`).

`EnvironmentProbe::Lookup` (CPU) and `EnvironmentLookup` (`EnvironmentLighting.slang`) produce the terms:

- SH irradiance at the normal;
- trilinear prefiltered radiance at the reflection vector, with lod = roughness × (mips − 1);
- the bilinear LUT at (N·V, roughness).

Radiance scales with `EnvironmentLighting::Intensity`. `Rotation` turns lookups around +Y.

**Furnace property.** A white dielectric in a uniform environment reflects exactly that radiance: diffuse (1 − E) + specular E = 1. Metals reflect A + B ≤ 1. This is single scattering; multi-scatter compensation is not applied.

## CPU references

Each GPU stage has a CPU definition that the native smokes feed with the GPU's own inputs:

- `BuildSkyCube`;
- `CubeImage::GenerateMips`;
- `PrefilterDirection` / `BuildPrefilteredCube`;
- `ProjectIrradianceSh`;
- `BuildBrdfLut`.

`CubeImage::SampleTrilinear` emulates Vulkan's seamless cube filtering. A footprint that leaves a face fetches the adjacent face's edge texel, a corner texel is the average of the three texels meeting there, and mips are blended linearly. `SampleNearest` follows Vulkan's nearest-mip rule, ceil(lod + ½) − 1.

## PBR image-regression gallery (item 62)

`Tests/Fixtures/PbrGalleryFixture.h` defines the gallery and is its golden CPU renderer.

- **Layout:** 6 columns (roughness 0..1) × 4 rows:
  - gold;
  - red plastic;
  - white dielectric;
  - half-metallic copper with occlusion 0.6.
- **Spheres** are analytic impostors in an orthographic view. The fragment derives the exact normal from its pixel center, so the CPU and GPU shade identical inputs, and every pixel more than a pixel inside a silhouette is compared.

`RhiSmoke/PbrGallery.slang` draws the same image on the GPU with `StandardPbrShadeResolved` + `EnvironmentLookup`.

The references are computed rather than stored as binary golden images:

- the CPU gallery test checks the expectations below on every platform;
- the native smoke compares the GPU image with the CPU renderer fed the GPU's own prefiltered cube, SH and LUT.

**Expectations.** Both the CPU test and the GPU image must show the following:

- in the furnace, white dielectrics equal 1 and nothing exceeds 1;
- metal highlights dim monotonically with roughness;
- the directional light adds highlights;
- occlusion scales image-based lighting exactly;
- the background stays clear.

Set `SWIM_PBR_GALLERY_DUMP=<directory>` when running the native smoke to write each frame's GPU, CPU and ×10 difference images. Each is written as a linear `.pfm` and a Reinhard-tone-mapped sRGB `.bmp`.

## Tests

| Suite | What it proves |
| --- | --- |
| `Render.Environment.Math` (9) | Face table round trips; solid angles sum to 4π; Hammersley stratification; GGX samples follow the normalized D; tangent frames; the split-sum matches brute-force directional albedo; lod mapping; rotation; SH orthonormality and exact convolution to band 2 |
| `Render.Environment.CubeImage` (2) | Mip layout, box filter, nearest/trilinear rules, seamless edges and corners |
| `Render.Environment.Sky/Prefilter/BrdfLut/Shading/Probe` (6) | Sky continuity and constants; prefilter preserves uniform radiance and converges to the brute-force GGX lobe; LUT texel mapping; furnace energy; `Shade` equivalence; lookup rotation, intensity and roughness lod; `StandardPbr::EnvironmentSpecularWeight` × prefiltered radiance is exactly the specular part of the split-sum IBL (item 76, the reflectance screen-space reflections are weighted by) |
| `Render.EnvironmentBuilder` (3) | Pass and dispatch structure, push constants per face and mip, view shapes (cube, 2D array, per-face 2D), targets, culling, contract violations |
| `Render.PbrGallery` (1) | The CPU gallery meets the expectations; image dumps |
| `ShaderCompiler.EnvironmentLayout` (2) | Compiled programs match their C++ contracts (it caught a `uint3` std430 padding bug before any GPU run) |
| `RHI.Vulkan.StorageTextureCreation` | Cube-compatible storage validation |
| Native `EnvironmentMapsMatchTheirCpuReferences` | Every GPU stage against its CPU definition, for 3 environments (including a furnace) |
| Native `PbrGalleryMatchesTheCpuReference` | The gallery per pixel, in 3 frames (lit, rotated with intensity 0.6, furnace) |
