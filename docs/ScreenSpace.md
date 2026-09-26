# Screen-space ambient occlusion, reflections and fog

This covers critical-path item **76**, the optional screen-space modules: ground-truth-based ambient occlusion (GTAO), screen-space reflections (SSR) and exponential height fog.

`Renderer/ScreenSpace` runs between Forward+ and TAA. It reads the scene depth and four Forward+ targets: the shading normals, the indirect radiance, and (for reflections) the specular reflectance and the specular IBL radiance. It writes a new HDR color.

```text
Renderer/ScreenSpace
  ScreenSpaceSettings         AO, reflection and fog settings + validation
  ScreenSpaceRecords          GpuScreenSpaceParams (400 B, one upload per frame)
  ScreenSpaceBindings         descriptor contracts of the four programs
  ScreenSpaceReference        BuildScreenSpaceParams and the CPU definition of every pass (ScreenSpace::)
  ScreenSpaceEffects          Record: GTAO, blur, reflections, composite
  ScreenSpaceGraphResources   what one Record scheduled
Shaders/Slang/ScreenSpace
  ScreenSpaceRecords.slang    mirrors the record and the shared helpers
  ScreenSpaceAo.slang         SwimScreenSpaceAo
  ScreenSpaceBlur.slang       SwimScreenSpaceBlur
  ScreenSpaceReflection.slang SwimScreenSpaceReflection
  ScreenSpaceComposite.slang  SwimScreenSpaceComposite
Renderer/ForwardPlus          the Normal, Indirect, Reflectance and Specular targets
```

`Renderer/ScreenSpace` depends only on RenderGraph and the RHI contract. No other renderer module includes it. `scripts/verify-build-layout.py` enforces this.

## A frame

```text
ForwardPlusRenderer::Record  -> Color, Depth (Sampled), Normal, Indirect, Reflectance, Specular (item 76), Velocity (item 75)
      |
ScreenSpaceEffects::Record(graph, { Color, Depth, Normal, Indirect, Reflectance, Specular, View, Settings, NoiseFrame })
  1. GTAO         depth + normals -> raw visibility (R32Float, unclamped)
  2. blur         5x5 depth-aware -> visibility, clamped, ^Power (R32Float)
  3. reflections  one mirror ray per pixel through the depth buffer -> hit radiance + confidence (RGBA16Float)
  4. composite    color - (1 - ao) * indirect, + confidence * ao * (reflectance * hit - specular), then height fog
                  -> Output (RGBA16Float)
      |
TemporalAntiAliasing::Record(Output, Depth, Velocity) -> PostProcessor
```

- `View` is the camera the inputs were rendered with: the world-to-view matrix, the unjittered projection (reverse-Z perspective) and the frame's NDC jitter. Every pass reconstructs view positions through the inverse projection, with the jitter removed.
- `NoiseFrame` rotates the AO noise; pass the frame index so that TAA averages it away.
- **AO off:** passes 1–2 are skipped; a 1×1 stand-in fills the AO slot and is never read.
- **Reflections off:** pass 3 is skipped; one 1×1 zero stand-in fills the reflection, reflectance and specular slots. `Reflectance` and `Specular` may then be omitted.
- **Everything off:** nothing is recorded, and `Output` is the input color (`Passthrough`).
- The reflection program (`ScreenSpaceEffectsDesc::Reflection`) is optional; recording reflections without it throws.

## Forward+ targets (item 76)

| Target | Format | Opaque pixels | Elsewhere |
| --- | --- | --- | --- |
| `ForwardPlusTargets::Normal` | RGBA16Float | world shading normal (after normal mapping and back-face flips), perceptual roughness in w | 0 |
| `ForwardPlusTargets::Indirect` | RGBA16Float | ambient × base color × occlusion + IBL (`ForwardPlus::IndirectRadiance`), the part of Color that AO may remove | 0 |
| `ForwardPlusTargets::Reflectance` | RGBA16Float | split-sum specular reflectance `(kS × A + B) × occlusion` (`StandardPbr::EnvironmentSpecularWeight`), the weight of a reflected radiance | 0 |
| `ForwardPlusTargets::Specular` | RGBA16Float | specular IBL radiance, `Prefiltered × Reflectance`: the part of Indirect a reflection replaces | 0 |

