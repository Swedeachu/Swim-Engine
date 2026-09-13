#include "Engine/Systems/Renderer/RenderGraph/RenderGraph.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraphExecutor.h"
#include <type_traits>

static_assert(!std::is_copy_constructible_v<Swim::Render::RenderGraph>);
static_assert(!std::is_copy_constructible_v<Swim::Render::RenderGraphExecutor>);
static_assert(!std::is_same_v<Swim::Render::GraphBuffer, Swim::Render::GraphTexture>);
