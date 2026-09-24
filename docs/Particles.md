# GPU particles

This covers critical-path item **77**: GPU particles. `Renderer/Particles` simulates, sorts and draws particles entirely on the GPU. No particle ever exists on the CPU; the CPU only schedules emission and packs one small record per emitter per frame.

```text
Renderer/Particles
  ParticleSettings          ParticleEmitterDesc (emission, shape, motion, collision, curves, flipbook) + validation
  ParticleRecords           GpuParticle (48 B), GpuParticleEmitter (320 B), GpuParticleCounters (16 B), GpuParticleFrame (128 B)
  ParticleBindings          descriptor contracts of the five programs
  ParticleReference         PackParticleEmitter, BuildParticleFrame and the CPU definition of every pass (Particles::)
  ParticleSystem            emitters, the persistent pool, Simulate (4 compute passes) and Draw (1 graphics pass)
  ParticleGraphResources    what one Simulate scheduled
Shaders/Slang/Particles
  ParticleRecords.slang     mirrors the records and the shared helpers (hash, curves, world position)
  ParticleSimulate.slang    SwimParticleSimulate
  ParticleEmit.slang        SwimParticleEmit
  ParticleCompact.slang     SwimParticleCompact
  ParticleFinalize.slang    SwimParticleFinalize (bitonic sort + indirect arguments)
  ParticleRender.slang      SwimParticleRender (vertexMain + fragmentMain)
```

`Renderer/Particles` depends only on RenderGraph, the RHI contract and the generational handles of `Renderer/Resources`. No other renderer module includes it. `scripts/verify-build-layout.py` enforces this.

## The pool

`ParticleSystem` owns five persistent device-local buffers:

| Buffer | Contents |
| --- | --- |
| Pool | `GpuParticle` per slot: position, age, velocity, lifetime (0 = free), size, rotation, angular velocity, id |
| Free list | `uint` per slot: each emitter's free slots, a stack within its range |
| Draw list | `uint` per slot: each emitter's live slots after the compact pass, in draw order after the finalize pass |
| Counters | `GpuParticleCounters` per emitter row: free-list size, live count, dropped spawns (cumulative) |
| Draw arguments | `DrawIndexedIndirectCommand` per emitter row |

- **Emitters** are generational handles (`GpuResourceRegistry`). Each owns a contiguous range of `Capacity` slots, allocated first fit; its row index names its counters and draw arguments.
- **Release** invalidates the handle at once. The range and row are reused only after the supplied timeline point completes (`Collect`), so in-flight frames never see a new emitter's data in an old one's slots. Freed neighbouring ranges coalesce.
- **A new range** is reset by the first `Simulate` after creation: free slots, a full free list and fresh counters, recorded as graph uploads. `AbortFrame` redoes it.

## A frame

```text
ParticleSystem::Simulate(graph, view, dt)          CPU: advance emission clocks, pack one record per live emitter
  1. simulate   every occupied slot: age, integrate, collide; dead slots return to the free list
  2. emit       this frame's spawns pop free slots and initialize (Particles::SpawnParticle)
  3. compact    every live slot appends itself to its emitter's draw list
  4. finalize   one group per emitter: sort alpha-blended draw lists back to front; write the indirect arguments
ParticleSystem::Draw(graph, frame, program, { Color, Depth }, bindless)
  additive emitters, then alpha-blended ones back to front by origin: one DrawIndexedIndirect each
```

- Compute passes 1–3 dispatch 64-thread groups along x over the largest range (or spawn count) and one group row per emitter (y). The finalize pass runs one 256-thread group per emitter.
- Spawns are popped after the simulation step, so a slot freed this frame can be reused this frame, and new particles start at age 0 without a step.
- A spawn that finds the free list empty is dropped and counted in `Dropped`. Which spawns are dropped is then unspecified (slot assignment is concurrent); `AdvanceEmission` already caps one frame's spawns at the capacity.
- Nothing is recorded without live emitters.
- After `Execute`, call `CommitFrame`, or `AbortFrame` to rewind the emission clocks.

## Emission (CPU)

`Particles::AdvanceEmission` is the whole CPU-side schedule, per emitter and frame of `dt` seconds over `[Time, Time + dt)`:

