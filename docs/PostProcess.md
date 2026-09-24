# Post-processing: exposure, tone mapping, bloom and grading

This covers critical-path items **73** (HDR scene color, exposure and tone mapping) and **74** (bloom and color grading), the first Phase 17 post-stack checkpoint.

`Renderer/PostProcess` turns HDR scene color into display output with graph-scheduled compute passes. Forward+ already renders to an RGBA16Float target. Every effect can be turned off cleanly:

- manual exposure skips the histogram;
- disabled bloom records no bloom passes;
- default grading values skip grading entirely.

```text
Renderer/PostProcess
  PostProcessSettings         exposure, bloom, grading, tone mapping and output settings + validation
  PostProcessRecords          GpuExposureState (16 B, persistent), GpuPostParams (128 B), push constants
  PostProcessBindings         descriptor contract of the six programs
  PostProcessReference        CPU definition of every pass (Post::)
  PostProcessor               Record: histogram, exposure, bloom chains, composite
  PostProcessGraphResources   what one Record scheduled
Shaders/Slang/PostProcess
  PostProcessRecords.slang    mirrors the records and every Post:: helper
  PostHistogram.slang         SwimPostHistogram
  PostExposure.slang          SwimPostExposure
  PostBloomDownsample.slang   SwimPostBloomDownsample
  PostBloomUpsample.slang     SwimPostBloomUpsample
  PostComposite.slang         SwimPostComposite (rgba8) and SwimPostCompositeHdr (POST_OUTPUT_HDR=1, rgba16f)
```

`Renderer/PostProcess` depends only on RenderGraph and the RHI contract. No other renderer module includes it. `scripts/verify-build-layout.py` enforces this.

## A frame

```text
HDR scene color (RGBA16Float, e.g. ForwardPlusTargets::Color)
      |
PostProcessor::Record(graph, { Source, Output, Settings, DeltaTime })
  1. histogram clear + histogram     (automatic exposure only)
  2. exposure                        one thread, persistent GpuExposureState
  3. bloom down 0..n-1               level 0: exposure, Karis average, threshold
     bloom up n-2..0                 up[i] = down[i] + tent(up[i + 1])
  4. composite                       exposure + bloom -> grading -> tone map -> encoding
      |
Output: RGBA8Unorm (sRGB) or RGBA16Float (HDR10 PQ / scRGB), Storage
```

## Exposure (item 73)

- **Histogram:** 256 bins of log2 Rec.709 luminance between `MinLog2Luminance` and `MaxLog2Luminance` (default −12..12).
  - Bin 0 collects black, NaN and anything darker than the range; the average ignores it.
  - Each 16×16 group bins into group-shared counters, then adds its non-zero bins to the global histogram with atomics, so the counts are deterministic.
- **Average:** the pass averages the bin centers between `LowPercentile` and `HighPercentile` of the non-black pixels (default 50–95 %), so dark shadows and bright highlights don't swing the exposure.
- **EV100:** reflected-light metering (K = 12.5, ISO 100): `EV100 = log2(L) + 3`, minus `Compensation`, clamped to `[MinEv100, MaxEv100]`.
- **Adaptation:** `EV += (target − EV) × (1 − e^(−dt × speed))`, with `SpeedUp` toward brighter scenes and the slower `SpeedDown` toward darker ones.
  - The first frame, `ResetExposureHistory()` (camera cuts) and an invalid state snap to the target.
  - With no measurement, the previous average is kept (18 % grey on a snap).
- **Exposure:** `1 / (1.2 × 2^EV100)`, saturation-based: the largest unclipped luminance maps to 1.
- **Manual mode:** uses `ManualEv100 − Compensation`, without a histogram or adaptation.
- **Determinism:** the exposure pass runs in one thread, so its reduction order is exactly `Post::UpdateExposure`'s.

The state lives in a persistent 16-byte buffer (`GetExposureStateBuffer`). It is imported into every graph, read and written by the exposure pass, and read by the bloom and composite passes.

## Bloom (item 74)

- **Levels:** level i is `(w >> (i + 1)) × (h >> (i + 1))` (`MipCount` 1–8; fewer on small images). Every level is an RGBA16Float storage texture.
- **Downsample:** Jimenez's 13-tap filter. Every tap sits on a texel corner, so each tap is an exact 2×2 box average made of `Load`s, not bilinear samples. That makes the CPU definition bit-reproducible.
  - Weights: 0.5 for the centre square, 0.125 for each of the four outer squares.
- **Level 0** additionally:
  - multiplies by the exposure, so the threshold is in display-relative units;
  - uses the Karis average, weighting each group by `1 / (1 + luma)`, which stops single fireflies from flickering;
  - applies a soft-knee threshold (`Threshold`, `Knee`).
- **Upsample:** `up[i] = down[i] + tent(up[i + 1])`. The tent is a 3×3 `[1 2 1]` filter over bilinear taps at the texel's position, folded into 4×4 exact `Load` weights.
- **Composite:** adds `Intensity / levels × tent(up[0])` to the exposed color at full resolution.

## Grading (item 74)

Grading runs in linear Rec.709 after exposure and bloom, before tone mapping, in this order:

