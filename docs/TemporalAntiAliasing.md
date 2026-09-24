# Temporal anti-aliasing and motion vectors

This covers critical-path item **75**: motion vectors, sub-pixel jitter and temporal anti-aliasing (TAA), the second Phase 17 post-stack checkpoint.

Forward+ renders each frame with a different sub-pixel jitter and writes a motion vector per opaque pixel. `Renderer/Temporal` then accumulates frames: it reprojects last frame's result through the motion vectors, rejects history that no longer matches, and blends. Aliased edges converge to their coverage, and moving objects leave no trail.

```text
Renderer/Temporal
  TemporalSettings          Feedback, ClipGamma, JitterPhases + validation
  TemporalRecords           TemporalResolveConstants (32 B push constants)
  TemporalBindings          descriptor contract of SwimTemporalResolve
  TemporalReference         CPU definition of the jitter and the resolve (Temporal::)
  TemporalAntiAliasing      jitter sequence, persistent history, Record
  TemporalGraphResources    what one Record scheduled
Shaders/Slang/Temporal
  TemporalRecords.slang     mirrors the records and every Temporal:: helper
  TemporalResolve.slang     SwimTemporalResolve (8 x 8 compute)
Renderer/ForwardPlus         jitter, previous view-projection and the velocity target
```

`Renderer/Temporal` depends only on RenderGraph and the RHI contract. No other renderer module includes it: Forward+ takes the jitter as two floats and writes velocity into a plain graph texture. `scripts/verify-build-layout.py` enforces this.

## A frame

```text
jitter = taa.GetJitterNdc(settings, width, height)
ForwardPlusView { ViewProjection (unjittered), PreviousViewProjection = last frame's, Jitter = jitter }
ForwardPlusRenderer::Record(graph, frame, { Color, ObjectId, Depth (Sampled), Velocity (Sampled) })
      |
ScreenSpaceEffects::Record (item 76, optional) -> AO and fog applied to Color
      |
TemporalAntiAliasing::Record(graph, { Color, Depth, Velocity, Settings })
  resolve    one compute pass: history (last output) + current -> output (next history)
      |
Output: RGBA16Float (Sampled, Storage), exported ShaderRead -> PostProcessor Source
```

The caller keeps last frame's unjittered view-projection. Each `Record` advances the jitter sequence and makes its output the next history, so it assumes the graph executes.

## Motion vectors (Forward+)

