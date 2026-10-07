# Reflections: planar, screen space, local probes, environment

Specular reflections come from four layers, each covering what the one before cannot see:

0. **Planar reflections** (`Engine/Systems/Renderer/Reflections/PlanarReflections*`, see below): sharp, current captures for the flat faces of mirrors (the mirror cube) and the camera-facing cap of chrome spheres, where SSR has nothing on screen to return.
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

## Planar reflections

A `PlanarReflector` component (`Engine/Components/PlanarReflector.h`: `Shape` Box/Plane/Sphere, `PlaneHalfExtents`, `Radius`, `Quality`, `Priority`) marks an object; `SceneRenderBridge` gathers them into `RenderFrameInput::PlanarReflectors`. In the sandbox, the mirror cube (Box, priority 2), every chrome sphere in the reflection lab and the six chrome toy spheres carry one.

**Planning** (`PlanarReflections::Planner`, CPU, allocation-free once warm):
- **Candidates.** Boxes contribute their six faces, planes one, spheres their camera-facing cap.
- **Culling.** Back-facing faces, faces outside the camera frustum and faces smaller than `MinScreenFraction` of the screen height are dropped before anything is rendered. Nothing else hands a mirror to SSR: a near mirror's planar reflection is never replaced by SSR.
- **SSR is the far LOD only.** A record's SSR preference (`PlanarReflections::SsrPreference`) is 0 while the reflector covers at least `SsrFallbackScreenFraction` (0.1) of the screen height. It rises smoothly to 1 at `MinScreenFraction`, where the reflector is culled. The composite lets a confident SSR hit replace the planar reflection only by that preference, scaled by the planar weight. The rim of a sphere cap therefore blends towards plain SSR and probes, while a near mirror shows only its capture. Before this, every record had a preference of 1 − texel density: 0.5 at the old 0.5 resolution scale, so SSR took half of every mirror and its artefacts showed through.
- **Sharing.** Faces within `PlaneAngleTolerance`/`PlaneDistanceTolerance` merge into one capture (coplanar mirrors, floor tiles): the reflected camera is offset to the nearest member and the lookup's distance refinement corrects the rest.
- **Capture view.** The camera mirrored in the plane looks along the plane normal through the reflector's visible portal (clipped to the camera frustum, grown by `PortalMargin`) with an off-axis, reverse-Z, infinite projection whose near plane is the mirror (+2 mm). Texel density on the mirror is uniform and no texel is spent outside it.
- **Sphere caps** capture from the centre towards the camera. The cap is as wide as `SphereMinCosine` (0.75) allows, within a capture half angle of `SphereMaxHalfAngle` (1.3 rad); a narrower cap is matched rather than letting rays run off the capture. The capture side is tan(half angle) × R for a sphere R pixels in radius (× `ResolutionScale`): at the cap's centre the reflected direction turns 2 / R per screen pixel, so one capture texel per screen pixel. The old size (0.5 × the sphere's pixel diameter) was about 2.6 times too coarse, which made sphere reflections blurry.
- **LOD.** Plane captures are sized from the portal's foreshortened screen footprint × `ResolutionScale` (1.0) × `Quality`, clamped to `MinResolution .. AtlasResolution` and quantised to 8.
- **Full rate, no reprojection.** Slots (atlas layers, `MaxPlanes`, at most `MaxPlanarReflections` = 12) follow reflector keys with LRU reuse. With the defaults (`MaxAgeSeconds` 0, `CapturesPerFrame` 12) every visible capture re-renders every frame, so planar reflections are never an older frame's image reprojected. The sandbox in 2026-10-06's first version re-rendered still captures only two a frame, and moving content only four. In the lab, nine reflectors competed for that, so captures were several frames old. The lookup's distance refinement then reprojected them across depth edges, which caused the doubled yellow ball and the warped seam. Where the reprojected window no longer covered the face, the mirror's stale object probe showed through, including a sphere that had left seconds earlier. Within the budget, changed content sorts first: new captures, moved reflectors, portals no longer covered, a mover (within `MoverRange`) in the old or new view, or a mover that was there at the last capture (one more capture after it leaves). Then come camera motion beyond `MotionTolerance` (0.25 texels), size changes beyond 20 % and age. Setting `MaxAgeSeconds` above 0 re-enables reuse of still captures when the cost matters more than full rate.

**Rendering** (`PlanarReflectionRenderer`): captures render through the ordinary scene path with their own GPU visibility (frustum = the capture, far plane = `CullDistance`, the owner excluded by `ExcludedObjectId`) at coarse LODs. Each capture takes the camera's TAA jitter as the same sub-texel offset of its own projection. Since captures re-render every frame, TAA anti-aliases the reflection at no extra cost. `Supersample` (default 1; 2..4 renders larger and the resolve averages each block, luminance-weighted, keeping the nearest distance) remains for when TAA is off. `PlanarReflectionResolve.slang` writes the persistent RGBA16F 2D-array atlas.

**Reflections inside captures** (`Reflections/CaptureShading.slang`, in both the planar and the probe resolve): a capture is an ordinary Forward+ render, so reflective objects in it carried only the environment's specular IBL. A chrome ball or mirror seen in a mirror, most visibly from its far side, looked like a flat piece of sky. Reflective capture texels (reflectance luminance > 0.02) now replace their specular IBL with their reflection probe: their object probe, else the area probe covering them, direction only. Planar captures use this frame's probe records; probe faces use the records as they stood before this frame's filters (the source atlas is written, the prefiltered one read). Facing mirrors converge over a few frames.