- The transparent pass writes (0, 0, 0, alpha) to the indirect, reflectance and specular targets with the same premultiplied blend as color. Behind glass they therefore hold the transmitted share, exactly what is still inside Color.
- The heatmap debug view writes 0, so AO and SSR leave it alone.
- Transient targets stand in when none are supplied, like the velocity target.
- The opaque pass now has seven color attachments. Vulkan's minimum `maxColorAttachments` is 4; every desktop driver (NVIDIA, AMD, Intel, Mesa, MoltenVK) exposes 8, and the Vulkan backend rejects pipelines above the device limit. Packing the thin G-buffer (octahedral normals, a single reflectance/specular target) is a later bandwidth optimization.
- **Reflectance without IBL:** the reflectance needs the split-sum LUT's (A, B). A `ForwardPlusFrame::BrdfLut` may now be supplied without an environment (`EnvironmentBuilder::RecordBrdfLut`): it is bound and sets `ForwardViewFlagBrdfLut`, and the specular radiance is 0. With neither, the reflectance is 0 and reflections have no effect. (Karis' analytic split-sum fit was tried as a LUT-free fallback and rejected: against this engine's height-correlated LUT it is off by up to 0.4.)

The reflectance/specular split exists because a forward renderer has already added the specular IBL into Color. A reflection found on screen must *replace* that term, not be added on top, and a colored metal must tint what it reflects. Both need the specular IBL (to subtract) and the reflectance (to weight the hit) per pixel: six values that no existing target has room for.

Ambient occlusion describes how much *ambient* light reaches a point. A forward renderer that darkens its whole output also darkens direct sunlight and emission. With the indirect target, the composite removes the occluded share of indirect light only.

## GTAO (ScreenSpaceAo.slang = ScreenSpace::GtaoTexel)

Horizon-based, cosine-weighted visibility after Jimenez et al., *Practical Real-Time Strategies for Accurate Indirect Occlusion* (2016). For a pixel with view position P, view normal N and view vector V = −P/|P|:

1. **Radius:** the world-space `Radius` in pixels, `Radius × P₀₀ × width / 2 / −z`, capped at `MaxRadiusPixels`. Below one pixel the pixel is open (1).
2. **Slices:** `SliceCount` directions `φ = (s + ξ₁) π / SliceCount`, with ξ₁ from interleaved gradient noise. The slice plane contains V and the view direction (cos φ, −sin φ, 0). The screen moves along (cos φ, sin φ), because screen y points down.
3. **Normal in the slice:** N projected onto the slice plane, with its length and signed angle n from V.
4. **Horizons:** on each side, `StepCount` samples at `(k + ξ₂) / StepCount` of the radius, snapped to pixel centres. Each sample's elevation cosine is faded toward the normal's hemisphere bound, cos(n ± π/2), over the last `Falloff` × `Radius` of distance. The highest one is the horizon. Samples off screen, on the sky or on the pixel itself are skipped.
5. **Integral:** horizons are clamped to n ± π/2, and each slice adds `|N_proj| × Σ ¼ (cos n + 2h sin n − cos(2h − n))`.

The raw output is the slice average, not yet clamped. A single slice of an open surface can exceed 1, and clamping before the blur would bias open surfaces dark (0.86 instead of 1 with one slice).

Sky pixels and pixels without a normal (no opaque surface) are 1.

## Blur (ScreenSpaceBlur.slang = ScreenSpace::BlurTexel)

A 5×5 average weighted by `max(0, 1 − |z − z₀| / (BlurDepthTolerance × z₀))` in view depth, skipping sky neighbours. Averaging 25 differently rotated slices removes most of the noise; TAA takes the rest. The result is clamped to [0, 1] and raised to `Power`.

## Reflections (ScreenSpaceReflection.slang = ScreenSpace::ReflectionTexel)

One mirror ray per pixel, marched in screen space with perspective-correct depth (after McGuire and Mara, *Efficient GPU Screen-Space Ray Tracing*, 2014). For a pixel with view position P, view normal N and roughness r (`Normal.w`):

