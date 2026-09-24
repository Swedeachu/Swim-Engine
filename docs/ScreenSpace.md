# Screen-space ambient occlusion and fog

This covers the first part of critical-path item **76**, the optional screen-space modules: ground-truth-based ambient occlusion (GTAO) and exponential height fog. Screen-space reflections, the third module of item 76, come next.

`Renderer/ScreenSpace` runs between Forward+ and TAA. It reads the scene depth and two new Forward+ targets: the shading normals, and the indirect radiance. It writes a new HDR color.

```text
Renderer/ScreenSpace
  ScreenSpaceSettings         AO and fog settings + validation
  ScreenSpaceRecords          GpuScreenSpaceParams (288 B, one upload per frame)
  ScreenSpaceBindings         descriptor contracts of the three programs
  ScreenSpaceReference        BuildScreenSpaceParams and the CPU definition of every pass (ScreenSpace::)
  ScreenSpaceEffects          Record: GTAO, blur, composite
  ScreenSpaceGraphResources   what one Record scheduled
Shaders/Slang/ScreenSpace
  ScreenSpaceRecords.slang    mirrors the record and the shared helpers
  ScreenSpaceAo.slang         SwimScreenSpaceAo
  ScreenSpaceBlur.slang       SwimScreenSpaceBlur
  ScreenSpaceComposite.slang  SwimScreenSpaceComposite
Renderer/ForwardPlus          the Normal and Indirect targets
```

`Renderer/ScreenSpace` depends only on RenderGraph and the RHI contract. No other renderer module includes it. `scripts/verify-build-layout.py` enforces this.

## A frame

```text
ForwardPlusRenderer::Record  -> Color, Depth (Sampled), Normal, Indirect (item 76), Velocity (item 75)
      |
ScreenSpaceEffects::Record(graph, { Color, Depth, Normal, Indirect, View, Settings, NoiseFrame })
  1. GTAO        depth + normals -> raw visibility (R32Float, unclamped)
  2. blur        5x5 depth-aware -> visibility, clamped, ^Power (R32Float)
  3. composite   color - (1 - ao) * indirect, then height fog -> Output (RGBA16Float)
      |
TemporalAntiAliasing::Record(Output, Depth, Velocity) -> PostProcessor
```

- `View` is the camera the inputs were rendered with: the world-to-view matrix, the unjittered projection (reverse-Z perspective) and the frame's NDC jitter. Every pass reconstructs view positions through the inverse projection, with the jitter removed.
- `NoiseFrame` rotates the AO noise; pass the frame index so that TAA averages it away.
- **AO off:** only the composite runs, with a 1×1 stand-in it never reads.
- **AO and fog both off:** nothing is recorded, and `Output` is the input color (`Passthrough`).

## Forward+ targets (item 76)

| Target | Format | Opaque pixels | Elsewhere |
| --- | --- | --- | --- |
| `ForwardPlusTargets::Normal` | RGBA16Float | world shading normal (after normal mapping and back-face flips), perceptual roughness in w | 0 |
| `ForwardPlusTargets::Indirect` | RGBA16Float | ambient × base color × occlusion + IBL (`ForwardPlus::IndirectRadiance`), the part of Color that AO may remove | 0 |

- The transparent pass writes (0, 0, 0, alpha) to the indirect target with the same premultiplied blend as color. Behind glass it therefore holds the transmitted indirect light, exactly the share still inside Color.
- The heatmap debug view writes 0, so AO leaves it alone.
- Transient targets stand in when none are supplied, like the velocity target.
- The opaque pass now has five color attachments. Vulkan's minimum `maxColorAttachments` is 4; desktop drivers expose 8.

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

## Composite (ScreenSpaceComposite.slang = ScreenSpace::CompositeTexel)

1. **AO:** `color.rgb − (1 − ao) × indirect.rgb`, clamped at 0. Alpha is kept.
2. **Fog:** along the camera ray to the pixel's surface (the sky uses `MaxDistance`; surfaces are capped there):
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

## Record contract

