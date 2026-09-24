# Text and retained UI

2026-09-24 — item 79, first runtime checkpoint.

This checkpoint provides text services and a retained UI CPU foundation. It produces paint data; it does not yet draw through the modern RHI or replace the sandbox's legacy text/UI. Item 79 remains open.

## Text ownership and shaping

`Systems/Text/FontFace` accepts a byte span and copies it. Load those bytes through the application's existing asset/IO service; there is no filesystem access, OS font lookup, singleton or renderer dependency in the module. Each face owns its FreeType library/face and immutable HarfBuzz font. The constructor rejects malformed, bitmap-only, non-SFNT or non-Unicode faces. Collection face indices are supported; variable fonts use their default instance (no variation-axis API yet).

`Shape(utf8, logicalSize, options)` shapes **one horizontal script/direction run**. It returns visual-order glyph IDs, UTF-8 byte clusters, advances/offsets, font metrics, resolved direction and a missing-glyph count. Options select direction, ISO 15924 script, BCP 47 language, kerning and standard/contextual ligatures. Empty language means `und`, independent of process locale. Invalid UTF-8 is replaced by HarfBuzz with U+FFFD. Input is bounded to 1 MiB per run; logical font sizes must be finite and in `(0, 16384]`.

Coordinates are font coordinates (positive Y up); `UiDocument` converts these to top-down screen coordinates. `GetMetrics` returns ascender, negative descender and line height. Shaping uses the font's unhinted OpenType advances scaled to logical units. Concurrent const face calls are supported; FreeType outline generation is serialized per face. There is no second thread pool.

HarfBuzz does not perform paragraph bidi ordering, script itemization, fallback or Unicode line breaking for us. Callers must segment such paragraphs before using this run API. The current UI label API supports a homogeneous script/direction per hard line and handles LF/CRLF, including an empty trailing line. It does not claim mixed-script paragraph support. Keep the original UTF-8 bytes when using the returned clusters; glyph indices are not caret positions.

## MSDF cache

`GlyphAtlas` keys entries by retained font identity and glyph ID. Its configuration fixes page size, maximum pages, raster em size and full signed-distance range in texels. Defaults: 512² pages, up to 8 pages, 48 texels/em, range 4. Descriptor validation caps texture storage at 256 MiB. Pages allocate only on demand.

FreeType outlines are converted through line, quadratic and cubic callbacks into msdfgen shapes, normalized and oriented, edge-colored and rasterized. The bitmap is flipped into top-down RGB8 **linear UNORM distance data**, never sRGB. Glyph quad bounds include distance padding. A separate one-texel packing gutter prevents adjacent glyph contamination. Whitespace is cached with `NoAtlasPage`; it keeps its shaping advance but allocates no texels.

- `Get(face, glyph)` returns an `AtlasGlyph` by value. It retains the face, preventing pointer reuse from aliasing a different font.
- `GetPage(page)` exposes a read-only byte span and monotonically increasing page revision. Cache hits do not increase revisions. Consumers can compare revisions to decide when uploads are necessary. Spans remain valid until atlas destruction; subsequent additions can update their pixels, so do not read them concurrently with mutation.
- Placements never repack or evict. Glyph UVs remain stable for the atlas's lifetime. Keep the atlas alive while its paint records are used.
- Out-of-range glyphs, oversized glyphs and full page budgets report exceptions. Failed requests do not change existing glyph placements, pixels or packing state.
- Atlas mutation is single-owner/external synchronization. Generation is synchronous and can be expensive. Prewarm known runs with `atlas.Get(face, glyph.Glyph)` before time-critical frames. Async population and GPU lifetime management remain future consumer work.

The GPU consumer must decode the RGB median around 0.5, use the supplied distance range and screen derivatives for coverage, and multiply the premultiplied quad color by coverage. Store/upload these pages as linear data. Do not add ordinary box-filtered mipmaps without a distance-field sampling policy. Upload completion and timeline-safe texture/descriptor retirement belong to the future rendering adapter.

## Retained UI contract

`Systems/UI/UiDocument` has process-unique node IDs and explicit ownership; IDs are never reused. Invalid or foreign IDs throw for operations requiring a node. `Remove` returns false for absent IDs/root, removes a whole subtree otherwise, and clears focus/capture safely. `Reparent` rejects cycles/root movement and appends to a different parent in paint/tab order. Limits are 65,536 nodes and 256 levels.

