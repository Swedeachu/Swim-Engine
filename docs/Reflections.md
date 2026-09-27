# Reflections: screen space, local probes, environment

Specular reflections come from three layers, each covering what the one before cannot see:

1. **Screen-space reflections** ([ScreenSpace.md](ScreenSpace.md#reflections-screenspacereflectionslang--screenspacereflectiontexel)): exact, sharp and dynamic, but they can only return what the camera sees.
2. **Local reflection probes** (`Engine/Systems/Renderer/Reflections`): cube captures of the scene around a point, parallax-corrected with their stored distances. They cover what is off screen, behind the camera or hidden.
3. **The global environment** (the procedural sky with the volumetric clouds folded in, [Environment.md](Environment.md)): everything else.

In `ScreenSpaceComposite.slang` (CPU: `ScreenSpace::CompositeTexel`), the fallback is F = specular + coverage × (reflectance × probe − specular). Coverage is how much of the environment the probes replace. An SSR hit then replaces F by its confidence × AO. Nothing is a flat colour: the sandy blur under chrome spheres, the dark blobs between touching spheres and the floor-coloured "ground" of the old fallback are all gone.

## Screen-space reflection fixes

- **Surface thickness from back faces.** A depth-only Forward+ pass (`ForwardBackDepth`: `FORWARD_DEPTH_ONLY` + `FORWARD_BACK_DEPTH`, it discards front faces) gives each pixel the depth of the farthest back face. The thickness of what the ray passed is then back − front, at least `BackFaceMinThickness` (2 cm). Pixels with no back face (open surfaces, the floor) count as infinitely thick. With the constant `Thickness`, a ray passing *behind* a sphere hit the sphere, and one passing *under* a sphere 5 mm above the floor could not reach the floor.
- **Hidden-side rejection.** A crossing is accepted only if the sample before it was in front of the same surface (`SilhouetteTolerance`, 3 % of depth). Otherwise the ray entered through a side the camera cannot see and would return the wrong texel: it reports `RejectedHit` and the probes take over. Hits on back-facing normals are rejected the same way.
- **Tests** (`Render.ScreenSpace.HitValidation`): the traced hits are compared with analytic ray casts of sphere scenes. Wrong hits fell from 123 to 26 (mirror cube) and from 9 to 5 (touching spheres), while 93–95 % of the visible hits are still found.

`ReflectionSettings::BackFaces` (on) turns the thickness pass on. `sandbox.ssrbackfaces 0` compares with the old behaviour.

## Probes

`Engine::ReflectionProbe` (component):

| Field | Meaning |
| --- | --- |
| `ObjectProbe` | Captured from the entity's centre, without the entity (`GpuViewRecord::ExcludedObjectId`, culled in `GpuVisibility`). Used only by the entity's own pixels, matched through the ObjectId target. This is how a chrome ball reflects its surroundings but not itself. |
| Area probe | Serves every pixel within `InfluenceRadius`, weighted by `saturate((radius − d) / BlendDistance)`. The two strongest blend by weight / (d / radius + 0.05). |
| `Dynamic` | Keeps re-capturing its oldest faces. Static probes capture once, and again when moved beyond `MoveThreshold`. |
| `Priority`, `CaptureNear`, `Offset` | Scheduling weight, capture near plane, capture point offset. |

`SceneRenderBridge::GetReflectionProbes` gathers them. `RenderSettings::ReflectionProbes` (`Resolution` 128 by default and 256 in the sandbox, so flat mirrors stay sharp; `MaxProbes` ≤ 16, `FacesPerFrame`, `FiltersPerFrame`, `PrefilterSamples`) bounds the cost.

**Scheduling** (`ReflectionProbes::Scheduler`): each probe gets a slot of a cube-array atlas, by priority / (1 + camera distance). Faces are ranked by urgency:
1. faces never captured;
2. faces of dynamic probes that *see something that moved this frame*: `RenderFrameInput::ReflectionMovers`, which `SceneRenderBridge` gathers as bounding spheres of mesh entities whose world position changed; a mover counts within `MoverRange` (15 m) and inside the face's 90° frustum (`FaceSees`);
3. faces of probes that moved;
4. the stalest faces of dynamic probes.

Only `FacesPerFrame` faces are captured each frame (the sandbox uses 6). A ball rolling past a chrome sphere therefore refreshes the face that sees it every frame or two, instead of waiting for the round-robin, which covers 16 probes × 6 faces. A probe is used once all six faces exist. There are no 6-face bursts, no readbacks and no device idle.

**Capture:** each face is an ordinary frame of the Forward+ pipeline (sky, GPU visibility with its own instance, clusters, forward) at the probe resolution with a 90° reverse-Z projection, without transparent objects (no sort per face). Its passes are named "Probe Forward+ …" and "Probe sky" in the GPU timings. Captures are lit by the global environment, not by other probes, so there is no recursion. `ReflectionProbeResolve.slang` writes each face into the source atlas:
- rgb: the radiance; sky texels take the global environment's mip 0, which has the clouds (× `EnvironmentIntensity`);
- alpha: the distance to what the texel sees (`near / depth × √(1 + ndc²)`, `DistanceSky` for the sky).

`EnvironmentPrefilter` with `PREFILTER_KEEP_ALPHA` then builds the GGX mips that shading samples by roughness. Prefiltering is 6 faces × every mip, so at most `FiltersPerFrame` probes (2) are filtered per frame. Probes not yet filtered for their current occupant go first, and a probe is used only once it has been filtered.

**Parallax** (`ReflectionProbes::ParallaxDirection`, mirrored in the composite):
1. The reflected ray is marched in the probe's space with texel-adaptive steps, up to 96 and up to 40 m. Each step turns the sample's direction, as seen from the probe, by about 3 probe texels (at least 0.02 rad). Steps are short where the ray passes close to the probe and long where it heads away from it.
2. It looks for the first step from in front of to beyond the captured distance, and places it with 5 bisections.
3. The crossing counts only if the refined point lies on the captured surface (`ParallaxThickness`: 8 % of the distance + twice the final interval). A ray passing behind a nearby object, as the probe sees it, also goes "beyond", but at the object's silhouette, far behind its surface. It marches on instead of stopping there. That false stop was the step-banded smear beside the mirror cube.
4. Nothing is jittered. Earlier fixed steps left stair steps, and per-pixel jitter left a hatch pattern TAA did not remove. Flat mirrors (the mirror cube) show the probe almost 1:1, so both showed up there.
5. A ray that crosses nothing within reach keeps its own direction (the sky).

## Debug views

`ReflectionSettings::Debug` (`sandbox.reflectdebug`, or the Rendering tab):
- **Sources:** SSR is red, rejected SSR (hidden side) magenta, probes green, the environment blue. Non-reflective pixels are dimmed grey.
- **Probe age:** a hue per probe slot × its coverage, darkened as its oldest face ages (0 → 4 s).

## Cost (RTX 4070, 1080p, the reflection lab)

GPU 12.1 ms with 15 active probes and 3 faces captured per frame, against 11.9 ms with probes and back faces off (the frame-to-frame noise is about ±0.3 ms). `render.stats 120` prints the per-pass times. The back-face pass is one depth-only draw of the visible scene.

## Tests

- `Render.ReflectionProbes` (5): faces that see movers go first (frustum test, range, the owner ignored); face bases, views and projections; capture distances; selection and blending; parallax against a wall and a sphere; an occluder in front of a far wall; a ray passing behind a near object; jitter independence; scheduler budgets, slots, moved and dynamic probes.
- `Render.ScreenSpace.HitValidation` (3).
- `Render.ScreenSpaceEffects.BackFaceDepthAndProbesReachTheirPasses`.
- `Render.Visibility.CaptureViewsCullTheirExcludedObject`.
- `Game.Sandbox.TheReflectionLabHasProbesAndADynamicFloor`.
- The validated `RHI.Vulkan.Smoke.ScreenSpace` frame with back faces.