- **Color:** a sampled, single-sample 2D RGBA16Float texture.
- **Depth:** the same size and sampled, either D32Float (bound through a depth-aspect view) or R32Float. Reverse-Z.
- **Normal, Indirect:** the same size, sampled, RGBA16Float.
- **View:** the projection must be perspective (row 3 = 0, 0, −1, 0) and invertible; the view must be invertible; the jitter must be finite.
- **Errors:** an invalid frame throws `std::invalid_argument` before anything is recorded.
- **Resources:** `ScreenSpaceGraphResources` names the output, both visibility textures, the params and the three passes, so callers can read any stage back.

## Not yet

- Screen-space reflections (the rest of item 76).
- Multi-bounce AO (colored by albedo), specular occlusion and bent normals.
- Volumetric fog with shadowed light shafts, and local fog volumes.
- Half-resolution AO with upsampling.
- Engine wiring (item 56).

## Tests

| Suite | What it proves |
| --- | --- |
| `Render.ScreenSpace.Reference` (7) | **Params:** they pack the inverse projection and view (the camera position), the pixel radius scale and the normalized sun; every invalid setting, an orthographic projection, a singular view and NaN jitter are rejected. **Positions:** view positions invert the jittered projection; view normals; the noise range and per-frame rotation. **GTAO** over ray-cast scenes: open floors and walls average above 0.97; the floor at a wall's base is about 0.62; occlusion fades with distance and grows with the radius; slice counts agree on average; power applies after the blur; sub-pixel radii and normal-less pixels are open. **Blur:** keeps constants and does not cross a box's silhouette. **Composite:** AO removes exactly the occluded indirect light, clamps at 0 and keeps alpha. **Fog:** matches uniform and numeric height integrals to 10⁻⁴, including level and grazing rays and the start distance; the phase function integrates to 4π; per-pixel composite, the sky at the max distance, a brighter view toward the sun, and zero density as the identity |
| `Render.ScreenSpaceEffects` (2) | On the mock device: three dispatches in order with 8×8 groups and no push constants, the composite's bindings (a depth-aspect D32 view or plain R32Float), the params upload; fog alone runs only the composite with the stand-in; everything off records nothing. Every rejected program, input and setting |
| `ShaderCompiler.ScreenSpaceLayout` (1) | The three programs reflect their bindings, 8×8 groups and no push constants, and their parameter record equals `GpuScreenSpaceParams` |
| `ShaderCompiler.GpuAvBudget` | AO 12, blur 6 and composite 16 instrumented buffer accesses (limit 75) |
| `Render.ForwardPlus.Reference`, `Render.ForwardPlusRenderer` | `IndirectRadiance` is exactly ambient + IBL; five opaque and two transparent attachments; transient and supplied targets |
| Native `ScreenSpaceEffectsMatchTheCpuReference` | See below |
| Native `ClusteredForwardPlusMatchesTheCpuReference` | Now also compares the normal + roughness and indirect targets of every interior opaque pixel with the ray cast, through transparent layers |

### Native smoke

`ScreenSpaceEffectsMatchTheCpuReference` ray-casts a floor with a wall, a pillar and a floating slab at 256×144. It uploads the depth, the normals (binary16), and a lit color with its indirect part, then reads back every stage:

- **Raw GTAO:** against `ScreenSpace::GtaoTexel` over the same inputs, within 0.02 on at least 99 % of texels. GPU trigonometry can move a pixel-snapped sample by one pixel.
- **Blur:** against `BlurTexel` over the GPU's raw visibility, within 10⁻³ on at least 99.9 %.
- **Output:** against `CompositeTexel` over the GPU's visibility, within 3·10⁻³ relative + 10⁻⁴ on at least 99.9 %.
- The floor at the wall's base must be at least 0.2 darker than the open floor.

Frames:

1. AO alone (D32 depth);
2. AO with 4 slices, 8 steps and power 1.5, height fog with a sun lobe, jitter (0.31, −0.27) pixels and R32Float depth;
3. fog alone;
4. everything off (nothing recorded);
5. a 1080p frame that prints the AO, blur and composite times.