- **Rate:** `Rate × dt` accumulates; whole particles spawn and the fraction carries. Without looping, the rate stops at `Duration`.
- **Bursts:** each burst fires at `Time_b + k × Duration` (every cycle when looping, once otherwise), including several cycles in one long frame.
- **Ids:** spawns get consecutive ids from `NextId`. Every random draw of a particle is a pure function of its id and the emitter's `Seed`, which is what lets the GPU and the CPU produce the same particle whatever slot it lands in.
- `SetEmitting(false)` keeps the clock running without spawning, so bursts do not pile up.

## Spawn and simulation

`Particles::SpawnParticle` (ParticleEmit.slang) draws uniform values from independent PCG-hashed streams:

- lifetime, speed, size, rotation and angular velocity in their `[Min, Max]` ranges;
- direction uniform in the cone of half angle `ConeAngle` around `Direction` (cos θ uniform in [cos ConeAngle, 1], Duff et al. orthonormal basis);
- position uniform in the shape: a point, a ball of radius `ShapeExtent.x`, or a box of half extents `ShapeExtent`;
- world-space emitters transform position and velocity by the emitter transform at spawn; local-space ones keep them in the emitter's space and are drawn through its current transform, so they move with it.

`Particles::SimulateParticle` (ParticleSimulate.slang), per step:

1. `age + dt ≥ lifetime` → the particle dies (the slot is freed, nothing else changes);
2. `v = (v + g dt) × max(0, 1 − drag dt)`; `p += v dt`; `rotation += ω dt`; `age += dt`;
3. with collision (world space only), below `GroundHeight` the particle is put on the plane and, if moving down, bounces with `Restitution` and loses `Friction` of its tangential speed.

## Drawing

`SwimParticleRender` draws six indices (two triangles) per instance; the instance id indexes the emitter's draw list. `Particles::BillboardCorner` places each corner:

- camera-facing: the camera right and up axes, rotated by the particle's rotation;
- size = spawn size × `SizeOverLife` at the normalized age (a piecewise-linear curve of up to four keys);
- the sprite UV inside the current flipbook frame: `Columns × Rows` cells, row-major from the top left; `FrameRate > 0` plays frames per second (wrapping), 0 spreads the frames over the lifetime.

`Particles::ShadeFragment` multiplies `ColorOverLife` (a four-key RGBA gradient, straight alpha) by the sprite texel from the bindless table (`TextureIndex`, `SamplerIndex`), or, untextured, by a procedural soft disc `1 − |2uv − 1|²`, and outputs premultiplied `(rgb × a, a)`.

| `ParticleSystem::PipelineDesc` | Additive | AlphaBlend |
| --- | --- | --- |
| Color | RGBA16Float, One / One, destination alpha kept | RGBA16Float, One / OneMinusSourceAlpha |
| Depth | D32Float, GreaterEqual (reverse-Z), no write | same |
| Cull | none | none |
| Sorting | none (order independent) | per emitter, back to front by view depth, then id |

The render layout's space 1 is the bindless table's space (`ForwardPlusBindlessSpace` has the same schema), so one `BindlessResourceTable` serves Forward+ and particles.

### Transparency policy

- **Within an emitter:** additive particles need no order. Alpha-blended emitters sort their draw list in one workgroup by `Particles::DrawsBefore` (larger view depth first, then lower id, so ties are deterministic); their capacity is therefore limited to `MaxSortedParticles` (2,048).
- **Between emitters:** additive emitters draw first, then alpha-blended ones back to front by emitter origin. Intersecting blended emitters are not sorted into each other.
- Particles test against the scene depth but never write it, so they draw after opaque geometry. Drawing them into Forward+'s color target before screen-space effects and TAA is the intended placement.

## Settings

| `ParticleEmitterDesc` | Default | Range |
| --- | --- | --- |
| `Capacity` | 1,024 | ≥ 1; ≤ 2,048 when alpha blended |
| `Space` / `Blend` | World / Additive | |
| `Rate`, `Duration`, `Looping`, `Bursts` | 50/s, 0 (forever), true, none | rate ≥ 0; bursts in [0, Duration) |
| `Shape`, `ShapeExtent` | Point, 0 | extents ≥ 0 |
| `Direction`, `ConeAngle` | +Y, 0.5 rad | non-zero; [0, π] |
| `Speed`, `Lifetime`, `Size` ranges | 1–2 m/s, 1–2 s, 0.1–0.2 m | speed ≥ 0; lifetime and size > 0; min ≤ max |
| `Rotation`, `AngularVelocity` ranges | 0 | finite, min ≤ max |
| `Gravity`, `Drag` | (0, −9.81, 0), 0 | drag ≥ 0 |
| `Collision`, `GroundHeight`, `Restitution`, `Friction` | off, 0, 0.5, 0.1 | world space only; [0, 1] |
| `SizeOverLife`, `ColorOverLife` | constant 1, white | 1–4 keys, strictly increasing times in [0, 1]; alpha in [0, 1] |
| `TextureIndex`, `SamplerIndex` | 0, 0 (procedural disc) | both or neither |
| `FlipbookColumns`, `FlipbookRows`, `FlipbookFrames`, `FlipbookFrameRate` | 1, 1, 1, 0 | frames ≤ columns × rows; rate ≥ 0 |
| `Seed` | 0 | |

