#pragma once

#include "Engine/Systems/UI/UiCanvas.h"
#include "Engine/Systems/UI/UiDocument.h"

#include <cstdint>
#include <memory>

namespace Engine
{
	// A retained UI document shown by an entity (Phase 23): a screen overlay, or a panel /
	// billboard placed in the world by the entity's Transform. The UI runtime routes input
	// to it (pointer, keyboard, text, gamepad navigation), lays it out, animates it and
	// hands it to the frame renderer, which composites it after tone mapping (world
	// canvases are depth-tested against the scene).
	struct UiCanvas
	{
		std::shared_ptr<Swim::UI::UiDocument> Document;
		Swim::UI::UiCanvasMode Mode = Swim::UI::UiCanvasMode::Screen;
		// Screen: canvas size in framebuffer pixels (0 = the whole viewport) and its offset.
		// World: the canvas size in canvas pixels.
		Swim::UI::UiPoint Size{ 0.0f, 0.0f };
		Swim::UI::UiPoint Offset{ 0.0f, 0.0f };
		// World panels and billboards (the translation/rotation come from the Transform).
		float UnitsPerPixel = 0.0025f;
		Swim::UI::UiPoint Pivot{ 0.5f, 0.5f };
		Swim::UI::UiBillboardMode Billboard = Swim::UI::UiBillboardMode::Spherical;
		// Billboards: keep ScreenPixelsPerCanvasPixel screen pixels per canvas pixel at any
		// distance (name tags, markers) instead of a world size.
		bool ConstantScreenSize = false;
		float ScreenPixelsPerCanvasPixel = 1.0f;
		float FadeStart = 0.0f;
		float FadeEnd = 0.0f;
		bool DepthTest = true;
		bool Interactive = true;
		bool BlocksPointer = false;
		std::int32_t Order = 0; // Screen canvases: higher on top.
		bool Visible = true;
	};
} // namespace Engine
