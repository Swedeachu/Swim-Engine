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

**Rendering** (`PlanarReflectionRenderer`): captures render through the ordinary scene path with their own GPU visibility (frustum = the capture, far plane = `CullDistance`, the owner excluded by `ExcludedObjectId`) at coarse LODs. Captures are not jittered (see "Moving objects and distant detail" below); the Catmull-Rom fetch smooths their texels, and `Supersample` (default 1; 2..4 renders larger and the resolve averages each block, luminance-weighted, keeping the nearest distance) anti-aliases them when the cost is acceptable. `PlanarReflectionResolve.slang` writes the persistent RGBA16F 2D-array atlas.

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

## Stability and cost (2026-10-06, late)

Found by rendering the reflection lab frame by frame on SwiftShader (`capture.sequence`, [EngineRuntime.md](EngineRuntime.md#running)):
- **Probes kept their first, empty capture.** The orbiting objects keep about 25 probe faces changing every frame. Those "changed" faces ranked above every still face, so still faces were never refreshed. Faces first captured before the scene's geometry was resident kept showing only sky and the environment's ground, for good: the grey, "foggy" lower half of the chrome spheres. Now a quarter of `FacesPerFrame` (from four faces a frame) is kept for still faces, oldest first. Faces never captured go before everything. Movers are also only the dirty mesh entities whose world transform actually changed (more than 1e-4): physics rewriting a resting body's transform every step no longer counts as motion (`SceneRenderBridge`).
- **SSR shimmered on the spheres' lower rims.** A sphere's rim minifies what it reflects, so one ray per pixel samples the floor's checkerboard sparsely and the jitter moves the samples. `ReflectionSettings::MaxFootprint` (3 px; `ScreenSpace::ReflectionFootprint`, `FootprintFade`) estimates how many pixels at the hit one pixel's reflection covers: the view ray's spread plus twice the normal's turn per pixel (`NormalTurnPerPixel`). Hits fade out from 3 to 6 px, and the prefiltered probe takes over there at a mip one short of that footprint.
- **Slight ghosting at the spheres' edges.** The temporal reflection filter now filters only the screen-space share of a pixel, and not on curved surfaces (normal turn × 50). Planar reflections and probes are current and stable on their own, and reprojecting them by the surface's motion dragged moving reflections along.
- **Cheaper captures.** Probe faces and planar captures draw one geometry pass (`ForwardPlusFrame::DepthPrepass` = false): their targets are small, so the depth prepass saved little shading and doubled the geometry. Probe faces cull geometry beyond `ReflectionProbeSettings::CullDistance` along the face (100 m; the sandbox 60 m, which keeps Sponza out of the lab's probes). A probe face's cost is its geometry, not its 128 x 128 pixels. Planar captures use `CullDistance` (the sandbox: 80 m). In the lab on SwiftShader, the frame's GPU time fell from 11.8 s to 7.6 s against the previous settings; the numbers are only relative, since SwiftShader is a CPU rasterizer.
- **Smooth, cheaper planar captures.** The composite reads planar captures through a 9-tap Catmull-Rom filter (`PlanarFetchSmooth`). Captures can then sit well below screen resolution (the sandbox: `ResolutionScale` 0.5, a 512 atlas; 0.75 and 0.5 were indistinguishable in side-by-side captures, 0.4 slightly softer) and still magnify smoothly, without blocks or the bilinear blur.

## Moving objects and distant detail (2026-10-06, night)

Found frame by frame on SwiftShader, each layer isolated (`sandbox.probes 0`, `sandbox.ssr 0`, `sandbox.planar 0`, `sandbox.labfloor`, and a main camera placed at a capture's own eye):
- **Duplicated or sectioned moving objects in chrome balls.** On a chrome ball only the cap facing the camera is planar; the wide ring around it is the ball's object probe (`sandbox.reflectdebug 1`). The scheduler captured a probe's changed faces round robin one by one, so two faces of one probe could hold a moving object at two different times: across a cube seam the orbiting block showed twice, or cut in sections, and the orbiting ball's own probe stitched faces captured from different positions. `ReflectionProbes::Scheduler` now captures all changed faces of a probe in the same frame. A probe whose changed faces do not fit what is left of the frame's budget waits as a whole (the round robin brings it to the front next frame); only a group larger than the whole budget for changed faces is split, so `FacesPerFrame` stays a bound.
- **Bright slices through nearby moving objects.** The probe's parallax march (`SampleProbe`, `ReflectionProbes::ParallaxDirection`) could not tell a ray passing behind an object, as the probe sees it, from a ray hitting a side of the object that the probe cannot see. Both go "beyond" at the object's silhouette and march on; with only sky behind, the march ended on the ray's own direction, so a sky-coloured band cut through the orbiting block wherever the ball's surface saw a side its centre did not. Now the first silhouette passed within `ParallaxHidden` (0.35 × its distance + 0.1 m) behind the object's surface is kept, and used when nothing real is crossed further on: a disocclusion is filled with the occluder, not the sky. A ray that does cross something behind (the ceiling, a far wall) still takes that.
- **Distant objects wobbled in planar reflections.** Captures took the camera's TAA jitter as a sub-texel offset of their own projection. Once a curved mirror magnifies a capture texel over several screen pixels, that offset became a visible wobble of small, distant reflected objects, and TAA cannot remove it (its motion vectors follow the mirror, not the reflection). It also made a boundary in the reflection (the lab's floor pad against the ground beyond it) jump by whole patches from frame to frame. Captures are no longer jittered: identical frames now give identical captures.
- **LODs in captures.** Planar captures and probe faces each pick their LODs from their own view (`GpuViewFlags::ResetLodHistory`, as shadow cascades already did). One LOD history served every capture of the frame, so each capture's hysteresis ran against whatever the previous capture had chosen.
- **SSR's reflections of reflections** read the previous frame's screen-space output (before TAA) instead of TAA's accumulated history, which carried TAA's own trail of moving objects into the reflections (`Reflection colour history` pass in `FrameRenderer`).

## Curved mirrors: one stable source per pixel, reactive TAA (2026-10-07)

The chrome balls still ghosted on their curved outer parts and fuzzed at the bottom. Frame by frame (with TAA off, too):
- **The fuzz at the bottom was SSR.** On a ball, the band below the planar cap took screen-space reflections of the floor. Checkerboard SSR traces half the pixels each frame and fills in the rest, and on a curved surface neighbouring pixels reflect very different things, so that band changed every frame even with TAA off. Objects with a reflection probe of their own (the chrome balls, the mirror cube) now take no SSR at all (`HasObjectProbe` in the composite): their planar cap and their probe see everything around them. The probe is prefiltered by the pixel's footprint, so it is stable without TAA.
- **The ghosts and old copies were TAA.** A still ball has zero motion vectors, so TAA reprojected last frame's reflection onto the same pixel, and on a busy rim the variance box let the moved block's old image through. Following FSR2's reactive mask, the composite marks mirror-like pixels as reactive: reflectance luminance smoothstep(0.25, 0.6) × (1 − smoothstep(0.05, 0.2) of roughness) × the share SSR leaves. It writes 2 + reactivity into the colour's alpha (Forward+ writes 1; `Temporal::Reactive` reads it back, and post-processing clamps the alpha). Reactive pixels weigh the current frame by `TemporalSettings::ReactiveFeedback` (0.35 instead of 0.1) and clip history to `ReactiveClipGamma` (0.75 standard deviations instead of 1.25).
- **Planar sphere caps are prefiltered too.** Towards the cap's rim the capture is minified, so the composite measures the pixel's footprint in capture texels (the reflected direction turned by the pixel's spread). From `ReflectionBlurDelay` (3) texels it blends from the Catmull-Rom tap to a 4-tap box of that footprint, fully at twice that (`PlanarMaxFootprint` 8).
- **Blur starts later (2026-10-07, later).** Both prefilters start at three times the footprint (`ReflectionBlurDelay`): the outer parts of the balls blurred too soon. The probe's mip is chosen from the footprint divided by 3, and is still one mip short of that.
- **What stays:** the probe mip is one short of the footprint. A fully matched mip made the balls grey and foggy and showed the cube faces' seams, so a little shimmer is left for TAA (now with less history) to average.

## One reflection per chrome ball (2026-10-07, afternoon)

A ball showed two reflections at once: the planar cap in its middle (re-rendered every frame) and its object probe around it (changed faces round robin, every two or three frames). Where they met, the passing orange block appeared twice at two times, and in the probe ring it moved in steps. Real-time reflection probes elsewhere make the same trade-off explicit: Unity's time slicing ("individual faces", "all faces at once") lags behind the scene, and only an update without slicing keeps a probe in sync with what moves around it. A cube captured at an object's centre matches what its surface reflects once it is parallax-corrected with the captured distances (Lagarde and Zanuttini, "Local image-based lighting with parallax-corrected cubemaps", 2012), which `SampleProbe` already does with its distance march.

So in the sandbox a chrome ball's own probe is its whole reflection:
- **No sphere caps.** The lab's chrome balls and the playground's toy spheres no longer have a `PlanarReflector`. Planar reflections remain for flat mirrors (the mirror cube), where they are exact. The engine still supports sphere caps for scenes without object probes.
- **No time slicing for what moves.** All of a probe's changed faces are captured in the same frame (since 2026-10-06), and the frame's budget is spent by on-screen size: a changed face's wait counts × (0.5 + 0.5 × its probe's rank against the best one). Balls up close refresh every frame, the far ones at no less than about half their rate. Still faces keep an eighth of the budget (at least one face from four).
- **Sharp enough to replace the cap.** 256 × 256 faces (was 128): at a ball's centre the reflected direction turns 2 / R per pixel, so 256 texels over 90 degrees match a ball a few hundred pixels across. 32 faces a frame (was 16; `MaxFacesPerFrame` is now 32), so that the lab's changing faces fit in one frame. At 24, a simulation of the lab (`TheLabsBallsRefreshWhatMovesEveryFrame`) showed the pair and the smooth ball skipping every other frame when demand peaked at the budget. The sandbox's `MoveThreshold` is 1 mm: at the default 5 cm the orbiting ball's own probe was re-captured only every second frame, so its reflection stepped. The planar atlas keeps 6 slots for the mirror cube (was 12).
- **The same frame.** Probe faces render, and their probes are filtered, before the composite in the frame they show, so the block in a ball is where the block is.

## No after-images: reflections that do not move with the jitter (2026-10-07, evening)

With one probe per ball, refreshed every frame, a passing object still left a faint after-image and seemed to step. Frame by frame (`timescale 0` and `after 40 timescale 1` hold the orbiting block beside the floor ball while the probes settle, then let it pass): with TAA off the block's reflection was a single copy moving every frame. The trail was TAA's history: reactive pixels still took 65 % of last frame, and along a moving edge the clip box contains the block's colour, so a trail of one or two pixels survived each frame.

History is there because the jitter moves each pixel's sample over the ball, and on a ball the normal, and with it the reflected direction, turns by several probe texels per pixel. The composite now takes the normal at the pixel's centre: the sample sits `Jitter × size / 2` pixels off it, and the normal's gradient comes from the four neighbours (central differences, one-sided at an edge), which carry the same offset. The reflection then stays the same from frame to frame while nothing moves, so mirror interiors need almost no history: `ReactiveFeedback` 0.8 (was 0.35), `ReactiveClipGamma` 0.5 (was 0.75). Only interior pixels are reactive (all four neighbours within 5 % of their depth; `ScreenSpace::InteriorTexel`). Silhouettes, where the jitter decides the coverage, keep full TAA so they stay anti-aliased.

**The second box was the parallax march, not an old position.** With TAA off, the block near a ball still showed twice: once parallax-corrected, and once more beside it with a stair-stepped edge. Sampling the probe by direction only (no march) showed one clean block, exactly where the second copy sat, so the cube itself was clean. A ray from the ball's surface that crosses nothing within reach ended on its own direction, and the probe's texel in that direction is the block as the ball's centre sees it. That held whether the ray went behind the block (as the centre sees it) or passed in front of it: an undisplaced second copy. The probe does not know what such a ray reaches. Now `SampleProbe` returns confidence 0 for a ray that crossed nothing although the probe sees something nearer than `ParallaxReach` along it, and the composite takes the environment (`Specular`, the sky along the ray) for that share, weighting each probe by its confidence. Rays that pass within `ParallaxHidden` behind an object's surface (a side the centre cannot see) still take the object, and rays that cross something real behind it take that. Three more edge cases drew a faint dotted outline of the copy:
- The stored distances are read as the nearest of the four texels around a direction (`ProbeTexelDistances`: the 2 x 2 texel centres on the cube face, each read exactly; no gather extension needed), not their bilinear blend. At a silhouette the blend of a near object and the sky is a surface that is not there.
- A hit on a silhouette texel (its four distances more than 1.5x apart) is not trusted, and the ray marches on.
- A crossing entered from the side (a step before it, the farthest of the four texels was more than 1.5x farther: the ray grazed the outline instead of meeting the face) is not trusted either.

Measured frame by frame (14 frames of the block passing the floor ball): isolated off-colour pixels around the block went from 10-18 per frame to 2-3, and the remaining ones also show without parallax.

## Debug views

`ReflectionSettings::Debug` (`sandbox.reflectdebug`, or the Rendering tab):
- **Sources:** planar is yellow, SSR is red, rejected SSR (hidden side) magenta, probes green, the environment blue. Non-reflective pixels are dimmed grey.
- **Probe age:** a hue per probe slot × its coverage, darkened as its oldest face ages (0 → 4 s).

## Cost (RTX 4070, 1080p, the reflection lab)

GPU 12.1 ms with 15 active probes and 3 faces captured per frame, against 11.9 ms with probes and back faces off (the frame-to-frame noise is about ±0.3 ms). `render.stats 120` prints the per-pass times. The back-face pass is one depth-only draw of the visible scene.

## Tests

- `Render.PlanarReflections` (5): lookup against the analytic mirror ray and reprojection after camera motion; culling (back-facing, tiny) and the far-LOD SSR preference (0 for near mirrors, even grazing); coplanar sharing and footprint sizing; capture reuse until motion, movers (and once after a mover left) or age, and the dynamic budget; sphere caps, their sizing and supersampled render sizes.
- `Render.ScreenSpace.Reference.OnlyReflectiveHitsReadTheHistory`.
- `Render.ReflectionProbes.FacesThatSeeMovingObjectsAreRecapturedFirst` (with the clean-up capture after the mover left), `StillFacesAreNotStarvedByConstantMovers`, `AProbesChangedFacesAreCapturedTogether`.
- `Render.Temporal.Reference.ReactivePixelsDropMovedReflections`.
- `Render.ReflectionProbes.AProbesChangedFacesAreCapturedTogether` also checks the on-screen weighting (the nearest probe about every second frame with room for one, the far ones at no less than half that).
- `Render.ReflectionProbes` (5): faces that see movers go first (frustum test, range, the owner ignored); face bases, views and projections; capture distances; selection and blending; parallax against a wall and a sphere; an occluder in front of a far wall; a ray passing behind a near object; jitter independence; scheduler budgets, slots, moved and dynamic probes.
- `Render.ScreenSpace.HitValidation` (3).
- `Render.ScreenSpaceEffects.BackFaceDepthAndProbesReachTheirPasses`.
- `Render.Visibility.CaptureViewsCullTheirExcludedObject`.
- `Game.Sandbox.TheReflectionLabHasProbesAndADynamicFloor`.
- The validated `RHI.Vulkan.Smoke.ScreenSpace` frame with back faces.