1. **White balance** (`Temperature`, `Tint` in −100..100): von Kries scaling in LMS toward the CIE daylight locus. The 3×3 matrix is built on the CPU, and zero is exactly the identity.
2. **Contrast** around 18 % grey: `0.18 × (c / 0.18)^contrast`.
3. **ASC CDL:** `(c × slope + offset)^power`.
4. **Saturation** around Rec.709 luminance.

Default values set `GradingEnabled = 0`, and the shader skips grading entirely.

## Tone mapping and output (item 73)

| `ToneMapper` | Definition |
| --- | --- |
| `Clamp` | saturate (debugging) |
| `Reinhard` | per channel `x (1 + x / W²) / (1 + x)`, `WhitePoint` W maps to 1 |
| `Aces` | ACES RRT + ODT, Stephen Hill's fit with its input/output matrices |
| `PbrNeutral` (default) | Khronos PBR Neutral: hue preserving, linear (minus 0.04) below 0.76, then compressed toward white |

| `OutputEncoding` | Target | Encoding |
| --- | --- | --- |
| `Srgb` | RGBA8Unorm | tone map → sRGB OETF → optional ±½ LSB interleaved-gradient-noise dither |
| `Hdr10` | RGBA16Float | tone map relative to `PeakNits / PaperWhiteNits`, × paper white in nits → BT.709→BT.2020 → SMPTE ST 2084 (PQ) |
| `ScRgb` | RGBA16Float | the same nits in linear BT.709, 1.0 = 80 nits |

In HDR, the tone mapper's white is the display peak (default 1,000 nits), and SDR white is `PaperWhiteNits` (default 200). Compute shaders can't store to sRGB formats, so the sRGB curve is applied in the shader and stored to an RGBA8Unorm image.

## Record contract

- **Source:** a sampled, single-sample 2D RGBA16Float texture.
- **Output:** a source-sized Storage texture: RGBA8Unorm for `Srgb`, RGBA16Float for HDR10 or scRGB.
- **`DeltaTime`:** finite and ≥ 0.
- **Settings:** checked by `ValidatePostProcessSettings`.
- **Errors:** an invalid frame throws `std::invalid_argument`.
- **Resources:** `PostProcessGraphResources` names the histogram, the imported exposure state, the params, both bloom chains and the exposure and composite passes, so callers can read any stage back.

## Not yet

- Depth of field and motion blur (later). AO, reflections and fog ([Screen-space effects](ScreenSpace.md), item 76) and TAA (item 75) run before this stack: `TemporalAntiAliasing`'s output is a valid `PostProcessFrame::Source` (see [Temporal anti-aliasing](TemporalAntiAliasing.md)).
- A 3D grading LUT and user LUT import.
- Local exposure.
- Physical camera (aperture, shutter, ISO) presets.
- Writing to the swapchain, and HDR mastering metadata (`VK_EXT_hdr_metadata`). The engine wiring (item 56) connects the output to presentation.

## Tests

| Suite | What it proves |
| --- | --- |
| `Render.PostProcess.Reference` (10) | Binary16 rounding. Each tone mapper is monotonic, bounded, grey-preserving and matches known values (ACES at 18 % grey, PBR Neutral's toe and linear section, Reinhard's white point). sRGB and PQ against their standards (100 nits → 0.5081), round trips and BT.2020 white. Grading is the identity by default, and white balance, contrast pivot, CDL and saturation each behave as defined. Histogram bins, the black bin and percentile averaging. Exposure snap, asymmetric adaptation, convergence, compensation, clamps, fallbacks and manual mode. Bloom filters preserve constants, the threshold is continuous, and Karis tames fireflies. Full frames in all three encodings, with and without bloom and dither. Settings validation |
| `Render.PostProcessor` (4) | On the mock device: 12 dispatches for the full chain with correct group counts, pipelines and push constants (reset on the first frame and after `ResetExposureHistory`), the composite's bindings and output view. Manual exposure without bloom records 2 dispatches plus the stand-in upload, and a single bloom level reads its downsample directly. HDR selects the HDR composite. Every rejected frame |
| `ShaderCompiler.PostProcessLayout` (1) | All six programs match `PostProcessBindings` (types, push-constant sizes, thread groups), and `ExposureState` and `PostParams` equal the C++ records |
| `ShaderCompiler.GpuAvBudget` | The post programs stay below GPU-AV's 75 instrumented buffer accesses (composite 19) |
| Native `PostProcessMatchesTheCpuReference` | See below |

### Native smoke

`PostProcessMatchesTheCpuReference` uploads a wide-range HDR image: a ten-stop gradient, a black strip and emissive disks up to 1,500. It reads back every stage:

- **Histogram:** equal in total, and at most 0.1 % of pixels in a different bin (log2 ulps at bin edges).
- **Exposure state:** equal to `Post::UpdateExposure` over the GPU's histogram and previous state.
- **Bloom levels:** each level is recomputed from the GPU's own input level. Mismatches beyond half-precision tolerance are allowed on at most 0.1 % of values.
- **Output:** every texel equals `Post::CompositeTexel` with the GPU's exposure and bloom: SDR within one 8-bit step, HDR within 4·10⁻³.

Frames:

1. automatic exposure, sRGB with dither;
2. a 16× brighter image after 0.1 s: the exposure must move toward it without reaching it;
3. manual EV 4 with full grading, ACES and 3 bloom levels;
4. HDR10 after a history reset;
5. scRGB with Reinhard and no bloom;
6. a 1080p frame that prints pass timings.
