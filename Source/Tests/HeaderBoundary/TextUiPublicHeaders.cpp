// Compiles with only Source/ on the include path, without any text library.
#include "Engine/Systems/Renderer/UiRendering/UiAtlasTextures.h"
#include "Engine/Systems/Renderer/UiRendering/UiRenderSurfaces.h"
#include "Engine/Systems/Renderer/UiRendering/UiRenderer.h"
#include "Engine/Systems/Text/FontCollection.h"
#include "Engine/Systems/Text/FontFace.h"
#include "Engine/Systems/Text/GlyphAtlas.h"
#include "Engine/Systems/Text/TextLayout.h"
#include "Engine/Systems/Text/TextSegmentation.h"
#include "Engine/Systems/Text/Utf8.h"
#include "Engine/Systems/UI/UiCanvas.h"
#include "Engine/Systems/UI/UiCanvasRouter.h"
#include "Engine/Systems/UI/UiDocument.h"
#include "Engine/Systems/UI/UiTheme.h"
#include "Engine/Systems/UI/UiWidgets.h"
#include "Engine/Systems/UiInput/UiInputBridge.h"

#include <type_traits>

static_assert(!std::is_copy_constructible_v<Swim::Text::FontFace>);
static_assert(!std::is_copy_constructible_v<Swim::UI::UiDocument>);
static_assert(std::is_standard_layout_v<Swim::UI::UiPaintQuad>);
static_assert(std::is_standard_layout_v<Swim::Render::GpuUiQuad>);
static_assert(!std::is_copy_constructible_v<Swim::Render::UiAtlasTextures>);
static_assert(std::is_copy_constructible_v<Swim::Text::TextLayout>); // Immutable value; cheap to share by pointer.
static_assert(!std::is_copy_constructible_v<Swim::Render::UiRenderSurfaces>);
static_assert(std::is_copy_constructible_v<Swim::UI::UiTheme>); // Themes are values; documents share them immutably.
static_assert(sizeof(Swim::Render::GpuUiDrawConstants) == 96);
