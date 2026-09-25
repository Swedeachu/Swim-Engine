# Text and retained UI

Critical-path item **79** (Phase 20). Checkpoints:

- **2026-09-24 — foundation:** font faces, horizontal run shaping, the MSDF glyph atlas and the retained `UiDocument` (layout, paint records, pointer/focus input).
- **2026-09-25 — paragraphs, editing and rendering (this document's current state):** paragraph layout (bidi, script itemization, cluster fallback, line breaking, alignment), caret/selection/IME text editing, flex/anchor/aspect layout, rounded/bordered solids and nine-slice images, measure and paint caches, parallel atlas prewarm, a widget layer, the Input → UI bridge, and `Renderer/UiRendering`: the RenderGraph UI pass with atlas residency and SDR/HDR composition, with a CPU reference and a native smoke.

Item 79 stays **open** until the native smoke has passed on the desktop and the runtime draws its UI through it. Sandbox migration waits for the modern renderer to present frames (item 56); the legacy text/UI remains active.

## Module map

| Path | Role | Depends on |
| --- | --- | --- |
| `Systems/Text` | `FontFace`, `FontCollection`, `GlyphAtlas`, `TextSegmentation`, `TextLayout`, `Utf8` | FreeType, HarfBuzz, msdfgen, SheenBidi, libunibreak (private) |
| `Systems/UI` | `UiDocument` (tree/API, layout, paint, input, editing units), `UiWidgets` | Text |
| `Systems/UiInput` | `UiInputBridge`: `Input::InputSystem` frame → `UiDocument` | UI, Input |
| `Renderer/UiRendering` | `UiRenderer`, `UiAtlasTextures`, `UiRenderReference` (`Ui::` CPU definition), records/bindings/settings | RenderGraph, RHI contract, Resources, UI/Text headers |
| `Shaders/Slang/Ui` | `UiRecords.slang`, `UiQuad.slang` (`SwimUiQuad`) | — |

`scripts/verify-build-layout.py` enforces the boundaries: text libraries stay inside `Systems/Text`/`Systems/UI` and the Text tests, no public header names a library type, Text/UI never include renderer/scene/platform headers, `UiRendering` includes only RenderGraph/RHI/Resources renderer headers, no other renderer module includes UI, and `UiInput` includes only UI, Text and Input.

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

### Widgets (`UiWidgets.h`)

`CreateLabel`, `CreateButton` (`DefaultButtonStyle`: padding, rounded, centered text, hit-testable and focusable), `CreateImage`, `CreateScrollView` (clipped column) and `CreateTextField` (clipped editor) create ordinary nodes. `UiButtonStates` turns drained Enter/Leave/Press/Release/Cancel/Focus/Blur events into hover/pressed/focus-ring colors through paint-only style changes.

### Input bridge (`Systems/UiInput/UiInputBridge`)

`UiInputBridge::Apply(inputSystem, document)`, once per frame after `InputSystem::AdvanceFrame` and a `Layout`:

- window → framebuffer pixels by `FramebufferScale`; primary-button press/release; wheel notches × `WheelStep` (positive wheel scrolls up);
- key presses **including operating-system repeats** and committed text, interleaved in event order through the new `InputSystem::GetTextEditEvents()` (`GetKeyPresses()` lists the presses alone); Control or, for macOS, Super as the shortcut modifier; keys reach the document only while a node has focus (Tab starts traversal when `TabStartsNavigation`);
- IME composition only when `InputSystem::HasTextCompositionUpdate()` (a preedit persists across frames without events; SDL's code-point cursor is converted to bytes);
- application focus loss cancels pointer capture and clears UI focus.

It returns `PointerOverUi`, `KeyboardCaptured` and `WantsTextInput` so the game can ignore input the UI consumed. Starting/stopping platform text input and `WindowSystem::SetTextInputArea` (new, SDL3's `SDL_SetTextInputArea`) stay with the application.

## Glyph atlas

Unchanged contract (single owner, stable UVs, no eviction, page revisions, `GetChangedRows`), plus:

- `Prewarm(face, glyphs, parallelFor)`: distance fields of the missing glyphs are generated through a caller-supplied parallel-for (for example `JobSystem::ParallelFor`), then packed serially in the given order — pixel-identical to calling `Get` in that order. A failed generation inserts nothing and rethrows; `Contains(face, glyph)` checks the cache.
- `UiDocument::PrewarmGlyphs(atlas, parallelFor)` prewarms every glyph a laid-out document shows, so the following `Paint` only looks glyphs up (use before time-critical frames). True background population (while frames render) is still future work.

## UI rendering (`Renderer/UiRendering`)

### One pass, one draw

`UiRenderer::Record(graph, frame, program, bindlessTable)` converts the paint list (`Ui::BuildQuads`) into `GpuUiQuad` instances (112 bytes: rectangle, clip and UV in framebuffer pixels, premultiplied color and border color, radius, border, glyph pixel range, kind, bindless texture and sampler), uploads them as a graph upload, and records **one graphics pass with one instanced draw** (6 vertices per quad) in paint order:

- The vertex stage intersects each quad with its clip rectangle and remaps the UV to the clipped part — exact for axis-aligned rectangles, so there are no discards, scissor changes or batch splits. The scissor covers the target.
- Clip space: the RHI is +Y-up (the Vulkan backend flips the viewport), while UI rectangles are +Y-down framebuffer pixels, so the vertex stage negates NDC y. The fragment stage's `SV_Position` is already in top-left-origin pixels.
- Glyph pages and images are sampled through the shared bindless table (space 1, the same layout as Forward+ and particles), so textures never split the draw.
- Fragments shade solids (a rounded-box SDF with a one-pixel ramp; the border ring from a second, inset SDF), MSDF glyphs (median of the three channels, `PixelRange = max(1, DistanceRange × screen pixels per atlas texel)`, computed on the CPU so GPU and reference agree), and images (premultiplied texel × tint).
- Output: premultiplied, blended with `One, OneMinusSourceAlpha` on color and alpha. `UiRenderer::PipelineDesc(format, …)` builds the pipeline per target format.

`frame.DpiScale`/`OffsetX/Y` map logical units to framebuffer pixels (split screens); `frame.Clear` makes an overlay-only target; `frame.Atlas` and `frame.Images` declare the sampled textures as graph reads, so this frame's atlas uploads run first. An empty paint list records nothing (unless `Clear`).

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

`BuildQuads`, `BuildDrawConstants`, `ShadeQuad`, `Encode`, `Blend`, `Rasterize` (pixel centres in the clipped `[x0, x1) × [y0, y1)`, the top-left rule for axis-aligned edges) and `SampleBilinear` (Vulkan's texel-centre, clamp-to-edge convention) define every rule `UiQuad.slang` evaluates, line for line.

## Minimal consumer

```cpp
#include "Engine/Systems/Renderer/UiRendering/UiRenderer.h"
#include "Engine/Systems/UI/UiWidgets.h"
#include "Engine/Systems/UiInput/UiInputBridge.h"

// Setup (fonts come from the asset/IO layer; no OS lookup).
auto fonts = std::make_shared<Swim::Text::FontCollection>(
	std::vector<std::shared_ptr<const Swim::Text::FontFace>>{ latinFace, cjkFace, emojiFace });
Swim::UI::UiDocument ui;
Swim::Text::GlyphAtlas atlas;
Swim::UI::UiButtonStates buttonStates;
const auto play = Swim::UI::CreateButton(ui, ui.GetRoot(), fonts, "Play", 24.0f);
buttonStates.Track(play);
const auto name = Swim::UI::CreateTextField(ui, ui.GetRoot(), fonts, 18.0f);
Swim::UI::UiInputBridge input({ /* FramebufferScale */ pixelDensity });
Swim::Render::UiAtlasTextures atlasTextures(device, bindlessTable);
Swim::Render::UiRenderer uiRenderer;

// Per frame, after InputSystem::AdvanceFrame and the post stack.
ui.Layout({ framebufferWidth, framebufferHeight }, dpiScale);
const auto consumed = input.Apply(inputSystem, ui); // PointerOverUi / KeyboardCaptured for the game.
const auto events = ui.DrainEvents();
buttonStates.Apply(ui, events);
for (const auto& event : events) { /* Click, TextChanged, Submit, ... */ }
ui.Layout({ framebufferWidth, framebufferHeight }, dpiScale);
const auto& paint = ui.Paint(atlas);
const auto atlasFrame = atlasTextures.Update(graph, atlas);
Swim::Render::UiRenderFrame frame;
frame.Paint = paint;
frame.Target = postOutput;          // RGBA8Unorm (sRGB values) or RGBA16Float (HDR10/scRGB).
frame.DpiScale = dpiScale;
frame.Composition.Encoding = Swim::Render::UiOutputEncoding::Srgb;
frame.Atlas = &atlasFrame;
uiRenderer.Record(graph, frame, uiProgram, bindlessTable.GetTable());
const auto done = executor.Execute(graph.Compile());
atlasTextures.CommitFrame();
if (ui.WantsTextInput()) { /* WindowSystem::StartTextInput + SetTextInputArea(ui.GetTextInputRect() / pixelDensity) */ }
```

## Build and test

Windows (PowerShell, repository root). **Two new text dependencies (SheenBidi, libunibreak) must be downloaded once:** the soft build is disconnected, so run the clean build, or configure once with downloads enabled.

```powershell
cmake --preset windows-debug -DFETCHCONTENT_FULLY_DISCONNECTED=OFF   # once, fetches SheenBidi + libunibreak
& .\scripts\build-windows-soft.ps1 -Debug
if ($LASTEXITCODE -ne 0) { throw "Debug build/default tests failed" }

& .\build\windows-debug\SwimTests.exe --filter=Text --filter=UI --filter=UiInput --filter=Render.Ui --filter=Input.TextEditing --filter=ShaderCompiler.UiLayout --verbose
if ($LASTEXITCODE -ne 0) { throw "Text/UI tests failed" }

$env:SWIM_RUN_RHI_SMOKE = "1"
foreach ($profile in "core", "sync", "gpu", "all") {
	$env:SWIM_RHI_VALIDATION = $profile
	& .\build\windows-debug\SwimTests.exe --filter=RHI.Vulkan.Smoke.UiRendererMatchesTheCpuReference --verbose
	if ($LASTEXITCODE -ne 0) { throw "UI smoke failed ($profile)" }
}
Remove-Item Env:SWIM_RUN_RHI_SMOKE, Env:SWIM_RHI_VALIDATION

python .\scripts\verify-build-layout.py
```

Linux:

```bash
cmake --preset linux-debug
cmake --build --preset linux-debug --target SwimTests SwimTextUiPublicHeaders --parallel
./build/linux-debug/SwimTests --filter=Text --filter=UI --filter=UiInput --filter=Render.Ui --filter=Input.TextEditing --filter=ShaderCompiler.UiLayout
SWIM_RUN_RHI_SMOKE=1 SWIM_RHI_VALIDATION=core ./build/linux-debug/SwimTests --filter=RHI.Vulkan.Smoke.UiRendererMatchesTheCpuReference
python3 scripts/verify-build-layout.py
```

`--filter=Render.Ui` selects `Render.Ui.Reference`, `Render.Ui.Renderer` and `Render.Ui.AtlasTextures`; the focused command runs **57 cases**. The smoke prints one line per target (`[ui sdr]`, `[ui hdr10]`, `[ui scrgb]`, `[ui linear-srgb]`, `[ui sdr-grown]`) with compared/lit/outlier counts, then `[ui 1080p] … draw … ms`. Record those and the pass time in the validation record.

In the container the smoke passed on a source-built SwiftShader with 0 outliers in all five frames ([record](validation/Item79-2026-09-25.md)); SwiftShader lacks `shaderDrawParameters`, so that run used a variant of `UiQuad.slang` with `SV_VulkanVertexID`/`SV_VulkanInstanceID`. The committed shader keeps `SV_VertexID`/`SV_InstanceID` like the other RHI programs, so the desktop run is still the gate.

## Remaining work (item 79 stays open)

1. Run the native smoke on the desktop under all four validation profiles; record the 1080p UI budget.
2. Runtime wiring: construct `UiAtlasTextures`/`UiRenderer` where the modern renderer presents frames (with item 56), migrate sandbox UI/text and retire `FontPool`/legacy text.
3. Background (cross-frame) atlas population and atlas page compaction/eviction for very large glyph sets; world-space text instances.
4. Visual (not logical) cursor movement in bidi text, double-click word selection, undo/redo, password fields, rich text spans (per-range fonts/colors), tab stops.
5. Layout: wrapping flex lines, baseline alignment, grid; rounded clipping of children (clips stay rectangles).

## Implementation references

- [UAX #9 Unicode Bidirectional Algorithm](https://www.unicode.org/reports/tr9/), [SheenBidi](https://github.com/Tehreer/SheenBidi)
- [UAX #14 Line Breaking](https://www.unicode.org/reports/tr14/), [UAX #29 Text Segmentation](https://www.unicode.org/reports/tr29/), [libunibreak](https://github.com/adah1972/libunibreak)
- [UAX #24 Script property](https://www.unicode.org/reports/tr24/)
- [HarfBuzz buffer properties](https://harfbuzz.github.io/setting-buffer-properties.html), [clusters](https://harfbuzz.github.io/clusters.html)
- [msdfgen](https://github.com/Chlumsky/msdfgen) (median, screen pixel range)
- [SDL3 text input](https://wiki.libsdl.org/SDL3/SDL_SetTextInputArea)