`Layout(framebufferSize, dpiScale)` converts the canvas to logical units. Root bounds always fill that canvas. Node sizes are content plus padding when Auto, or the preferred logical/percentage size, clamped to min/max. Preferred sizes include padding. Positive margins sit outside the node; a stack's gap is between participating children. Absolute children use top-left offsets relative to parent content, do not participate in intrinsic measurement and do contribute to scroll extent. Overlay siblings share the same origin. Text and child flow share the content origin; place them in separate nodes when they should stack.

Percentages are fractions `[0, 1]` of the **final parent content size**. To break intrinsic sizing cycles, a percentage on an Auto parent axis contributes zero to that parent's measurement, then resolves at arrangement time; resulting overflow follows normal clipping rules. Row/column layouts do not grow/shrink children or implicitly stretch them. Anchors, aspect ratios and flex alignment are not present yet.

`Clip` restricts descendants/text to the content box intersected with ancestor clips; the node background uses its outer box. Scrolling is enabled by `Clip`, clamped against measured content extents and applied to both text and children. Without clipping the scroll offset resolves to zero. Hidden nodes and descendants are omitted from measure/paint/input and have zero query bounds after layout. Disabled subtrees still paint but cannot receive pointer/focus input.

Mutations invalidate layout. Call `Layout` before bounds, paint, hit-testing or tab traversal; stale access throws instead of returning last-frame hit regions. Repeating Layout on an unchanged document/canvas is a no-op. Text shaping is retained, and setting identical text/font/size/direction does not invalidate it. Dirty layout currently recomputes the document; Paint rebuilds the ordered list.

`Paint(atlas)` emits `UiPaintQuad`s in stable parent-before-child and sibling order. A quad carries logical bounds, a logical clip rectangle, premultiplied linear RGBA, and optionally an atlas page/UV/distance range. Zero-alpha or fully clipped quads are omitted. The returned vector belongs to the document and is replaced by the next Paint; copy it if a renderer needs a separate frame snapshot. Calls can populate the atlas and can fail on atlas budget exhaustion. There is no draw submission, GPU upload, image widget or HDR conversion in this module yet.

## Input contract

Pass **framebuffer-pixel** coordinates to `HitTest`/pointer methods. The platform adapter must convert window logical coordinates when necessary. Paint and layout coordinates remain logical, so the renderer applies the same DPI scale.

- Hit-testing walks reverse paint order and respects clipping, visibility, enablement and `HitTest` flags.
- Primary pointer down captures a node; release always targets the captured node and clicks only when the pointer is over that same node. `CancelPointer` cancels capture without a click.
- Down focuses a focusable target or clears focus. `FocusNext` walks stable hierarchy order, wraps and supports reverse Tab. `ActivateFocused` supplies keyboard activation. Focusable offscreen scroll children remain in tab order; automatic scroll-to-focus remains widget work.
- Disabling/hiding/removing a focused or captured node (including an ancestor) emits Blur/Cancel/Leave as applicable. Invalid IDs in removal events intentionally identify the removed node; check `Contains` before reading it.
- Events are queued (`DrainEvents`), avoiding mutation during callbacks. There is no DOM capture/bubble propagation or multi-touch handling yet.
- The future platform adapter should call `CancelPointer` and `Focus({})` on application focus loss and drain events every input frame. Hover is updated on pointer samples; resample the current pointer after layout changes when hover feedback must track moving widgets.

## Minimal consumer

The caller supplies `fontBytes`, framebuffer dimensions and input. These are not process globals.

```cpp
#include "Engine/Systems/UI/UiDocument.h"

auto face = std::make_shared<Swim::Text::FontFace>(fontBytes);
Swim::Text::GlyphAtlas atlas;
Swim::UI::UiDocument document;
const auto button = document.Create(document.GetRoot());
Swim::UI::UiStyle style;
style.Width = Swim::UI::UiLength::Pixels(220.0f);
style.Height = Swim::UI::UiLength::Pixels(48.0f);
style.Padding = { 12.0f, 8.0f, 12.0f, 8.0f };
style.Clip = true;
style.HitTest = true;
style.Focusable = true;
style.Background = { 0.04f, 0.08f, 0.18f, 1.0f };
document.SetStyle(button, style);
document.SetText(button, face, "Play", 24.0f);
document.Layout({ 1920.0f, 1080.0f }, 1.5f);
const auto& paint = document.Paint(atlas);
// A future UI renderer consumes paint and the changed atlas pages.
```

