// Compiles with only Source/ on the include path, without any text library.
#include "Engine/Systems/Text/FontFace.h"
#include "Engine/Systems/Text/GlyphAtlas.h"
#include "Engine/Systems/UI/UiDocument.h"

#include <type_traits>

static_assert(!std::is_copy_constructible_v<Swim::Text::FontFace>);
static_assert(!std::is_copy_constructible_v<Swim::UI::UiDocument>);
static_assert(std::is_standard_layout_v<Swim::UI::UiPaintQuad>);
