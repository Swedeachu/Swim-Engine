# Camera effects, lensing, picking and the runtime console

## Camera effects (`Features/CameraEffects.h`)

Three render features, each in the stage where its physics happens:

```
scene (linear HDR) -> GravitationalLensing (order 5: black holes) -> DepthOfField (20) -> CameraLens (30)
   -> [exposure, bloom, white balance, contrast, saturation, tone mapping: PostProcessSettings]
   -> FilmSensor (AfterPostProcess: display-referred)
```

All three start disabled and cost nothing until enabled.

**`DepthOfField`** (programs `DepthOfFieldPrepare`, `DepthOfFieldGather`, `DepthOfField`) uses the thin-lens circle of confusion c = A f |s − S| / (s (S − f)), with A = f / N. The focal length comes from the camera's field of view on a `SensorHeightMm` (24 mm) sensor. The CoC is converted to pixels through the sensor height.
- A half-resolution pass stores the colour and the signed CoC, using the nearest of each 2×2 block so thin foreground edges keep their blur. Autofocus (`FocusDistance` 0) takes the nearest of five centre taps.
- A golden-angle gather (`Samples`) lets each sample contribute when its own disc reaches the pixel. Samples behind the pixel blur no more than the pixel itself, so a sharp subject keeps a sharp outline; samples in front spill over it. `AnamorphicSqueeze` stretches the disc into an oval.
- The full-resolution composite blends by the pixel's CoC and the foreground's coverage.

**`CameraLens`** (programs `CameraLensHalation`, `CameraLens`) works per output pixel:
- Radial distortion r(1 + k1 r² + k2 r⁴): k1 > 0 is barrel, k1 < 0 pincushion. It is blended with an equidistant fisheye (`Fisheye`), and `FitZoom` maps the corners onto the frame's corners.
- Lateral chromatic aberration: red and blue land at radii 1 ± CA·r².
- Corner softness: a 5-tap cross growing with r².
- Natural vignetting: cos⁴ of the field angle raised to `Vignette`.
- Halation: a quarter-resolution, soft-thresholded glow of the highlights, warm-tinted and optionally stretched horizontally.
- A lens colour filter.

**`FilmSensor`** (program `FilmSensor`) works on the RGBA8 display frame:
- Contrast-adaptive sharpening, an unsharp mask limited like AMD CAS.
- Grain: zero-mean value noise on `GrainSize` cells, new every frame. It is strongest in the mid-tones (4L(1 − L)) and has an optional chroma part.

**Presets are derived, not tabled.** A `CameraLook` describes a camera physically: f-number, lens age (0 modern … 1 uncorrected), residual distortion, anamorphic squeeze, film or digital, ISO, white balance in Kelvin, stock contrast and saturation, a filter and in-camera sharpening. `DeriveCameraLook` computes every effect value from it:
- vignette = 0.15 + 0.35·age + 0.25·speed;
- CA = 0.0005 + 0.004·age + 0.002·(squeeze − 1);
- grain ∝ √(ISO / 100): 0.011 for film, 0.0035 for digital;
- halation only on film;
- white balance through the CIE daylight locus: the camera neutralizes x_D(K) while the scene is lit at D65, and the grading's temperature axis moves x by 0.05 or 0.1 per 65 units.

`CameraPresetLook` gives the presets:

| Preset | Look |
| --- | --- |
| Off | Every effect off, neutral grading |
| Clean modern | f/2.8, a modern lens, ISO 200, light sharpening |
| Cinematic 35mm | f/2.0, film, ISO 250, 6900 K, a vintage-coated cine prime |
| Anamorphic | 2× squeeze, f/2.8, film, ISO 500, 6100 K |
| Vintage | An old lens, film, ISO 400, 8000 K, a warm filter, low contrast and saturation |
| Neutral photoreal | f/5.6, a perfect lens, ISO 100 |

`ApplyCameraLook` writes the derived values into the three features and the grading, and each value stays editable afterwards. In the sandbox:
- `sandbox.camera 0..5` or the *Camera/Post* tab picks a preset;
- `sandbox.dof <f-number> [focus m]` sets the depth of field.

At the sandbox's 60° field of view (a 21 mm lens), depth of field is physically shallow only up close; narrow the field of view for a longer lens.

## Black holes (`Features/GravitationalLensing.h`)

`GravitationalLensing` (BeforePostProcess, order 5) traces, for every pixel, the light ray backward through each hole's region (a sphere of `Reach` R_s, at most four `Lenses`) as a Schwarzschild null geodesic: a = −1.5 R_s h² x / r⁵ with h = |x × v| (the orbit equation u″ + u = 1.5 R_s u²), integrated with midpoint steps of 0.1 (r − R_s/2), shrinking near the hole (at most 240). A ray:
- **falls through the horizon:** its background is black, the shadow at the photon-capture radius (3√3/2) R_s;
- **passes behind a surface of the frame:** at every step the point on the bent ray is projected into the frame and tested against the depth buffer (a hit when it lies just behind the surface drawn there, within a thickness that grows with distance and with the step). The first hit is what the pixel sees: a surface inside the region is seen through the bending around it, and foreground occludes the gas behind it. Only depth is read per step; the colour is fetched once at the hit;
- **escapes the region:** it continues straight, in steps that grow with distance, with the same test (lensed floor, objects, Einstein rings, the far-side images); with no hit it shows the frame in its direction (the sky), fading to the pixel's own colour where that leaves the screen. A downward ray that found no floor went past the ground's edge and keeps the last ground it crossed, so the void beyond a finite floor never shows as a band of sky.

