# Item 79 validation — controls, themes and canvases, September 25, 2026

This record covers the third item 79 checkpoint (Phase 20), the two checklists added to the plan after the paragraph/rendering checkpoint ([its record](Item79-2026-09-25.md)):

- **Controls:** `UiDocument` controls (`UiControls.cpp`: buttons, checkboxes, toggles, sliders with ticks and value labels, scroll bars with step buttons and Auto/Overlay visibility), visual states with layered rules, transitions, opacity and image skins (`UiVisuals.cpp`), document themes (`UiTheme`), Tab/spatial navigation, themed `UiWidgets`, and gamepad navigation in `UiInputBridge`.
- **Canvases:** `UiCanvas` (screen, render-surface, world-panel and billboard placement, rays, hits, projections), `UiCanvasRouter` (multi-canvas pointer, capture and focus), world drawing in `UiRenderer` (canvas-to-clip matrix, derivative coverage and MSDF ranges, scene depth, canvas fade, mip targets), `UiRenderSurfaces`, and the CPU reference's `CanvasAt`/`RasterizeProjected`.
- **Text fix:** point-only contours and zero-length lines are dropped before msdfgen.

The contracts are in [Text and retained UI](../TextAndUi.md). No runtime path constructs the UI yet (item 56); legacy text/UI stays active and nothing was retired. Item 79 stays open.

## Results

| Gate | Cases | Checks | Result |
| --- | ---: | ---: | --- |
| Linux `linux-debug` configuration, default suite | 765 | 658,001 | Pass |
| Previous checkpoint in this container | 740 | 657,513 | Pass |
| Focused filter (`Text UI UiInput Render.Ui Input.TextEditing ShaderCompiler.UiLayout`) | 82 | 3,171 | Pass |
| Same modules and suites (all but the shader-layout case) under `-fsanitize=address,undefined` with LeakSanitizer | 81 | 3,146 | Pass, no reports |
| All default targets, including `SwimTextUiPublicHeaders` with the new public headers | — | — | Build |
| `ShaderCompiler.UiLayout` (reflection vs. `UiRenderBindings`, `GpuUiQuad`, 96-byte push constants) | 1 | — | Pass |
| `ShaderCompiler.GpuAvBudget` for `SwimUiQuad` | — | — | 13 buffer accesses (limit 75) |
| `scripts/verify-build-layout.py` with the new controls/canvas rules (checked to fire on a planted include and an unregistered smoke) | — | — | Pass |
| Native `RHI.Vulkan.Smoke.UiRendererMatchesTheCpuReference` on SwiftShader, validation disabled | 1 | — | Pass, 0 outliers in 159,840 pixels |
| Native `RHI.Vulkan.Smoke.UiWorldCanvasesMatchTheCpuReference` on SwiftShader, validation disabled | 1 | — | Pass, 0 outliers in 358,868 pixels |
| Both smokes under the `core`/`sync`/`gpu`/`all` profiles | — | — | **Not run here** (no validation layer); desktop gate |

### New cases (25)

- `UI.Controls` (6): checkboxes (pointer, label hit target, keys, activation, mixed, read-only, disabled); toggles (click, knob drag, eased knob, Enter, code changes snap); sliders (jump, drag with grab offset, clamping, keys, cross-axis navigation, wheel while focused, vertical, page clicks, snapping, validation); scroll bars (thumb length and position, sync from code in the same Layout, drag, paging, wheel, `SetValue`, Auto hiding, target validation); overlay bars (floating placement, fade and reappearance); slider value labels, tick marks and scroll bar step buttons with hold-repeat.
- `UI.Theme` (2): theme changes restyle paint, layout (axis swap for vertical sliders) and text; `Customize`; node rules override the class; `Paint | Text` theming keeps a node's size; invalid themes and rules rejected; transitions ease through `Update` and style changes snap; image skins per state are paint-only; paint revisions.
- `UI.Widgets` (1 new, 1 reworked): themed buttons through hover/press/focus/disabled without relayout; subtree opacity.
- `UI.Navigation` (2): `TabIndex` order with negative indices skipped; spatial navigation on a grid, no wrap-around, editors keep Left/Right, `SetArrowNavigation`.
- `UI.Canvas` (4) and `UI.CanvasRouter` (3): see the plan's world-space checklist.
- `UiInput.Bridge` (2): gamepad navigation, slider adjustment, activation, hold-repeat, stick edges, East/shoulders; router frames casting the mouse into a world panel, focus loss.
- `Render.Ui.Reference` (2): world canvases through the screen mapping equal the screen rasterizer (< 10⁻⁴); perspective coverage scaling, canvas opacity, behind-camera culling and derivative glyph ranges.
- `Render.Ui.Renderer` (1): world frames push the canvas matrix, flags and opacity, bind the depth pipeline and attachment, draw one mip through a view of that level; validation of depth, offsets, opacity and mips; depth pipeline descriptions.
- `Render.Ui.Surfaces` (1): bindless surfaces, one draw per mip, redraw only on paint changes (`Force`, abort), stats, `PanelPaint`, release and slot reuse, validation.
- `Text.Atlas` (1): `PointOnlyContoursLeaveNoDistanceFieldArtifacts` (fails on the previous code).

