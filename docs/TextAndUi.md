# Text and retained UI

Critical-path item **79** (Phase 20). Checkpoints:

- **2026-09-24 — foundation:** font faces, horizontal run shaping, the MSDF glyph atlas and the retained `UiDocument` (layout, paint records, pointer/focus input).
- **2026-09-25 — paragraphs, editing and rendering:** paragraph layout (bidi, script itemization, cluster fallback, line breaking, alignment), caret/selection/IME text editing, flex/anchor/aspect layout, rounded/bordered solids and nine-slice images, measure and paint caches, parallel atlas prewarm, a widget layer, the Input → UI bridge, and `Renderer/UiRendering`: the RenderGraph UI pass with atlas residency and SDR/HDR composition, with a CPU reference and a native smoke.
- **2026-09-25 — controls, themes and canvases (this document's current state):** checkboxes, toggles, sliders (ticks, value labels) and scroll bars (step buttons, auto/overlay visibility) as document behaviour; visual states with per-state rules, eased transitions and image skins; document themes; Tab order, spatial and gamepad navigation; canvas placement (screen, render surface, world panel, billboard) with ray-cast input, pointer capture and one keyboard owner across canvases (`UiCanvasRouter`); world-space drawing (a canvas-to-clip matrix, derivative-based coverage and MSDF ranges, depth testing, canvas fade) and render surfaces with re-rasterized mip chains; a second native smoke; and a glyph-atlas fix for point-only contours.

Item 79 stays **open** until the native smokes have passed on the desktop and the runtime draws its UI through them. Sandbox migration and the scene-side canvas component wait for the modern renderer to present frames (item 56); the legacy text/UI remains active.

## Module map

| Path | Role | Depends on |
| --- | --- | --- |
| `Systems/Text` | `FontFace`, `FontCollection`, `GlyphAtlas`, `TextSegmentation`, `TextLayout`, `Utf8` | FreeType, HarfBuzz, msdfgen, SheenBidi, libunibreak (private) |
| `Systems/UI` | `UiDocument` (tree/API, layout, paint, input, editing, controls and visual-state units), `UiTheme`, `UiWidgets`, `UiCanvas` (placement math), `UiCanvasRouter` (multi-canvas input) | Text |
| `Systems/UiInput` | `UiInputBridge`: `Input::InputSystem` frame → a `UiDocument` or a `UiCanvasRouter` (mouse, keys, text, IME, gamepad) | UI, Input |
| `Renderer/UiRendering` | `UiRenderer` (screen and world canvases), `UiAtlasTextures`, `UiRenderSurfaces`, `UiRenderReference` (`Ui::` CPU definition), records/bindings/settings | RenderGraph, RHI contract, Resources, UI/Text headers |
| `Shaders/Slang/Ui` | `UiRecords.slang`, `UiQuad.slang` (`SwimUiQuad`) | — |

`scripts/verify-build-layout.py` enforces the boundaries: text libraries stay inside `Systems/Text`/`Systems/UI` and the Text tests, no public header names a library type, `Systems/UI` includes only UI and Text headers (no renderer, scene, platform, input, SDL, EnTT or glm), `UiRendering` includes only RenderGraph/RHI/Resources renderer headers, no other renderer module includes UI, and `UiInput` includes only UI, Text and Input. It also requires the controls/canvas units, their suites and both native UI smokes.

All five text libraries are compiled from pinned sources as private static libraries in `Swim::TextDependencies` (`cmake/TextDependencies.cmake`): FreeType `VER-2-14-3`, HarfBuzz `14.5.0` (single TU), msdfgen `v1.13` (core), **SheenBidi `v3.0.0`** (unity TU, UAX #9 and script runs) and **libunibreak `8.0`** (UAX #14 line breaks, UAX #29 graphemes and words; Unicode 17). The UI modules compile only where those dependencies are available.

## Text

### Font faces and shaping (unchanged contract)

`FontFace` copies font bytes and owns its FreeType face and immutable HarfBuzz font. `Shape(utf8, size, options)` shapes one horizontal script/direction run with UTF-8 byte clusters (grapheme-monotone), kerning and ligatures. Coordinates are font coordinates (Y up). Concurrent const calls are supported; only FreeType outline extraction is serialized per face now — distance-field generation runs concurrently (see `GlyphAtlas::Prewarm`).

### Fallback: `FontCollection`

An immutable, shareable ordered chain (face 0 is primary; at most 64). There is no OS font lookup. `SelectFace(cluster, preferred)` picks, per grapheme cluster:

1. the preferred face (the previous cluster's) when the cluster starts with whitespace, punctuation or a symbol and that face covers it — runs are not split around spaces;
2. otherwise the first face that maps every code point of the cluster, ignoring controls and default-ignorables (ZWJ/ZWNJ, variation selectors, bidi controls, tags);
3. otherwise the primary face, which shapes `.notdef` (reported as `MissingGlyphs`).

### Segmentation: `TextSegmentation`

- `FindTextBoundaries(utf8, language)`: grapheme, word and line-break (`None`/`Allowed`/`Mandatory`) flags before every byte, from libunibreak with the language's tailoring.
- `FindScriptSpans(utf8)`: maximal single-script runs (ISO 15924 tags), Common/Inherited characters joining their neighbours.
- `AnalyzeBidi(utf8, direction)`: paragraphs (P1; CR LF, LF, CR, U+2029, NEL separate them), base level (P2–P3 for `Auto`, left-to-right without a strong character) and per-byte embedding levels. Text ending in a separator gets a final empty paragraph that keeps the previous direction.
- Input must be valid UTF-8 (≤ 1 MiB); `Utf8.h` decodes, steps back and sanitizes (every invalid byte → U+FFFD).

### Paragraph layout: `TextLayout`

`TextLayout(utf8, fonts, desc)` builds, immutably:

1. sanitized text (offsets refer to `GetText()`), boundaries, script spans and per-grapheme faces;
2. per bidi paragraph: a measuring shape of the (level, script, face) items, then greedy line breaking over UAX #14 opportunities. Trailing spaces hang (they never cause a wrap); a word wider than the line breaks between graphemes (emergency break, at least one grapheme per line). U+2028/VT/FF end lines without ending the paragraph and are never shaped;
3. per line: SheenBidi's line runs (rules L1–L2, so trailing whitespace takes the paragraph level) split by face/script, each shaped again with its own level — the visual order of `TextRun`s and `TextGlyph`s;
4. vertical metrics from the tallest face used on the line (the primary face always counts; `LineSpacing` multiplies the line height, extra space split above and below);
5. alignment: `Start`/`End` resolve per paragraph direction; `Left`/`Right`/`Center` are absolute. The box is `MaxWidth` when finite, else the widest line. Hanging whitespace sits outside the box (to the right in LTR, to the left in RTL paragraphs).

Glyph origins are in layout space (top-left origin, Y down, on the baseline, offsets applied). `GetWidth()` is the widest visible line, `GetHeight()` the sum of line heights.

**Editing queries** (grapheme boundaries are caret stops): `GetCaret` (visual x/top/height; at a direction boundary the caret attaches to the leading edge of the following character), `HitTest(x, y)`, `GetSelectionRects(begin, end)` (one rectangle per contiguous visual piece — a mixed-direction selection is several), `Next/PreviousCaretStop`, `Next/PreviousWord` (starts of words, spaces skipped), `LineStart/LineEnd`, `LineAbove/LineBelow` (preferred x). Soft-wrap positions use downstream affinity (the offset belongs to the next line); `LineEnd` of a soft line stops before its hanging space so the caret stays on that line. A ligature spanning several graphemes ("ffi") divides its advance evenly between them. Cursor movement is logical (Right moves forward in memory order, which is visually leftwards inside RTL text).

Shaping runs twice per line (measure, then per line segment). Contexts are not shared across a soft break. Layout cost is linear in the text; `UiDocument` caches layouts per node and width.

## Retained UI

### Layout

`UiDocument` keeps its first-checkpoint contract (unique IDs, cycle/depth checks, Auto/logical/percentage sizes with min/max, margins/padding/gaps, row/column/overlay flow, clipping and scrolling, logical DPI units) and adds:

- **Flex (Row/Column parents):** children `Grow` into positive free space (by weight) and `Shrink` from an overflow (weighted by preferred size), clamped to min/max. The parent's `Justify` distributes what is left (`Start`, `Center`, `End`, `SpaceBetween`, `SpaceAround`, `SpaceEvenly`); `AlignItems`/`AlignSelf` place children on the cross axis (`Start`, `Center`, `End`, `Stretch` — stretch fills Auto cross lengths). Defaults (grow/shrink 0, start/start) keep the previous behaviour. Overlay parents align children on both axes.
- **Anchors (Absolute children):** `AnchorMin`/`AnchorMax` are fractions of the parent's content box. Equal anchors pin `Pivot` (a fraction of the node's size) at `AnchorMin × size + Offset`; different anchors on an axis stretch the node between them minus its margins. The default (all zero) is the previous top-left `Offset` placement.
- **Aspect ratio:** `AspectRatio` (width/height) derives an Auto axis from the other (width wins when both are Auto).
- **Text in layout:** `SetText(node, fonts, text, size, {direction, language})` (a single-face overload remains). Style carries `TextAlign`, `TextWrap` and `LineSpacing`. Wrapping text measures against the room its ancestors give it: the node's own definite content width, else the nearest definite ancestor's content width (the canvas at the root) minus paddings and margins, capped by `MaxSize`. Arrangement lays the text out again at the final content width when wrapping or alignment depends on it.
- **Images:** `SetImage(node, UiImage)` — renderer-defined texture/sampler handles (for example bindless indices), natural size (the node's intrinsic content size), UV rectangle, straight-alpha tint, `Stretch`/`Contain` fit and nine-slice borders (on-screen widths plus UV fractions; borders shrink proportionally when the box is too small). Textures are sampled as **premultiplied** alpha.
- **Rounded and bordered backgrounds:** `CornerRadius` (clamped to half the shorter side) and an inner `BorderWidth`/`BorderColor` ring; paint-only.

**Caches.** `SetStyle` compares the old and new style: changes to colors, radius or border are paint-only and skip layout entirely (`Layout` returns without a new revision). Any other change marks the node and its ancestors; `Measure` reuses a node's cached size when neither it nor a descendant changed and its inputs (available size, wrap room) are equal, so a leaf change remeasures its ancestor path only (`GetMeasuredNodeCount`). Arrangement still visits the whole tree (cheap; bounds, clips and order). `Paint` rebuilds only nodes whose content, bounds, clip or scroll changed, or all of them when a different atlas is passed (`GetRepaintedNodeCount`); the returned list concatenates the per-node caches in paint order.

### Paint output

`UiPaintQuad` gained `CornerRadius`, `BorderWidth`, `BorderColor` (solids) and `Texture`/`Sampler` (the new `Image` kind). Colors stay premultiplied linear RGBA; bounds and clips stay logical. Per node, in order: background, image quads, selection rectangles, glyphs, preedit underline, caret.

### Input and editing

Pointer input is still in framebuffer pixels; `PointerDown` takes modifiers (Shift extends a text selection). `Wheel(point, delta)` scrolls the innermost clipped node under the point that can still move. `PointerMove/Down/Up`, `Wheel` and `FocusNext` lay the document out again with the last canvas when it changed since (for example after typing earlier in the same frame); `HitTest`, `GetBounds` and `Paint` still require a current `Layout` (`IsLayoutCurrent`).

`SetEditable(node, true, {Multiline, MaxBytes})` makes a text node an editor (focusable and hit-testable regardless of its style):

- `KeyDown(key, modifiers)` routes to the focused node: Left/Right by grapheme or word (Control), Up/Down by line with a preferred column (ends of the text in single-line fields), Home/End by line or document (Control), Backspace/Delete by grapheme or word, Enter (line break in multiline fields, a `Submit` event otherwise), Escape (collapse the selection or abandon the preedit), Control+A/C/X/V through `SetClipboard` callbacks, and Tab/Shift+Tab focus traversal. Non-editable focus maps Enter/Space to activation and Escape to blur.
- `TextInput(utf8)` replaces the selection with committed text: sanitized, other C0 controls dropped, line breaks dropped in single-line fields, truncated at a grapheme boundary to `MaxBytes`. Committed edits queue `TextChanged`.
- `SetComposition(utf8, cursor)` shows an IME preedit at the caret, underlined, without changing `GetText()`; the IME owns the keyboard meanwhile. Commit arrives as `TextInput`; an empty composition, Escape or focus loss abandons it.
- A pointer press places the caret, a drag extends the selection; clipped editors scroll to keep the caret visible. `GetTextInputRect()` gives the caret rectangle in framebuffer pixels for the IME candidate window; `WantsTextInput()` says when to start platform text input; `SetCaretVisible` is the application-timed blink phase.

### Controls

A control is behaviour the document implements for an ordinary node (`SetControl(node, UiControl)`); its parts are ordinary descendant nodes listed in `UiControl::Parts`. Controls take input only through the document (pointer, wheel, keys, activation), never from the platform, so they behave the same on a screen overlay and on a world-space canvas. A control node is hit-testable and focusable (scroll bars only with `Style.Focusable`); parts are not, so presses anywhere on a checkbox's label or a slider's track reach the control.

| Kind | Input | Parts |
| --- | --- | --- |
| `Button` | Click on release inside; Enter/Space/gamepad A while focused | — |
| `Checkbox` | Click, Space/Enter: Unchecked ↔ Checked; Mixed (from code) → Checked | Track (the box), Mark, Mixed, Label (all in flow) |
| `Toggle` | Click or Space/Enter switches; dragging the knob past half switches on release; the knob eases over the knob's `TransitionSeconds` (`Update`) | Track, Thumb (the knob, a child of the track, placed), Label |
| `Slider` | Thumb drag keeps the grab offset; track press `Jump`s under the pointer (then drags) or `Page`s towards it; Left/Right (Up/Down when vertical) step, PageUp/PageDown page, Home/End; wheel steps while focused; vertical sliders put Min at the bottom | Track, Fill, Thumb and Tick marks (direct children, placed); Label (any node) shows the value when `LabelDecimals >= 0` |
| `ScrollBar` | Mirrors `ScrollTarget`'s offset on its axis: thumb drag, track press pages by the viewport (or jumps), step buttons step 40 units (or `Step`) and repeat while held (0.4 s delay, 20 per second, through `Update`), wheel over the bar, keys when focusable | Thumb, Decrement, Increment (direct children, placed) |

- **Values.** `Min ≤ Value ≤ Max`, snapped to `Min + k·Step` when `Step > 0`. `SetValue`/`SetChecked` (code) clamp and snap and emit nothing. Input emits `ValueChanged` (with `UiEvent::Value`) on every change and `ValueCommitted` at its end: drag release, each key, each click or wheel step. A cancelled pointer (`CancelPointer`, focus loss) keeps and commits the value reached. Check states are reported as 0, 1 and 0.5 (Mixed).
- **`ReadOnly`** controls keep hover, focus and visuals but never change by input (their axis keys then navigate). Disabled controls (`Enabled = false` on them or an ancestor) take no input and show `UiState::Disabled`.
- **Scroll bars** refresh from their target after every Layout (a bar arranged before its target is re-arranged once the target's extent is known, in the same Layout). The thumb length is `track × viewport / (viewport + maximum)`, at least `MinThumbLength`. `Auto` bars are neither painted nor hit while the target does not overflow (they keep their space); `Overlay` bars also fade out after `FadeDelaySeconds` without scrolling or hover (over `FadeSeconds`, through `Update`) and reappear when the target scrolls.
- **Geometry roles** (slider track/fill/thumb/ticks, scroll thumb/buttons, toggle knob) are placed by the control inside their parent's content box; their own sizes come from their styles (the theme's metrics). Everything else lays out normally.

### Visual states, rules and transitions

Every node has a `UiState` (`GetState`): Hovered, Pressed, Focused, Dragging, Checked, Mixed and ReadOnly come from its nearest interactive ancestor-or-self (hit-testable, focusable, editable or a control), so a checkbox's box and mark or a button's label follow the control; Disabled comes from the node's own availability.

The paint a node is drawn with (`GetVisual`, a `UiResolvedVisual`) is its style's paint fields, then the matching **state rules** of its theme class, then its own rules (`SetStateRules`). A rule (`UiStateRule`) applies when the state has every `When` flag and none of the `Unless` flags, and overrides any of background, border color/width, corner radius, text color, image tint, opacity, or the image itself (a **skin**: painted in the content box instead of the node's image; skins never change layout).

- **State changes ease** over the node's `TransitionSeconds` (smoothstep on colors, widths, radius and opacity; images switch at once) when the application calls `Update(seconds)` each frame; style, rule and theme changes snap. With 0 (the default) nothing needs `Update`.
- **`Opacity`** (style or rule) multiplies the node's paint and its descendants' (premultiplied, so blending stays correct); 0 paints nothing. It is paint-only.
- `Paint` resolves visuals before painting and repaints only nodes whose visual, bounds, clip, scroll or effective opacity changed. `GetPaintRevision()` changes whenever `Paint`'s output did (render surfaces redraw only then).

### Themes (`UiTheme.h`)

`UiDocument::SetTheme(shared_ptr<const UiTheme>)` (a default dark theme without fonts initially) turns a `UiPalette` (surface, accent, focus, text, track, … colors), `UiMetrics` (corner radius, border and focus widths, spacing, paddings, control height, text sizes, slider/checkbox/toggle/scroll bar dimensions, transition seconds, disabled opacity) and a font chain into one `UiClassStyle` per `UiThemeClass` (Panel, Label, Button, TextField, ScrollView, ScrollBar/ScrollThumb/ScrollButton, Checkbox/CheckBox/CheckMark/CheckMixed, Toggle/ToggleTrack/ToggleKnob, Slider/SliderTrack/SliderFill/SliderThumb/SliderTick/SliderValue): a base style plus state rules. `UiTheme::Customize(class, style)` adjusts any generated class (colors, sizes, extra rules, image skins). Invalid themes are rejected before anything changes.

`SetThemeClass(node, class, apply)` themes a node now and on every later `SetTheme`, taking the parts of its class chosen by `UiThemeApply`:

- `Paint`: background, border, radius, text/selection/caret colors, opacity and transition seconds, and the class's rules;
- `Layout`: width, height, min/max size, padding and gap — axes swapped for vertical controls and their parts (class styles describe horizontal controls), never for a slider's value label;
- `Text`: the theme's fonts and the class's text size, for nodes with text.

Per-node overrides are the node's own rules (after the class's); to keep a custom size, theme the node without `Layout`. `UiThemeClass::None` unthemes a node, keeping its current style.

### Keyboard and gamepad navigation

- **Tab order:** `FocusNext`/Tab visit focusable, available nodes with `TabIndex > 0` first in ascending order, then `TabIndex = 0` in document order, wrapping around; negative indices are skipped (still focusable by pointer or `Focus`).
- **Spatial:** `Navigate(Up/Down/Left/Right)` moves focus to the nearest candidate that way: facing-edge distance plus twice the gap across the direction (0 when the boxes overlap), no wrap-around. Without focus it focuses the first node in tab order. Arrow keys that the focused node does not use navigate (`SetArrowNavigation(false)` turns that off); editors keep Left/Right, sliders and scroll bars keep their axis.
- **Activation:** Enter/Space (`ActivateFocused`) toggle checkboxes and toggles and click anything else; Escape blurs.

### Widgets (`UiWidgets.h`)

Themed helpers build controls from nodes and theme them from the document's theme (text helpers throw when the theme has no fonts): `CreatePanel`, `CreateLabel`, `CreateButton`, `CreateTextField`, `CreateCheckbox(label, state)`, `CreateToggle(label, on)`, `CreateSlider(UiSliderDesc)` (range, step, page, orientation, track click, `Ticks`, `ShowValue`/`Decimals`), `CreateScrollBar(target, UiScrollBarDesc)` (orientation, visibility, track click, `StepButtons`) and `CreateScrollArea(rootStyle, vertical, horizontal, visibility, stepButtons)` (a clipped viewport with its bars in flow, or floating over its edges for `Overlay`). Unthemed helpers with explicit fonts and styles remain (`CreateLabel(fonts, …)`, `CreateImage`, `CreateScrollView`, `CreateTextField(fonts, …)`). Every part is reachable through `GetControl(node).Parts` for rules, images or replacement.

### Canvases (`UiCanvas.h`)

A document is always a 2D canvas in framebuffer pixels (its Layout size). A canvas's *placement* decides where those pixels appear — pure math, no renderer or scene dependency (row-major matrices on column vectors, rigid views looking down −Z, +Y-up clip space, +Y-down canvas pixels):

| `UiCanvasMode` | Placement | Drawn by |
| --- | --- | --- |
| `Screen` | `ScreenClipFromCanvas(width, height, offset)` | `UiRenderer` into any color target |
| `RenderSurface` | its own texture (`UiRenderSurfaces`), sampled by any material, mesh or world panel | `UiRenderSurfaces::Record` |
| `WorldPanel` | `CanvasToWorld(WorldPanel, UiWorldPlacement)`: a transform, `Pivot` (canvas fraction at the origin), `UnitsPerPixel` | `UiRenderer` with `ClipFromCanvas`, depth tested |
| `Billboard` | as a panel, facing the camera: `Spherical` (towards the camera position), `Cylindrical` (around `UpAxis`) or `ScreenAligned` (the camera's axes); `ConstantScreenSize` keeps `ScreenPixelsPerCanvasPixel` at any distance | same |

`ClipFromCanvas(canvasToWorld, camera)` gives the renderer's matrix; `CanvasFade` (1 before `FadeStart`, 0 after `FadeEnd`) and `CanvasDistance` (for back-to-front sorting) are helpers. For input: `ScreenRay(camera, pixel)` (perspective or orthographic, any depth convention), `IntersectCanvas(ray, canvasToWorld, size, twoSided)` (hit inside `[0, size)`, with distance and facing) and `IntersectCanvasPlane` (the unbounded plane, for capture); `ProjectCanvasPoint`/`ProjectCanvasRect` map canvas pixels to the viewport (IME windows).

### Multi-canvas input (`UiCanvasRouter`)

The router owns no documents; it routes one pointer and one keyboard across every canvas of a frame (`Add(UiCanvasDesc{document, mode, Interactive, BlocksPointer, Order})`, `SetScreenPlacement`, `SetWorldPlacement(canvasToWorld, size, twoSided)`, `SetCamera`):

- `PointerMove(UiPointer{Screen, Ray, SurfaceHits})` picks the hovered canvas: screen canvases first (top-most `Order`), where a hit-testable node is under the pointer (or anywhere inside with `BlocksPointer`); then world panels/billboards hit by the ray and the application's `UiSurfaceHit`s (a mesh ray cast converted from UV to canvas pixels for render surfaces), nearest first, with the same node-or-blocking rule — so the world behind a HUD or an empty part of a panel stays reachable. The previous canvas gets `PointerLeave`.
- `PointerDown` captures the pointer for the hovered canvas until `PointerUp`: its positions then follow the ray on the canvas's unbounded plane (a world slider keeps dragging off its panel; parallel rays keep the last point).
- **One focus owner.** A press that focuses a node makes its canvas the keyboard owner and blurs the previous owner's document; a press outside every canvas returns the keyboard to the game. `KeyDown`, `TextInput`, `SetComposition` and `Navigate` go to the owner; without one, Tab (`FocusNext`) and directions start in the first interactive canvas with focusable nodes. `GetTextInputRect()` gives the owner's caret in viewport pixels — offset for screen canvases, projected through the camera for world ones.

### Input bridge (`Systems/UiInput/UiInputBridge`)

`UiInputBridge::Apply(inputSystem, document, deltaSeconds)` or `Apply(inputSystem, router, camera, surfaceHits, deltaSeconds)`, once per frame after `InputSystem::AdvanceFrame` and the documents' `Layout`:

- window → framebuffer pixels by `FramebufferScale`; primary-button press/release; wheel notches × `WheelStep` (positive wheel scrolls up). With a router, the mouse is also a world ray through the camera (re-cast every frame, since world canvases move under a still mouse);
- key presses **including operating-system repeats** (now also PageUp/PageDown) and committed text in event order (`InputSystem::GetTextEditEvents()`); Control or, for macOS, Super as the shortcut modifier; keys reach the UI only while a node has focus (Tab starts traversal when `TabStartsNavigation`);
- IME composition only when `InputSystem::HasTextCompositionUpdate()`;
- **gamepad** (`Gamepad = deviceId`): D-pad and left stick (past `StickThreshold`) send arrow keys to the focused node — adjusting a focused slider on its axis, navigating otherwise, always navigating out of text fields — or navigate from nothing; held directions repeat after `RepeatDelaySeconds` every `RepeatIntervalSeconds` (needs `deltaSeconds`); South is Enter, East is Escape, the shoulders are Shift+Tab / Tab;
- application focus loss cancels pointer capture and clears UI focus.

It returns `PointerOverUi`, `KeyboardCaptured`, `GamepadCaptured` and `WantsTextInput` so the game can ignore input the UI consumed. Starting/stopping platform text input and `WindowSystem::SetTextInputArea` stay with the application.

## Glyph atlas

Unchanged contract (single owner, stable UVs, no eviction, page revisions, `GetChangedRows`), plus:

- `Prewarm(face, glyphs, parallelFor)`: distance fields of the missing glyphs are generated through a caller-supplied parallel-for (for example `JobSystem::ParallelFor`), then packed serially in the given order — pixel-identical to calling `Get` in that order. A failed generation inserts nothing and rethrows; `Contains(face, glyph)` checks the cache.
- `UiDocument::PrewarmGlyphs(atlas, parallelFor)` prewarms every glyph a laid-out document shows, so the following `Paint` only looks glyphs up (use before time-critical frames). True background population (while frames render) is still future work.
- Outlines are cleaned before msdfgen: zero-length lines and point-only contours (anchor or phantom points some fonts and subsetting tools leave) are dropped. An empty contour in msdfgen's overlap-aware combiner drew a streak through open glyph tops — the fixture's `u` showed one above its bowl (`Text.Atlas.PointOnlyContoursLeaveNoDistanceFieldArtifacts`).

## UI rendering (`Renderer/UiRendering`)

### One pass, one draw

`UiRenderer::Record(graph, frame, program, bindlessTable)` converts the paint list (`Ui::BuildQuads`) into `GpuUiQuad` instances (112 bytes: rectangle, clip and UV in canvas pixels, premultiplied color and border color, radius, border, glyph pixel range and unit range, kind, bindless texture and sampler), uploads them as a graph upload, and records **one graphics pass with one instanced draw** (6 vertices per quad) in paint order:

- The vertex stage intersects each quad with its clip rectangle in canvas pixels and remaps the UV to the clipped part — exact for axis-aligned rectangles, also under a projection (straight edges stay straight), so there are no discards, scissor changes or batch splits. The scissor covers the target.
- Corners go through `GpuUiDrawConstants::ClipFromCanvas` (96-byte push constants: the matrix, target size, encoding, white scale, flags, canvas opacity). Screen overlays use `ScreenClipFromCanvas`: the RHI is +Y-up (the Vulkan backend flips the viewport) while canvas pixels are +Y-down.
- Glyph pages and images are sampled through the shared bindless table (space 1, the same layout as Forward+ and particles), so textures never split the draw.
- Fragments shade solids (a rounded-box SDF with a one-pixel ramp; the border ring from a second, inset SDF), MSDF glyphs (median of the three channels) and images (premultiplied texel × tint), from the interpolated canvas position. Screen overlays use a one-pixel ramp and `PixelRange = max(1, DistanceRange × screen pixels per atlas texel)` computed on the CPU, so GPU and reference agree exactly. **World canvases** (`UiDrawWorld`) take them from screen-space derivatives: the ramp spans `½(fwidth(x) + fwidth(y))` canvas pixels and the glyph range is msdfgen's `max(½ · UnitRange · (1/fwidth(u) + 1/fwidth(v)), 1)`, so text stays sharp at any distance and angle.
- Output: premultiplied, multiplied by the canvas opacity, blended with `One, OneMinusSourceAlpha` on color and alpha. `UiRenderer::PipelineDesc(format, …, depthFormat)` builds the pipeline per target format; with `D32Float` it tests scene depth with the canonical reverse-Z `GreaterEqual`, never writes it, and draws both faces.

`frame.DpiScale`/`OffsetX/Y` map logical units to framebuffer pixels (split screens); `frame.Clear` makes an overlay-only target; `frame.Atlas` and `frame.Images` declare the sampled textures as graph reads, so this frame's atlas uploads run first. An empty paint list records nothing (unless `Clear`).

### World canvases, depth and mips

- `frame.ClipFromCanvas` (`UI::ClipFromCanvas`) draws a world panel or billboard: quads stay in canvas pixels (offsets must be 0); coverage and glyph ranges come from derivatives (above).
- `frame.Depth` (single-sampled `D32Float` of the drawn extent, and `UiRenderProgram::DepthPipeline`) depth-tests the canvas against the scene; omit it for always-on-top world UI. Canvases are one draw each: sort them back to front with the scene's transparents (`UI::CanvasDistance`). Compose them in the HDR scene with `Linear` encoding and a `LinearScale` for UI white.
- `frame.Opacity` fades the whole canvas (`UI::CanvasFade`).
- `frame.TargetMip` draws one mip level of the target (its extent is the target's `>> mip`).

### Render surfaces (`UiRenderSurfaces`)

Persistent color textures (`Create(UiRenderSurfaceDesc{width, height, format, mips})`: `RGBA8UnormSrgb` by default, sampled back as linear premultiplied color; `ColorAttachment | Sampled | TransferSource`) registered in the shared bindless table with a trilinear clamp sampler. `Record(graph, surface, renderer, program, bindlessTable, UiSurfaceContent)` imports the surface (exported `ShaderRead`) and, **only when the document's `PaintRevision` changed** (or `Force`), draws every mip level from the document — re-rasterized at `DpiScale / 2^mip`, not downsampled, so far-away text stays sharp. `CommitFrame`/`AbortFrame` after executing; `Release(lastUse)` retires a surface through the timeline, `Collect`/`Drain` free it. `PanelPaint(frame, canvasSize)` is the one image quad a world panel draws to show a surface; any material can sample `TextureIndex`/`SamplerIndex` instead.

### Composition (`UiCompositionSettings`)

UI is composited **after** tone mapping, onto the display-encoded image:

| Encoding | Target | UI white | Blending space |
| --- | --- | --- | --- |
| `Srgb` | UNORM holding sRGB values (the post stack's SDR output, BGRA8 swapchains) | 1.0 | sRGB-encoded (conventional UI blending) |
| `Linear` | `*Srgb` formats (hardware encodes after a linear blend) or float intermediates | `LinearScale` | linear |
| `Hdr10` | the post stack's HDR10 output (BT.2020 + PQ) | `PaperWhiteNits` | PQ |
| `ScRgb` | scRGB float (1.0 = 80 nits) | `PaperWhiteNits` | linear |

Each quad's premultiplied color is un-premultiplied, encoded, and premultiplied again (`Ui::Encode`), then blended. `Srgb` into an `*Srgb` format is rejected (double encoding). Match `PaperWhiteNits` with the post stack's so UI white equals SDR scene white.

### Atlas residency (`UiAtlasTextures`)

One RGBA8 UNORM texture per atlas page (linear distance data, alpha 1), registered in the bindless table, sampled with its own linear clamp sampler (no mips). `Update(graph, atlas)` imports every page (exported `ShaderRead`), uploads a new page whole (an initializing write) and afterwards only the row band `GetChangedRows` reports since the last committed revision (a partial `ReadWrite` copy), as graph-scheduled transfers. `CommitFrame` after executing, `AbortFrame` when the graph was dropped (the uploads repeat). `Release(lastUse)` detaches an atlas: views and bindless elements retire after the timeline point, `Collect` frees them (rewriting their bindless elements to the fallback first), `Drain` waits. Switching atlases without `Release` throws.

### CPU reference (`UiRenderReference.h`, namespace `Swim::Render::Ui`)

`BuildQuads`, `BuildDrawConstants`/`BuildCanvasDrawConstants`, `ShadeQuad` (with an optional `Footprint`), `Encode`, `Blend`, `Rasterize` (screen overlays: pixel centres in the clipped `[x0, x1) × [y0, y1)`, the top-left rule for axis-aligned edges) and `SampleBilinear` (Vulkan's texel-centre, clamp-to-edge convention) define every rule `UiQuad.slang` evaluates, line for line. For world canvases, `CanvasAt(constants, px, py)` inverts the canvas-plane homography at a pixel centre and returns the analytic footprint (the derivatives' counterpart), and `RasterizeProjected` shades every covered pixel with it.

## Minimal consumer

```cpp
#include "Engine/Systems/Renderer/UiRendering/UiRenderSurfaces.h"
#include "Engine/Systems/UI/UiCanvasRouter.h"
#include "Engine/Systems/UI/UiWidgets.h"
#include "Engine/Systems/UiInput/UiInputBridge.h"

// Setup (fonts come from the asset/IO layer; no OS lookup).
auto theme = std::make_shared<Swim::UI::UiTheme>();
theme->Fonts = std::make_shared<Swim::Text::FontCollection>(
	std::vector<std::shared_ptr<const Swim::Text::FontFace>>{ latinFace, cjkFace, emojiFace });
Swim::UI::UiDocument hud;     // A screen overlay.
Swim::UI::UiDocument terminal; // A world panel in the level.
hud.SetTheme(theme);
terminal.SetTheme(theme);
const auto play = Swim::UI::CreateButton(hud, hud.GetRoot(), "Play");
const auto volume = Swim::UI::CreateSlider(hud, hud.GetRoot(), { .Max = 100.0f, .Step = 1.0f, .ShowValue = true });
const auto power = Swim::UI::CreateToggle(terminal, terminal.GetRoot(), "Power");
Swim::UI::UiCanvasRouter router;
const auto hudCanvas = router.Add({ &hud });
const auto terminalCanvas = router.Add({ &terminal, Swim::UI::UiCanvasMode::WorldPanel });
Swim::UI::UiInputBridge input({ .FramebufferScale = pixelDensity, .Gamepad = padId });
Swim::Text::GlyphAtlas atlas;
Swim::Render::UiAtlasTextures atlasTextures(device, bindlessTable);
Swim::Render::UiRenderer uiRenderer;

// Per frame, after InputSystem::AdvanceFrame.
hud.Layout({ framebufferWidth, framebufferHeight }, dpiScale);
terminal.Layout({ 512, 256 });
const auto toWorld = Swim::UI::CanvasToWorld(Swim::UI::UiCanvasMode::WorldPanel, terminalPlacement, { 512, 256 });
router.SetCamera(camera);
router.SetWorldPlacement(terminalCanvas, toWorld, { 512, 256 });
const auto consumed = input.Apply(inputSystem, router, &camera, {}, deltaSeconds); // PointerOverUi / KeyboardCaptured for the game.
for (const auto& event : hud.DrainEvents()) { /* Click, ValueChanged, ValueCommitted, TextChanged, Submit, ... */ }
hud.Update(deltaSeconds); // Transitions, toggle knobs, overlay scroll bars, held step buttons.
terminal.Update(deltaSeconds);
hud.Layout({ framebufferWidth, framebufferHeight }, dpiScale);
terminal.Layout({ 512, 256 });
const auto atlasFrame = atlasTextures.Update(graph, atlas);
Swim::Render::UiRenderFrame world;  // Into the HDR scene, depth tested, before tone mapping.
world.Paint = terminal.Paint(atlas);
world.Target = sceneColor;
world.Depth = sceneDepth;
world.ClipFromCanvas = Swim::UI::ClipFromCanvas(toWorld, camera);
world.Composition.Encoding = Swim::Render::UiOutputEncoding::Linear;
world.Atlas = &atlasFrame;
uiRenderer.Record(graph, world, uiProgram, bindlessTable.GetTable());
Swim::Render::UiRenderFrame overlay; // After the post stack.
overlay.Paint = hud.Paint(atlas);
overlay.Target = postOutput;
overlay.DpiScale = dpiScale;
overlay.Atlas = &atlasFrame;
uiRenderer.Record(graph, overlay, uiProgram, bindlessTable.GetTable());
const auto done = executor.Execute(graph.Compile());
atlasTextures.CommitFrame();
if (router.WantsTextInput()) { /* StartTextInput + SetTextInputArea(*router.GetTextInputRect() / pixelDensity) */ }
```

## Build and test

Windows (PowerShell, repository root). SheenBidi and libunibreak must have been downloaded once (the previous checkpoint's `cmake --preset windows-debug -DFETCHCONTENT_FULLY_DISCONNECTED=OFF`); this checkpoint adds no dependency.

```powershell
& .\scripts\build-windows-soft.ps1 -Debug
if ($LASTEXITCODE -ne 0) { throw "Debug build/default tests failed" }

& .\build\windows-debug\SwimTests.exe --filter=Text --filter=UI --filter=UiInput --filter=Render.Ui --filter=Input.TextEditing --filter=ShaderCompiler.UiLayout --verbose
if ($LASTEXITCODE -ne 0) { throw "Text/UI tests failed" }

$env:SWIM_RUN_RHI_SMOKE = "1"
foreach ($profile in "core", "sync", "gpu", "all") {
	$env:SWIM_RHI_VALIDATION = $profile
	& .\build\windows-debug\SwimTests.exe --filter=RHI.Vulkan.Smoke.UiRendererMatchesTheCpuReference --filter=RHI.Vulkan.Smoke.UiWorldCanvasesMatchTheCpuReference --verbose
	if ($LASTEXITCODE -ne 0) { throw "UI smokes failed ($profile)" }
}
Remove-Item Env:SWIM_RUN_RHI_SMOKE, Env:SWIM_RHI_VALIDATION

python .\scripts\verify-build-layout.py
```

Linux:

```bash
cmake --preset linux-debug
cmake --build --preset linux-debug --target SwimTests SwimTextUiPublicHeaders --parallel
./build/linux-debug/SwimTests --filter=Text --filter=UI --filter=UiInput --filter=Render.Ui --filter=Input.TextEditing --filter=ShaderCompiler.UiLayout
SWIM_RUN_RHI_SMOKE=1 SWIM_RHI_VALIDATION=core ./build/linux-debug/SwimTests --filter=RHI.Vulkan.Smoke.UiRendererMatchesTheCpuReference --filter=RHI.Vulkan.Smoke.UiWorldCanvasesMatchTheCpuReference
python3 scripts/verify-build-layout.py
```

The focused command runs **82 cases** (`--filter=Render.Ui` selects `Render.Ui.Reference`, `Render.Ui.Renderer`, `Render.Ui.AtlasTextures` and `Render.Ui.Surfaces`; `--filter=UI` includes `UI.Controls`, `UI.Theme`, `UI.Navigation`, `UI.Canvas` and `UI.CanvasRouter`).

The screen smoke prints one line per target (`[ui sdr]`, `[ui hdr10]`, `[ui scrgb]`, `[ui linear-srgb]`, `[ui sdr-grown]`) with compared/lit/outlier counts, then `[ui 1080p] … draw … ms`. The world smoke prints `[ui world-ortho]`, `[ui world-faded]`, `[ui world-occluded]` (0 lit: scene depth hides the panel), `[ui world-rotated]`, `[ui world-billboard]`, `[ui world-widgets]`, `[ui world-widgets-billboard]`, `[ui surface mip 0]`, `[ui surface mip 2]` and `[ui surface panel]`, then a total. 1:1 frames allow 2.5 LSB and 1 % outliers; perspective frames compare hardware derivatives with analytic footprints and allow 6 LSB and 3 %. Record those lines and the pass times in the validation record.

In the container both smokes passed on a source-built SwiftShader with 0 outliers in every frame ([record](validation/Item79-Controls-2026-09-25.md)); SwiftShader lacks `shaderDrawParameters`, so those runs used a variant of `UiQuad.slang` with `SV_VulkanVertexID`/`SV_VulkanInstanceID` and no validation layer. The committed shader keeps `SV_VertexID`/`SV_InstanceID` like the other RHI programs, so the desktop runs under the four validation profiles are still the gate.

## Remaining work (item 79 stays open)

1. Run both native UI smokes on the desktop under all four validation profiles; record the 1080p UI budget.
2. Runtime wiring (with item 56): construct `UiAtlasTextures`/`UiRenderer`/`UiRenderSurfaces` where the modern renderer presents frames; draw world canvases with the scene's transparents (sorted, fogged — world UI does not apply fog yet); a scene/ECS canvas component (document, placement mode, transform or target surface, size, DPI, interactivity) feeding `UiCanvasRouter` and the renderer; migrate sandbox UI/text and retire `FontPool`/legacy text.
3. More controls: dropdowns/combo boxes, list views with virtualization, radio groups, tooltips, context menus, modal dialogs; slider value editing by typing.
4. Background (cross-frame) atlas population and atlas page compaction/eviction for very large glyph sets.
5. Visual (not logical) cursor movement in bidi text, double-click word selection, undo/redo, password fields, rich text spans (per-range fonts/colors), tab stops.
6. Layout: wrapping flex lines, baseline alignment, grid; rounded clipping of children (clips stay rectangles).

## Implementation references

- [UAX #9 Unicode Bidirectional Algorithm](https://www.unicode.org/reports/tr9/), [SheenBidi](https://github.com/Tehreer/SheenBidi)
- [UAX #14 Line Breaking](https://www.unicode.org/reports/tr14/), [UAX #29 Text Segmentation](https://www.unicode.org/reports/tr29/), [libunibreak](https://github.com/adah1972/libunibreak)
- [UAX #24 Script property](https://www.unicode.org/reports/tr24/)
- [HarfBuzz buffer properties](https://harfbuzz.github.io/setting-buffer-properties.html), [clusters](https://harfbuzz.github.io/clusters.html)
- [msdfgen](https://github.com/Chlumsky/msdfgen) (median, screen pixel range)
- [SDL3 text input](https://wiki.libsdl.org/SDL3/SDL_SetTextInputArea)