**Shading** (`ScreenSpaceComposite.slang`, `SamplePlanar`; CPU: `PlanarReflections::LookupUv`): a pixel on a record's plane (within the tolerance, normal cosine, owner and roughness fade) projects its mirror ray into the capture, then refines the hit twice with the stored distances. That corrects camera motion since the capture and sphere curvature. SSR goes over it only by the SSR preference (above); probes and the environment fill whatever remains. The Sources debug view shows planar pixels in yellow.

**Controls.**
- Settings: `RenderSettings::PlanarReflections` (`PlanarReflectionSettings`).
- Console: `render.set planar.planes|atlas|captures|supersample|scale|min-screen|ssr-fallback|sphere-cos|motion|max-age|cull`.
- Toggles: the `reflections.planar` toggle, `sandbox.planar 0|1` and the HUD checkbox.
- Stats: `PlanarReflections` (records), `PlanarCaptures` (re-renders this frame), `PlanarCandidates`.

## Ghosting fixes (2026-10-06)

Reflected objects lagged behind, doubled or tripled, and objects that had left could still be seen in reflections. Each layer had its own cause:
- **Planar captures** re-render every frame (above). The temporal reflection filter leaves the planar share of a pixel alone (the composite writes the term's weight × (1 − planar share)). Reprojecting a mirror's image by the mirror's own motion dragged the old image along: a double image while the camera moved.
- **Probe faces** are re-captured once after a moving object leaves their view (`ReflectionProbes::Scheduler` keeps `FaceSawMover` per face). Faces whose content changes (a mover in view or just left, a moving probe such as the orbiting ball's, a face never captured) are ranked by how long they waited, not by the probe's distance rank. They go round robin, oldest first, so a far probe is never starved by a near one. With `FacesPerFrame` at least half of them (the sandbox: 16 faces of 128 x 128, up to `MaxFacesPerFrame` = 24), every one updates at least every second frame. Every probe captured in a frame is also prefiltered in that frame (`FiltersPerFrame` only bounds the backlog).
- **Screen-space reflections** at "half resolution" are now a checkerboard ([ScreenSpace.md](ScreenSpace.md#half-resolution-phase-23-performance-work)): every pixel is traced every second frame instead of every fourth.
- **SSR's reflections of reflections** (`ReflectionSettings::History`) read the previous finished frame only at reflective hits, by `ScreenSpace::HistoryShare`: smoothstep(0.1, 0.5) of the hit's reflectance luminance. `ScreenSpaceReflectionBindings::Reflectance` (binding 10) carries the reflectance. Diffuse hits use the current colour, which is exact. Their frame-old image showed whatever had just moved away from them.
- **The temporal reflection filter** clips last frame's term in YCoCg towards the 3×3 neighbourhood mean, into the box of ±1 standard deviation (variance clipping, within the neighbourhood's own range). The old clamp was the neighbourhood's min/max, which let old reflections through wherever the neighbourhood had contrast. The further the history had to be clipped, or the faster the surface moves on screen (12 px per frame drops it), the more this frame's term counts.

## Debug views

`ReflectionSettings::Debug` (`sandbox.reflectdebug`, or the Rendering tab):
- **Sources:** planar is yellow, SSR is red, rejected SSR (hidden side) magenta, probes green, the environment blue. Non-reflective pixels are dimmed grey.
- **Probe age:** a hue per probe slot × its coverage, darkened as its oldest face ages (0 → 4 s).

## Cost (RTX 4070, 1080p, the reflection lab)

GPU 12.1 ms with 15 active probes and 3 faces captured per frame, against 11.9 ms with probes and back faces off (the frame-to-frame noise is about ±0.3 ms). `render.stats 120` prints the per-pass times. The back-face pass is one depth-only draw of the visible scene.

## Tests

- `Render.PlanarReflections` (5): lookup against the analytic mirror ray and reprojection after camera motion; culling (back-facing, tiny) and the far-LOD SSR preference (0 for near mirrors, even grazing); coplanar sharing and footprint sizing; capture reuse until motion, movers (and once after a mover left) or age, and the dynamic budget; sphere caps, their sizing and supersampled render sizes.
- `Render.ScreenSpace.Reference.OnlyReflectiveHitsReadTheHistory`.
- `Render.ReflectionProbes.FacesThatSeeMovingObjectsAreRecapturedFirst` (with the clean-up capture after the mover left).
- `Render.ReflectionProbes` (5): faces that see movers go first (frustum test, range, the owner ignored); face bases, views and projections; capture distances; selection and blending; parallax against a wall and a sphere; an occluder in front of a far wall; a ray passing behind a near object; jitter independence; scheduler budgets, slots, moved and dynamic probes.
- `Render.ScreenSpace.HitValidation` (3).
- `Render.ScreenSpaceEffects.BackFaceDepthAndProbesReachTheirPasses`.
- `Render.Visibility.CaptureViewsCullTheirExcludedObject`.
- `Game.Sandbox.TheReflectionLabHasProbesAndADynamicFloor`.
- The validated `RHI.Vulkan.Smoke.ScreenSpace` frame with back faces.