`UI.Widgets.ButtonsFollowHoverPressAndFocusWithoutRelayout` became `…ThroughTheThemeWithoutRelayout` (`UiButtonColors`/`UiButtonStates` are replaced by themes and state rules). The mock command list now records `BeginRendering` (color and depth views, render area).

## Native smokes on a software device

As in the previous record, SwiftShader (Subzero) was built from source and run with SDL's offscreen driver, a variant of `UiQuad.slang` using `SV_VulkanVertexID`/`SV_VulkanInstanceID` (identical otherwise; not committed) and validation disabled through a local, uncommitted fixture patch.

### `UiRendererMatchesTheCpuReference` (screen overlays, unchanged frames)

| Frame | Compared | Lit | Outliers | Worst |
| --- | ---: | ---: | ---: | ---: |
| `sdr` | 31,968 | 24,178 | 0 | 5.70·10⁻³ |
| `hdr10` | 31,968 | 24,176 | 0 | 1.48·10⁻³ |
| `scrgb` | 31,968 | 24,178 | 0 | 6.12·10⁻³ |
| `linear-srgb` | 31,968 | 24,178 | 0 | 6.29·10⁻³ |
| `sdr-grown` | 31,968 | 24,178 | 0 | 5.94·10⁻³ |

The screen path now goes through the canvas-to-clip matrix; the results are identical to the previous record.

### `UiWorldCanvasesMatchTheCpuReference`

A 256 × 160 RGBA16Float scene target with D32Float reverse-Z depth, cleared by a scene pass; UI composed with `Linear` encoding at 1.5× UI white. The smoke document (card, clipped bidi/fallback text, nine-slice, overlay) is a 256 × 128 canvas at DPI 1.25; the themed-controls document (checked checkbox, hovered toggle, focused slider with ticks and value label, button, scroll area with step buttons) is laid out at DPI 0.85.

| Frame | Camera and placement | Compared | Lit | Outliers | Worst |
| --- | --- | ---: | ---: | ---: | ---: |
| `world-ortho` | orthographic, panel 1:1 | 40,160 | 24,178 | 0 | 6.10·10⁻³ |
| `world-faded` | same, canvas opacity 0.5 | 40,160 | 24,178 | 0 | 3.19·10⁻³ |
| `world-occluded` | same, scene depth cleared nearest | 40,160 | 0 | 0 | 8.14·10⁻⁶ |
| `world-rotated` | perspective, panel rotated 26° | 40,894 | 6,791 | 0 | 3.50·10⁻³ |
| `world-billboard` | perspective, off-axis spherical billboard | 40,876 | 6,630 | 0 | 3.41·10⁻³ |
| `world-widgets` | orthographic, controls | 40,835 | 21,639 | 0 | 3.70·10⁻³ |
| `world-widgets-billboard` | perspective billboard, controls | 40,888 | 6,068 | 0 | 4.82·10⁻³ |
| `surface mip 0` | `RGBA8UnormSrgb` surface, 256 × 128 | 31,968 | 24,178 | 0 | 7.00·10⁻³ |
| `surface mip 2` | 64 × 32, re-rasterized at DPI 0.3125 | 1,967 | 1,442 | 0 | 6.30·10⁻³ |
| `surface panel` | orthographic panel sampling the surface (not redrawn) | 40,960 | 24,638 | 0 | 3.71·10⁻⁴ |

Tolerances: 2.5 LSB of UI white and ≤ 1 % outliers for 1:1 frames; 6 LSB and ≤ 3 % for perspective frames, which compare hardware derivatives (2 × 2 pixel differences) with the reference's analytic footprints — SwiftShader stayed within the 1:1 tolerance even there. The two passes took 19.2 s on the CPU rasterizer.

The GPU images were also inspected visually (dumped by a temporary, uncommitted hook): perspective foreshortening, billboard orientation, clipped text, the themed controls with ticks, value label and step buttons all look as intended.

### Bug found by the visual check

The fixture's `u` had a streak above its bowl, identical in the GPU image and the CPU reference — so the distance field itself was wrong. The glyph's outline has a point-only second contour (a lone on-curve point, as some fonts and subsetting tools leave); it became an empty msdfgen contour, which the overlap-aware combiner turns into a false edge. `FontFace` now drops empty contours and zero-length lines before `normalize`; the new `Text.Atlas` case checks the open top of the `u` and fails on the old code.

## Environment

As in [the previous record](Item79-2026-09-25.md): local-only configure flags (`-DSDL_X11=OFF -DSDL_WAYLAND=OFF -DSDL_UNIX_CONSOLE_BUILD=ON`, an Eigen mirror), Slang 2026.16.1, SwiftShader built from source. No dependency was added.

## Still required on the desktop

1. Both UI smokes with the committed shader under `core`, `sync`, `gpu` and `all`; record the `[ui 1080p]` draw time.
2. Runtime wiring with item 56: the scene/ECS canvas component, world UI among transparents (sorting, fog), sandbox migration.