Tracing the curved path against the depth buffer replaced a lookup at the unbent ray's depth, which cut the image into slices where the bent ray and the pixel's own depth disagreed (near the floor, and across walls behind the hole).

The bending builds up only inside the region, so it fades to zero at its edge without a seam. Deflection is exact where it matters: 2 R_s / b far out, with the second-order term, and capture below 2.6 R_s.

Along the way the ray crosses the gas (steps ≤ 0.3 R_s), integrated front to back as emission and absorption:
- **The accretion torus** in the hole's equatorial plane (the entity's local +Y) from about 3 R_s to `GasRadius`: turbulent value-noise fBm with a height that grows with radius, rotating differentially (ω ∝ r^−3/2, `GasSpeed` at 3 R_s), so clumps shear into orbital streams.
- **Electron-shell rings** (`Orbits`, 0–3): inclined orbits at 4.2, 5.6 and 7 R_s, the middle one retrograde. Each carries streaming gas and three bright clumps racing around it.
- **Colour:** emission flows in rainbow hues with the gas, Doppler-brightened on the approaching side, scaled by `GasBrightness` and `GasDensity`. It is HDR, so it blooms.

`TraceRay` and `GasDensityAt` are the CPU definition (tests), and `Upsert`/`Remove` let behaviours own lenses. The sandbox's black hole (bookmark 9, `Sandbox::BuildBlackHole`) rasterizes nothing:
- an entity scaled to 2 R_s and tilted;
- the `BlackHole` behaviour, which writes its position, R_s and disk axis to the feature every frame;
- a `Pickable` the size of its shadow, and a `MouseDrag`.

The Rendering tab's *Black hole* section sets the gas density, brightness, speed, rings and bending strength.

## Picking

- `Engine::Ray` and `RayQueries` (`Engine/Math/Ray.h`) intersect rays with a plane, a sphere, an AABB, a transformed box, sphere or capsule (any affine transform), and a triangle, plus `ClosestT`.
- `Camera` projects in both directions: `ScreenPointToRay`, `WorldToScreen`, `ScreenToWorld(x, y, distance)`, `ScreenToWorldAtDepth`, `ScreenToNdc` and `NdcToScreen`. `CameraSystem` forwards them on the render surface.
- `ScenePicking::PickAll`, `PickClosest` and `PickAtScreen` test `Pickable` shapes and `Rigidbody` colliders in their world transforms. They support layer masks and filters, need no physics world or renderer, and work while paused.

`Game::MouseDrag` builds dragging on top of these:
- a left press whose closest pick is the entity, or one of its children, grabs it;
- while the button is held, the entity follows the mouse ray on the camera-facing plane through its centre, so it keeps its depth and the grabbed spot stays under the cursor;
- the UI blocks grabs while it owns the pointer, the right button still flies the camera, and F still fires the ball shooter.

## Runtime console

`Engine::RuntimeConsole` (the model) and `Engine::RuntimeConsoleOverlay` (the UI) are owned by `SwimEngine`, so every scene has them.
- **Opening:** grave/tilde toggles the console, Enter runs a line through the engine's `CommandRegistry`, and Escape closes it. Up and Down walk the history like a shell and return to the draft.
- **Scrollback:** it shows the echoed command, whatever the command printed to stdout or stderr, and errors.
- **Input:** while open, the console takes the keyboard from the game.
- **Commands:** built in are `help` (every command), `clear` and `echo`; the engine adds `render.stats [n]` (print pass timings every n frames). `CommandRegistry::RegisterTyped<Args...>` registers commands with parsed arguments.
- **Overlays:** the overlay is a `UiRuntime` engine overlay (`AddOverlay`, `SetOverlayVisible`), drawn above scene canvases.

## Tests

- `Engine.CameraEffects` (4): the thin-lens CoC (35 mm on full frame, the f² / (N (S − f)) limit, f-number scaling); lens geometry (identity, corner fit, barrel/pincushion direction, fisheye, monotonicity); presets (Off is neutral, every preset stays within subtle bounds, film has halation, age and speed increase vignetting and CA, grain ∝ √ISO, white balance signs, apply/undo); each feature records its passes in its stage on the mock device (half-resolution depth of field, quarter-resolution halation, RGBA8 sensor).
- `Engine.GravitationalLensing` (3): geodesics (capture below the shadow radius, strong bending just outside it, the weak-field limit within 6 %, monotonic bending); the gas (empty inside 1.2 R_s and beyond its radius, a torus in the disk plane, flowing in time, rings off the plane); owned lenses and one pass.
- `Engine.ProceduralMeshes.TheAnnulusIsAFlatRingBetweenItsRadii`, plus the annulus in the winding and tangent checks.
- `Game.Sandbox`: `TheFourTabsFitInsideThePanel`, `CameraPresetsDriveTheFeaturesAndTheControlsFollow`, `TheBlackHoleLensesAndDragsOnACameraFacingPlane` (grab from its screen position, move 120 px, same camera depth, lens follows, shooter unaffected).
- `Engine.Picking` (3) and `Engine.RuntimeConsole` (4).