## Build and test

From the repository root in PowerShell, using the existing Ninja/MSVC Debug setup:

```powershell
& .\scripts\build-windows-soft.ps1 -Debug
if ($LASTEXITCODE -ne 0) { throw "Debug build/default tests failed" }

& .\build\windows-debug\SwimTests.exe --filter=Text --filter=UI --verbose
if ($LASTEXITCODE -ne 0) { throw "Text/UI tests failed" }

python .\scripts\verify-build-layout.py
if ($LASTEXITCODE -ne 0) { throw "Build layout verification failed" }
```

If the build script uses its Visual Studio fallback, use `build\windows-vs\Debug\SwimTests.exe` for the focused run. The existing soft build requires the previously introduced text dependencies to be cached; no new dependency is introduced here. On a machine without that cache, use `build-windows-clean.ps1 -Debug` for the initial build.

The new suites are in the normal default corpus. Repeated `--filter` arguments select their union. This focused command must report **20 cases**, not zero: 3 existing `Text.Dependencies`, 4 `Text.Font`, 4 `Text.Atlas`, and 9 `UI.*`. These are CPU tests; no `SWIM_RUN_RHI_SMOKE` or GPU validation profile is needed. Launching the sandbox does not exercise this checkpoint.

Linux with the existing configured dependency-enabled build:

```bash
cmake --preset linux-debug
cmake --build --preset linux-debug --target SwimTests SwimTextUiPublicHeaders --parallel
./build/linux-debug/SwimTests --filter=Text --filter=UI --verbose
python3 scripts/verify-build-layout.py
```

Local verification performed for this change:

- GCC 13.3 Debug, exact pinned FreeType 2.14.3, HarfBuzz 14.5.0 and msdfgen 1.13: **20 cases / 1,932 checks pass**. A temporary standalone CMake harness compiled the unchanged Swim test framework, the new Text/UI sources, and the Text/UI suites against those dependencies. This is targeted source validation, not a full engine build.
- The same first-party sources/cases pass with AddressSanitizer and UndefinedBehaviorSanitizer. Third-party libraries were not sanitizer-instrumented. LeakSanitizer cannot run under this environment's process tracing, so leak checking was disabled for that run.
- The atlas test compares distance signs and top-down orientation against independently rasterized FreeType coverage for L/O/A/g, ignoring partially covered contour samples. Other tests cover contextual Arabic/ligatures, combining and non-BMP clusters, malformed input, concurrent shaping, stable atlas entries, capacity failures, DPI, intrinsic/percentage layout, clipping and focus/capture lifecycle.
- `python scripts/verify-build-layout.py` passes. The repository's `SwimTextUiPublicHeaders` CMake target compiles with only `Source/` on its include path using the offline configuration.
- Full default engine suites, MSVC/Windows builds and desktop execution remain to run on the development machine. No new GPU pass exists to validate yet.

## Next checkpoint

1. RenderGraph UI pass, staged atlas uploads, bindless atlas/image residency, batched instanced quads, scissor/clip translation and defined sRGB/HDR output composition; native image smoke and timeline lifetime tests.
2. Paragraph script/bidi segmentation, cluster-aware fallback and line wrapping; then caret/selection, input-field editing and IME, with explicit platform text-input integration.
3. Anchors/aspect/flex alignment, widget wrappers, incremental subtree layout/paint invalidation, async glyph population and runtime migration.

Do not check off item 79 or claim sandbox integration until the relevant work and desktop validation exist. The existing world-renderer migration remains item 56.

## Suggested commit

Subject:

```text
Add text services and retained UI foundation
```

Description:

```text
Add owned FreeType faces, HarfBuzz shaping and bounded MSDF atlas pages.
Implement retained UI layout, clipping, scrolling, paint data and input/focus routing.
Wire build boundaries and focused regression coverage, and update the architecture plan.
Leave GPU UI rendering and advanced text/widget integration for the next checkpoint.
```

## Implementation references

- [HarfBuzz buffer properties](https://harfbuzz.github.io/setting-buffer-properties.html)
- [HarfBuzz OpenType font functions](https://harfbuzz.github.io/harfbuzz-hb-ot-font.html)
- [FreeType outline processing](https://freetype.org/freetype2/docs/reference/ft2-outline_processing.html)
- [msdfgen](https://github.com/Chlumsky/msdfgen)