`ParticleSystemDesc`: `Capacity` pool slots (default 65,536, at most 2²⁴), `MaxEmitters` (default 64, at most 65,535) and the four compute programs.

## Not yet

- Mesh and trail (ribbon) rendering; soft particles (depth fade against the scene).
- Depth-buffer collisions and colliders beyond one ground plane; forces fields.
- Spawning from GPU events (sub-emitters) and from GPU Scene meshes.
- Emitter assets and an ECS component (the CPU API exists; wiring is item 56's engine integration).
- Lighting particles with the clustered lights and shadows.
- Sorting across emitters, and alpha-blended emitters above 2,048 particles (a multi-pass global sort).

## Tests

| Suite | What it proves |
| --- | --- |
| `Render.Particles.Reference` (8) | **Random:** deterministic, uniform (10 buckets within 5 %), independent streams, the published PCG hash. **Emission:** rate carry, a non-looping duration, bursts once and every cycle (also several in one frame), the capacity cap. **Spawn:** every range; the cone is filled and never left; balls are uniform in volume; boxes stay inside; world space goes through the transform and local space does not. **Simulation:** semi-implicit Euler exactly; death leaves the particle unchanged; drag; bounces lose restitution and friction, each lower than the last. **Curves, gradients, flipbooks** over life and by frame rate. **Billboards** face the camera, have the right size and rotation, and map the flipbook cell. **Fragments:** premultiplied, disc falloff, texture multiply. **Pool:** a fountain reaches its steady state (rate × mean lifetime) and a small pool drops what does not fit. **Packing and validation** of every setting. **CPU rasterizer:** additive then sorted alpha blending, and the depth test |
| `Render.ParticleSystem` (5) | On the mock device: four dispatches in order with their group counts; range resets written once (the free lists, fresh counters and zeroed slots land in the buffers); ids continue; abort rewinds clocks and redoes resets; draws are additive first then blended back to front, rebinding pipelines only on a blend change, with the emitter's record index as the push constant and its row's argument offset; range allocation, release, retirement, reuse and coalescing; emitter-row limits; transforms and emission toggles; no emitters records nothing; every rejected program, desc, view and target; the pipeline states |
| `ShaderCompiler.ParticleLayout` (1) | Each compute program reflects exactly its binding subset; every record equals its C++ mirror; group sizes; the render program's bindings, one 4-byte push constant and the runtime-sized bindless space |
| `ShaderCompiler.GpuAvBudget` | Simulate 29, emit 41, compact 6, finalize 16 and render 24 instrumented buffer accesses (limit 75) |
| Native `GpuParticlesMatchTheCpuReference` | See below |

### Native smoke

`GpuParticlesMatchTheCpuReference` runs a bouncing world-space fountain, a drifting local-space alpha-blended smoke and textured flipbook bursts (a 4 × 2 atlas through the bindless table) for 30 frames at 30 Hz. Every frame it reads back the whole pool, the counters, the draw lists and the indirect arguments:

- **Particles:** each emitter's live particles, matched by id, against `Particles::` stepped from the GPU's own previous state: at most 0.1 % id mismatches and 0.5 % field outliers (10⁻⁴ relative on positions and velocities).
- **Bookkeeping:** live count = the GPU's live slots; free + live = capacity; no drops; arguments `(6, live, 0, 0, 0)`; every live slot drawn exactly once.
- **Sorting:** the alpha-blended draw list is back to front by the particles' own view depths (no inversions).
- **Image:** the last frame is drawn over a cleared depth plane 7 m away (some particles are behind it) and compared pixel by pixel with the CPU rasterizer over the GPU's particles (bilinear atlas sampling): at most 0.5 % outliers (0.01 + 1 %), skipping pixels whose centre lies within 0.02 px of a billboard edge.
- **Timing:** a 60,000-particle emitter prints the pass times.
