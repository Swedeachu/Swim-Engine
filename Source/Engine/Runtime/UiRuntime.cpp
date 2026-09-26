#include "Engine/Runtime/UiRuntime.h"

#include "Engine/Components/Transform.h"
#include "Engine/Components/UiCanvas.h"
#include "Engine/Systems/Scene/Scene.h"
#include "Engine/Systems/Text/FontCollection.h"
#include "Engine/Systems/Text/FontFace.h"

#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace Engine
{
	namespace
	{
		std::shared_ptr<const Swim::Text::FontFace> LoadFace(const std::filesystem::path& path)
		{
			std::ifstream stream(path, std::ios::binary);
			if (!stream)
			{
				return nullptr;
			}
			std::vector<char> bytes((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
			return std::make_shared<Swim::Text::FontFace>(std::as_bytes(std::span(bytes)));
		}

		// A darker, higher-contrast take on the default palette for 3D overlays.
		std::shared_ptr<Swim::UI::UiTheme> MakeTheme(std::shared_ptr<const Swim::Text::FontCollection> fonts)
		{
			auto theme = std::make_shared<Swim::UI::UiTheme>();
			theme->Fonts = std::move(fonts);
			theme->Metrics.TransitionSeconds = 0.08f;
			return theme;
		}
	} // namespace

	UiRuntime::UiRuntime(const std::filesystem::path& resourceRoot)
	{
		const auto directory = resourceRoot / "Fonts";
		auto regular = LoadFace(directory / "DejaVuSans.ttf");
		if (!regular)
		{
			throw std::runtime_error("UiRuntime: missing font " + (directory / "DejaVuSans.ttf").string());
		}
		auto bold = LoadFace(directory / "DejaVuSans-Bold.ttf");
		auto mono = LoadFace(directory / "DejaVuSansMono.ttf");
		fonts = Swim::Text::FontCollection::Single(regular);
		boldFonts = bold
			? std::make_shared<const Swim::Text::FontCollection>(std::vector<std::shared_ptr<const Swim::Text::FontFace>>{ bold, regular })
			: fonts;
		monoFonts = mono
			? std::make_shared<const Swim::Text::FontCollection>(std::vector<std::shared_ptr<const Swim::Text::FontFace>>{ mono, regular })
			: fonts;
		theme = MakeTheme(fonts);
	}

	UiRuntime::~UiRuntime()
	{
		RemoveAll();
	}

	std::shared_ptr<Swim::UI::UiDocument> UiRuntime::CreateDocument() const
	{
		auto document = std::make_shared<Swim::UI::UiDocument>();
		document->SetTheme(theme);
		return document;
	}

	void UiRuntime::RemoveAll()
	{
		for (auto& [entity, state] : canvases)
		{
			(void)entity;
			router.Remove(state.Handle);
		}
		canvases.clear();
	}

	void UiRuntime::Sync(Scene* sceneValue, const ViewDesc& viewValue)
	{
		namespace UI = Swim::UI;
		if (sceneValue != scene)
		{
			RemoveAll();
			scene = sceneValue;
		}
		view = viewValue;
		router.SetCamera(view.Camera);
		if (!scene)
		{
			return;
		}
		auto& registry = scene->GetRegistry();
		// Drop canvases whose entity, component or document went away (or were hidden).
		for (auto it = canvases.begin(); it != canvases.end();)
		{
			const auto* component = registry.valid(it->first) ? registry.try_get<UiCanvas>(it->first) : nullptr;
			const bool keep =
				component && component->Visible && component->Document == it->second.Document && component->Mode == it->second.Mode;
			if (keep)
			{
				++it;
				continue;
			}
			router.Remove(it->second.Handle);
			it = canvases.erase(it);
		}

		const float viewportWidth = std::max(view.Camera.ViewportWidth, 1.0f);
		const float viewportHeight = std::max(view.Camera.ViewportHeight, 1.0f);
		auto canvasView = registry.view<UiCanvas>();
		for (const entt::entity entity : canvasView)
		{
			const auto& component = canvasView.get<UiCanvas>(entity);
			if (!component.Visible || !component.Document)
			{
				continue;
			}
			const bool world = component.Mode == UI::UiCanvasMode::WorldPanel || component.Mode == UI::UiCanvasMode::Billboard;
			if (!world && component.Mode != UI::UiCanvasMode::Screen)
			{
				continue; // Render surfaces are not mirrored by the runtime (yet).
			}
			auto found = canvases.find(entity);
			if (found == canvases.end())
			{
				UI::UiCanvasDesc desc;
				desc.Document = component.Document.get();
				desc.Mode = component.Mode;
				desc.Interactive = component.Interactive;
				desc.BlocksPointer = component.BlocksPointer;
				desc.Order = component.Order;
				CanvasState state;
				state.Handle = router.Add(desc);
				state.Document = component.Document;
				state.Mode = component.Mode;
				state.Sequence = ++sequence;
				found = canvases.emplace(entity, std::move(state)).first;
			}
			auto& state = found->second;
			state.Culled = false;
			state.Order = component.Order;
			state.DepthTest = component.DepthTest;
			if (!world)
			{
				const UI::UiPoint size{ component.Size.X > 0.0f ? component.Size.X : viewportWidth - component.Offset.X,
					component.Size.Y > 0.0f ? component.Size.Y : viewportHeight - component.Offset.Y };
				state.LayoutSize = { std::max(size.X, 1.0f), std::max(size.Y, 1.0f) };
				state.DpiScale = view.DpiScale;
				state.Offset = component.Offset;
				router.SetScreenPlacement(state.Handle, component.Offset, component.Size.X > 0.0f ? state.LayoutSize : UI::UiPoint{});
			}
			else
			{
				state.LayoutSize = { std::max(component.Size.X, 1.0f), std::max(component.Size.Y, 1.0f) };
				state.DpiScale = 1.0f;
				UI::UiWorldPlacement placement;
				if (const auto* transform = registry.try_get<Transform>(entity))
				{
					const glm::mat4& m = transform->GetWorldMatrix(registry);
					// Row-major 3x4 of the column-major world matrix.
					for (int r = 0; r < 3; ++r)
					{
						for (int c = 0; c < 4; ++c)
						{
							placement.Transform[static_cast<std::size_t>(r * 4 + c)] = m[c][r];
						}
					}
				}
				placement.Pivot = component.Pivot;
				placement.UnitsPerPixel = component.UnitsPerPixel > 0.0f ? component.UnitsPerPixel : 0.0025f;
				placement.Billboard = component.Billboard;
				placement.ConstantScreenSize = component.ConstantScreenSize;
				placement.ScreenPixelsPerCanvasPixel = component.ScreenPixelsPerCanvasPixel;
				placement.FadeStart = component.FadeStart;
				placement.FadeEnd = component.FadeEnd;
				state.Placement = placement;
				try
				{
					state.CanvasToWorld = UI::CanvasToWorld(component.Mode, placement, state.LayoutSize, &view.Camera);
					router.SetWorldPlacement(state.Handle, state.CanvasToWorld, state.LayoutSize, true);
				}
				catch (const std::invalid_argument&)
				{
					// A constant-screen-size billboard whose anchor is behind the camera (or a
					// degenerate projection) has no placement this frame: skip it.
					state.Culled = true;
				}
			}
			router.SetInteractive(state.Handle, component.Interactive && !state.Culled);
			state.Document->Layout(state.LayoutSize, state.DpiScale);
		}
	}

	const Swim::UI::UiInputFrame& UiRuntime::ApplyInput(const Swim::Input::InputSystem* input, float deltaSeconds)
	{
		inputFrame = {};
		if (input && !canvases.empty())
		{
			inputFrame = bridge.Apply(*input, router, &view.Camera, {}, deltaSeconds);
		}
		return inputFrame;
	}

	std::span<const UiDrawItem> UiRuntime::Finish(float deltaSeconds)
	{
		namespace UI = Swim::UI;
		drawList.clear();
		std::vector<const CanvasState*> ordered;
		ordered.reserve(canvases.size());
		for (auto& [entity, state] : canvases)
		{
			(void)entity;
			state.Document->Update(std::max(deltaSeconds, 0.0f));
			state.Document->Layout(state.LayoutSize, state.DpiScale);
			ordered.push_back(&state);
		}
		std::sort(ordered.begin(), ordered.end(),
			[](const CanvasState* a, const CanvasState* b)
			{
				const bool aWorld = a->Mode != UI::UiCanvasMode::Screen;
				const bool bWorld = b->Mode != UI::UiCanvasMode::Screen;
				if (aWorld != bWorld)
				{
					return aWorld; // World canvases first (under the overlays).
				}
				if (a->Order != b->Order)
				{
					return a->Order < b->Order;
				}
				return a->Sequence < b->Sequence;
			});
		for (const CanvasState* state : ordered)
		{
			if (state->Culled)
			{
				continue;
			}
			UiDrawItem item;
			item.Document = state->Document.get();
			item.DpiScale = state->DpiScale;
			if (state->Mode == UI::UiCanvasMode::Screen)
			{
				item.OffsetX = state->Offset.X;
				item.OffsetY = state->Offset.Y;
			}
			else
			{
				const auto clip = UI::ClipFromCanvas(state->CanvasToWorld, view.Camera);
				item.ClipFromCanvas = clip;
				item.DepthTest = state->DepthTest;
				const float distance =
					UI::CanvasDistance(state->CanvasToWorld, state->LayoutSize, state->Placement.Pivot, view.Camera.View);
				item.Opacity = UI::CanvasFade(state->Placement, distance);
			}
			drawList.push_back(item);
		}
		return drawList;
	}
} // namespace Engine