1. **Eligibility:** sky and normal-less pixels, and `r ≥ MaxRoughness`, trace nothing.
2. **Ray:** `R = reflect(P/|P|, N)`, `MaxDistance` long in view space, clipped to the near plane (`SsrNearZ = −p₁₁ / (p₁₀ + 1)` from the projection; rays toward the camera end on it).
3. **March:** both ends are projected to pixels (with the frame's jitter). The ray is sampled at `tᵢ = min((i + j) / N, 1)`, i = 1..N, with `N = min(MaxSteps, ⌈pixel length / Stride⌉)` and j the pixel's interleaved-gradient noise, rotated per frame for TAA. The ray's view depth at t is interpolated perspective-correctly (z/w and 1/w are linear in screen space).
4. **Hit test:** at each sample pixel the ray is a candidate when its depth lies within `[scene depth, scene depth + Thickness]`, or when it is behind the scene and the previous sample was not (a *crossing*: a thin intersection the stride stepped over). Sky pixels and the start pixel are never behind; leaving the screen is a miss. Without the crossing test, whether a ray near a silhouette hit depended on where its samples happened to land, so reflected edges aliased to the stride and TAA smeared the stair steps into fuzzy jaggies.
5. **Refinement:** `RefineSteps` bisections of the last step move the hit to the first sample behind the depth buffer. A refined pixel on the sky (or the start pixel) falls back to the sample's pixel. The candidate is kept only if the ray is at most `max(Thickness, ray depth span of the final bisection interval)` behind the scene there; otherwise the ray passed behind a closer surface and the march continues.
6. **Back faces:** a hit whose normal faces along the ray is rejected, so rays that slip behind thin objects do not return their back sides.
7. **Confidence:** `clamp((MaxRoughness − r) / RoughnessFade)` × screen-edge fade (exact hit position within `EdgeFade` of the border) × distance fade (the last `DistanceFade` of `MaxDistance`).
8. **Radiance** (`FilteredHitRadiance`): the four texels around the exact (sub-pixel) hit position, each its color minus `(1 − ao) × indirect` when AO is on (so a reflected corner is as occluded as the corner itself), bilinearly weighted and divided by `1 + luminance`, then renormalized. The sub-pixel position keeps reflected edges smooth; the luminance weight stops a single bright texel (a specular highlight, a light orb) from flickering through the reflection as the per-frame jitter moves the hit.

The output is (radiance, confidence), or 0 on a miss. Glossy surfaces are faded out by roughness rather than blurred; importance-sampled glossy rays and a resolve filter are later work.

## Composite (ScreenSpaceComposite.slang = ScreenSpace::CompositeTexel)

1. **AO:** `color.rgb − (1 − ao) × indirect.rgb`, clamped at 0. Alpha is kept.
2. **Reflections:** where the reflection pass found a hit, `+ confidence × ao′ × (reflectance × radiance − specular)`, clamped at 0, with `ao′` the pixel's visibility when AO is on and 1 otherwise. After step 1 exactly `ao′ × specular` of the specular IBL is left in the color; this replaces that much of it by the equally occluded reflection. A miss (confidence 0) leaves the IBL untouched.
3. **Fog:** along the camera ray to the pixel's surface (the sky uses `MaxDistance`; surfaces are capped there):
   - density `Density × exp(−HeightFalloff × (y − BaseHeight))`, integrated in closed form from `StartDistance`: `τ = ρ(y₀) L (1 − e^(−k)) / k` with `k = HeightFalloff × dir.y × L`, using a series near k = 0;
   - transmittance `T = e^(−τ)`;
   - in-scattering `Color + SunColor × HG(g, −SunDirection · dir)`, where the Henyey-Greenstein phase is scaled so an isotropic medium (g = 0) gives 1, and it peaks when looking toward the sun;
   - `color × T + inscatter × (1 − T)`.

Fog uses the opaque depth, so transparent surfaces in front are fogged as if they sat at the opaque surface behind them.

## Settings

| `AmbientOcclusionSettings` | Default | Range |
| --- | --- | --- |
| `Enabled` | true | |
| `Radius` | 0.5 m | > 0 |
| `Falloff` | 0.4 | (0, 1] fraction of the radius |
| `Power` | 1 | (0, 8] |
| `MaxRadiusPixels` | 64 | [1, 256] |
| `SliceCount` | 2 | 1 .. 4 |
| `StepCount` | 4 | 1 .. 8 per side |
| `BlurDepthTolerance` | 0.1 | (0, 1] relative depth |

| `FogSettings` | Default | Range |
| --- | --- | --- |
| `Enabled` | false | |
| `Density` | 0.02 /m | ≥ 0 at `BaseHeight` |
| `HeightFalloff` | 0.1 /m | ≥ 0 (0 = uniform) |
| `BaseHeight` | 0 | finite |
| `Color` | (0.5, 0.6, 0.7) | ≥ 0, linear radiance |
| `SunColor` | 0 | ≥ 0 |
| `SunDirection` | (0, −1, 0) | direction the light travels; normalized |
| `Anisotropy` | 0.6 | [−0.95, 0.95] |
| `StartDistance` | 0 | ≥ 0 |
| `MaxDistance` | 1,000 | > `StartDistance` |

| `ReflectionSettings` | Default | Range |
| --- | --- | --- |
| `Enabled` | false | |
| `MaxDistance` | 20 m | > 0, view-space ray length |
| `Thickness` | 0.3 m | > 0, how far behind the depth buffer a sample still hits |
| `Stride` | 2 px | [1, 64] |
| `MaxSteps` | 64 | 1 .. 256 |
| `RefineSteps` | 4 | 0 .. 8 |
| `MaxRoughness` | 0.6 | (0, 1] |
| `RoughnessFade` | 0.2 | (0, `MaxRoughness`] |
| `EdgeFade` | 0.1 | (0, 0.5] of the screen |
| `DistanceFade` | 0.25 | (0, 1] of `MaxDistance` |

## Record contract

- **Color:** a sampled, single-sample 2D RGBA16Float texture.
- **Depth:** the same size and sampled, either D32Float (bound through a depth-aspect view) or R32Float. Reverse-Z.
- **Normal, Indirect:** the same size, sampled, RGBA16Float.
- **Reflectance, Specular:** required with reflections on; the same size, sampled, RGBA16Float.
- **View:** the projection must be perspective (row 3 = 0, 0, −1, 0) and invertible; the view must be invertible; the jitter must be finite. With reflections on, its near plane must lie in front of the camera.
- **Errors:** an invalid frame throws `std::invalid_argument` before anything is recorded.
- **Resources:** `ScreenSpaceGraphResources` names the output, both visibility textures, the reflection texture, the params and the four passes, so callers can read any stage back.

## Not yet

- Glossy reflections (GGX importance-sampled rays with a resolve filter), hierarchical-Z tracing, and reflections of the previous frame's final image (which would include fog and bloom).
- Reflections of transparent surfaces (the reflection pass reflects only opaque depth).
- Multi-bounce AO (colored by albedo), specular occlusion and bent normals.
- Volumetric fog with shadowed light shafts, and local fog volumes.
- Half-resolution AO with upsampling.
- Engine wiring (item 56).

## Tests

| Suite | What it proves |
| --- | --- |
| `Render.ScreenSpace.Reference` (12) | **Params:** they pack the inverse projection and view (the camera position), the pixel radius scale and the normalized sun; every invalid setting, an orthographic projection, a singular view and NaN jitter are rejected. **Positions:** view positions invert the jittered projection; view normals; the noise range and per-frame rotation. **GTAO** over ray-cast scenes: open floors and walls average above 0.97; the floor at a wall's base is about 0.62; occlusion fades with distance and grows with the radius; slice counts agree on average; power applies after the blur; sub-pixel radii and normal-less pixels are open. **Blur:** keeps constants and does not cross a box's silhouette. **Composite:** AO removes exactly the occluded indirect light, clamps at 0 and keeps alpha. **Fog:** matches uniform and numeric height integrals to 10⁻⁴, including level and grazing rays and the start distance; the phase function integrates to 4π; per-pixel composite, the sky at the max distance, a brighter view toward the sun, and zero density as the identity. **Reflections** over a ray-cast mirror floor in front of a box: of the floor pixels whose true mirror ray hits a surface the camera also sees, 98 % find it and 99.6 % of those land within 0.1 m of the analytic hit (travelled distance within 0.15 m); 99.9 % of sky-bound rays miss. Confidence is exactly the roughness, edge and distance fades; rays at or above `MaxRoughness` trace nothing; back faces are rejected; a mirror facing the camera clips its rays to the near plane; with a 4-pixel stride the crossing test keeps about three quarters of the one-pixel hits (the thickness test alone kept 42 %) and 97 % of them land within 1.5 px of the one-pixel hit; the filtered hit radiance returns texel centres exactly, keeps uniform areas and damps a lone highlight; every invalid setting and a near plane behind the camera are rejected. **Composite:** the specular IBL is replaced by `confidence × ao × (reflectance × hit − specular)`, misses and disabled reflections change nothing, the result clamps at 0, and the hit radiance carries the hit's own occlusion |
| `Render.ScreenSpaceEffects` (4) | On the mock device: three dispatches in order with 8×8 groups and no push constants, the composite's bindings (a depth-aspect D32 view or plain R32Float), the params upload; fog alone runs only the composite with the AO and reflection stand-ins (one 1×1 texture in all three reflection slots); everything off records nothing. Reflections record AO, blur, reflections, composite in that order, or reflections and composite alone; the composite binds the supplied reflectance and specular. Every rejected program, input and setting, including reflections without their program or inputs |
| `ShaderCompiler.ScreenSpaceLayout` (1) | The four programs reflect their bindings, 8×8 groups and no push constants, and their parameter record equals `GpuScreenSpaceParams` (400 bytes, including the projection and every reflection field) |
| `ShaderCompiler.GpuAvBudget` | AO 12, blur 6, reflections 18 and composite 17 instrumented buffer accesses (limit 75) |
| `Render.ForwardPlus.Reference`, `Render.ForwardPlusRenderer` | `IndirectRadiance` is exactly ambient + IBL; `SpecularEnvironment` is its specular part and weight; seven opaque and four transparent attachments; transient and supplied targets; a LUT without an environment |
| Native `ScreenSpaceEffectsMatchTheCpuReference` | See below |
| Native `ClusteredForwardPlusMatchesTheCpuReference` | Now also compares the normal + roughness, indirect, reflectance and specular targets of every interior opaque pixel with the ray cast, through transparent layers |

### Native smoke

`ScreenSpaceEffectsMatchTheCpuReference` ray-casts a floor with a wall, a pillar and a floating slab at 256×144. It uploads the depth, the normals (binary16), a lit color with its indirect part, and a reflectance (0.04 on the floor, 0.5 elsewhere) with a sky-colored specular IBL of that weight, then reads back every stage:

- **Raw GTAO:** against `ScreenSpace::GtaoTexel` over the same inputs, within 0.02 on at least 99 % of texels. GPU trigonometry can move a pixel-snapped sample by one pixel.
- **Blur:** against `BlurTexel` over the GPU's raw visibility, within 10⁻³ on at least 99.9 %.
- **Reflections:** against `ReflectionTexel` over the same inputs and the GPU's visibility, within 3·10⁻³ relative + 10⁻³ on at least 98 % (a march may step to a neighbouring pixel where the GPU rounds differently), and at least 5 % of the texels must hit.
- **Output:** against `CompositeTexel` over the GPU's visibility and reflections, within 3·10⁻³ relative + 10⁻⁴ on at least 99.9 %.
- The floor at the wall's base must be at least 0.2 darker than the open floor.

Frames:

1. AO alone (D32 depth);
2. AO with 4 slices, 8 steps and power 1.5, height fog with a sun lobe, jitter (0.31, −0.27) pixels and R32Float depth;
3. fog alone;
4. everything off (nothing recorded);
5. reflections alone (roughness 0.1, stride 1, 128 steps);
6. AO, reflections (6 refinement steps) and fog, jittered by (−0.22, 0.35) pixels, with R32Float depth (roughness 0.25);
7. a 1080p frame with AO, reflections and fog that prints the AO, blur, reflection and composite times.

On Mesa lavapipe (see [the item 76 record](validation/Item76-2026-09-24.md)) the GPU and CPU reflections agree exactly: 10,027 and 9,985 hits in frames 5 and 6 with 0 outliers.