See [Clustered Forward+](ForwardPlus.md#motion-vectors-and-jitter-item-75).

- The velocity is the UV of this frame minus the UV of the previous frame (y down), so `previousUv = uv − velocity`. It comes from both the camera and object motion: the previous clip position uses `GpuTransformRecord::Previous` and `PreviousViewProjection`, both unjittered.
- Jitter shifts only the rasterized position. It never appears in the velocity, and shading is unchanged.
- Only opaque surfaces write velocity. The background keeps the cleared 0, and transparent surfaces keep the velocity of what lies behind them.

## Jitter

`Temporal::JitterPixels(frame, phases)` is Halton(2, 3) at index `1 + frame % phases`, minus 0.5: offsets in (−0.5, 0.5) pixels that cover the pixel evenly. The default of 8 phases gives the usual 8× pattern. `JitterNdc` converts to NDC (×2/width, −2/height). `JitterPhases = 0` renders without jitter; history then only smooths motion.

A pixel whose centre is `c` sees the scene at `c − jitter` (in pixels, y down).

## Resolve (TemporalResolve.slang = Temporal::ResolveTexel)

For every texel:

1. **Neighborhood.** The 3×3 neighborhood of the current color, with clamped coordinates:
   - colors are sanitized: NaN and infinity become 0, and negatives are clamped to 0;
   - the mean, standard deviation, minimum and maximum are computed in YCoCg;
   - the velocity comes from the nearest texel, the largest reverse-Z depth; the first one in row order wins ties. Dilating the velocity this way keeps moving edges from reprojecting the background behind them.
2. **Reprojection.** `previousUv = uv − velocity`. If there is no history, or `previousUv` leaves [0, 1]², the output is the current color.
3. **History.** A clamp-to-edge bilinear sample of the last output, made of four `Load`s. The CPU uses the same weights exactly.
4. **Clipping.** The history is moved toward the centre of the box `[mean ± ClipGamma × σ] ∩ [min, max]` in YCoCg until it lies inside. Clipping, unlike clamping, keeps the history's hue. Negative results are clamped to 0.
5. **Blend.** The current color gets weight `Feedback / (1 + L)` and the history `(1 − Feedback) / (1 + L_history)`, with Rec. 709 luminance L. These weights keep bright, single-frame samples from dominating HDR edges. Alpha is 1.

| Setting | Default | Range | Effect |
| --- | --- | --- | --- |
| `Feedback` | 0.1 | (0, 1] | weight of the current frame; 1 disables accumulation |
| `ClipGamma` | 1.25 | [0.25, 8] | box half-size in standard deviations; smaller rejects more history (less ghosting, more flicker) |
| `JitterPhases` | 8 | 0 .. 64 | Halton sequence length; 0 disables jitter |

## History

- Two persistent RGBA16Float textures (`HistoryDesc`: Sampled, Storage, TransferSource) alternate as output and history. The output is imported undefined (the resolve writes every texel) or, once written, as `ShaderRead`, and exported `ShaderRead`.
- **Without history** (the first frame, `ResetHistory()`, or a resize), the resolve binds the current color in the history slot and a push constant tells it to ignore it. Nothing undefined is read.
- **Resize:** new textures are created, and the old pair is handed to the next graph, which retains them until its GPU work completes.
- **Camera cuts:** call `ResetHistory()`, and set `PreviousViewProjection` to the current matrix (or leave it empty) for that frame.

## Record contract

- **Color:** a sampled, single-sample 2D RGBA16Float texture: the jittered scene color.
- **Depth:** the same size and sampled, either D32Float (bound through a depth-aspect view) or R32Float. Reverse-Z.
- **Velocity:** the same size, sampled, RG16Float.
- **Settings:** checked by `ValidateTemporalSettings`.
- **Errors:** an invalid frame throws `std::invalid_argument` before anything is recorded or advanced.
- **Resources:** `TemporalGraphResources` names the output, the history that was read, the resolve pass, whether history was valid, and the jitter and frame index of this frame.

## Not yet

- Background velocity from camera motion (the sky keeps 0 and is only clipped), and velocity for transparent surfaces.
- Upsampling (TAAU), sharpening, and a reconstruction filter over the jittered samples.
- Per-object history rejection (stencil or reactive masks) for particles and animated textures.
- Motion blur, which will reuse the velocity target (item 76 and later).
- Engine wiring (item 56).

## Tests

| Suite | What it proves |
| --- | --- |
| `Render.Temporal.Reference` (6) | Halton values, the 8-phase jitter is centred and covers every quadrant, and the NDC sign convention. YCoCg round trips, clipping onto the box along the line to its centre, and clamp-to-edge bilinear sampling. Velocity dilation to the nearest texel with first-wins ties, and the variance box within min/max. A jittered static edge converges to a stable value, while the unjittered edge stays aliased. A moving square leaves no trail with dilated motion vectors, but ghosts without them and with a loose box. No history, off-screen reprojection and `Feedback = 1` give the current frame; sanitization; settings and size validation |
| `Render.TemporalAntiAliasing` (2) | On the mock device: one dispatch with the right groups, pipeline, push constants and bindings (a depth-aspect D32 view or plain R32Float); the color stands in as history on the first frame; the two textures ping-pong; the jitter advances; a reset and a resize restart history. Every rejected program, setting and input, with nothing advanced |
| `ShaderCompiler.TemporalLayout` (1) | `SwimTemporalResolve` reflects `TemporalResolveBindings`: four sampled textures, a storage output, 32 bytes of push constants and 8×8 groups |
| `ShaderCompiler.GpuAvBudget` | The resolve has no buffer accesses (limit 75); the Forward+ opaque program stays at 72 with motion vectors |
| `Render.ForwardPlus.Reference`, `Render.ForwardPlusRenderer`, `ShaderCompiler.ForwardPlusLayout` | Motion vectors, the velocity target and the 208-byte view record (see [Clustered Forward+](ForwardPlus.md#tests)) |
| Native `TemporalAntiAliasingMatchesTheCpuReference` | See below |
| Native `ClusteredForwardPlusMatchesTheCpuReference` | Now also compares every interior opaque pixel's motion vector, and renders a jittered frame |

### Native smoke

`TemporalAntiAliasingMatchesTheCpuReference` uploads synthetic 256×144 frames rendered with the TAA's own jitter: an HDR checkerboard (8-pixel squares, five stops of gradient, and a strip of negative values) panning 1.5 pixels per frame, with a bright rectangle moving diagonally in front of it. The frames carry exact motion vectors and reverse-Z depth.

Every output texel must equal `Temporal::ResolveTexel` over the same (binary16-rounded) inputs and the GPU's own previous output, within 3·10⁻³ relative + 10⁻⁴; at most 0.1 % of texels may differ. With history, more than 10 % of texels must visibly differ from the current frame, and without it none may.

Frames:

1. the first frame (no history);
2. six moving frames;
3. a reset, then one more frame;
4. a resize to 200×120 with R32Float depth, then one more frame;
5. two 1080p frames that print the resolve time.

A CPU dry run of the same scene found 18,689–23,116 of 36,864 texels taking history on the moving frames.
