#pragma once

#include "Engine/Systems/UI/UiCanvas.h"

#include <span>
#include <vector>

namespace Swim::UI
{
	struct UiCanvasHandle
	{
		std::uint32_t Value = 0;

		explicit operator bool() const { return Value != 0; }

		bool operator==(const UiCanvasHandle&) const = default;
	};

	struct UiCanvasDesc
	{
		UiDocument* Document = nullptr; // Not owned; Remove the canvas before destroying it.
		UiCanvasMode Mode = UiCanvasMode::Screen;
		bool Interactive = true; // False: displayed only, never hit.
		// True: a pointer anywhere on the canvas stops there (modal overlays, solid world
		// panels) even between nodes. False: only hit-testable nodes stop the pointer, so
		// the world behind a HUD stays reachable.
		bool BlocksPointer = false;
		std::int32_t Order = 0; // Screen canvases: higher on top (ties: later added on top).
	};

	// A pointer hit on a render-surface canvas found by the application (a ray cast against
	// the mesh showing it, converted from the hit UV to canvas pixels).
	struct UiSurfaceHit
	{
		UiCanvasHandle Canvas;
		UiPoint Point;		   // Canvas pixels.
		float Distance = 0.0f; // Along the pointer ray, comparable with world canvas hits.
	};

	struct UiPointer
	{
		std::optional<UiPoint> Screen; // Viewport pixels (mouse, touch).
		std::optional<UiRay> Ray;	   // World ray (mouse through the camera, crosshair, VR controller).
		std::span<const UiSurfaceHit> SurfaceHits;
	};

	// Input routing across every canvas of a frame (critical-path item 79): screen
	// overlays, render surfaces, world panels and billboards. The nearest canvas under
	// the pointer gets it (screen canvases before world ones); a press captures the
	// pointer for its canvas until release, following the ray on the canvas's unbounded
	// plane (dragging a world slider off its panel). One canvas at a time owns keyboard,
	// text and IME focus; focusing a node in another canvas blurs the previous one.
	// Owner thread; documents must be laid out (their pointer calls re-layout on demand).
	class UiCanvasRouter
	{
	  public:
		// Throws std::invalid_argument without a document or for an unknown mode.
		UiCanvasHandle Add(const UiCanvasDesc& desc);
		bool Remove(UiCanvasHandle canvas); // Ends its hover, capture and focus.
		bool Contains(UiCanvasHandle canvas) const;
		UiDocument* GetDocument(UiCanvasHandle canvas) const;
		void SetInteractive(UiCanvasHandle canvas, bool interactive);

		// Screen canvases: canvas pixel = viewport pixel - offset, inside size (0: unbounded).
		void SetScreenPlacement(UiCanvasHandle canvas, UiPoint offset, UiPoint size = {});
		// World panels and billboards (UI::CanvasToWorld), or render surfaces shown on a flat
		// quad: hit by pointer rays inside [0, canvasSize).
		void SetWorldPlacement(UiCanvasHandle canvas, const UiMatrix3x4& canvasToWorld, UiPoint canvasSize, bool twoSided = true);
		// The camera world canvases are seen through (projects their IME rectangles).
		void SetCamera(const UiCameraView& camera);

		// Hover and pointer moves; returns the canvas under (or capturing) the pointer.
		UiCanvasHandle PointerMove(const UiPointer& pointer);
		void PointerDown(UiKeyModifiers modifiers = {});
		void PointerUp();
		void CancelPointer();
		bool Wheel(UiPoint delta);

		// Keyboard, text and IME go to the focused canvas. Without one, Tab focuses the first
		// interactive canvas that has focusable nodes (screen canvases first).
		bool KeyDown(UiKey key, UiKeyModifiers modifiers = {});
		void TextInput(std::string_view utf8);
		void SetComposition(std::string_view utf8, std::uint32_t cursor);
		// Directional navigation in the focused canvas (or the first one, as Tab).
		bool Navigate(UiNavDirection direction);
		bool FocusNext(bool backwards = false);
		void Focus(UiCanvasHandle canvas); // Makes the canvas the keyboard owner (its document keeps its node focus).
		void ClearFocus();				   // Blurs the focused canvas's document.
		// Context menus (right button, Shift+F10, gamepad North): the menu registered under
		// the pointer on the hovered canvas (which becomes the focused canvas), or the one of
		// the focused canvas's focused node. False when there is none.
		bool OpenContextMenu();
		bool OpenContextMenuForFocus();

		UiCanvasHandle GetHovered() const { return hovered; }

		UiCanvasHandle GetCaptured() const { return captured; }

		UiCanvasHandle GetFocused() const { return focused; }

		bool IsPointerOverUi() const { return static_cast<bool>(hovered) || static_cast<bool>(captured); }

		bool WantsTextInput() const;
		// The focused editable caret in viewport pixels (screen offset or camera projection);
		// empty without one or for surfaces without a world placement.
		std::optional<UiRect> GetTextInputRect() const;

	  private:
		struct Canvas
		{
			UiCanvasHandle Handle;
			UiCanvasDesc Desc;
			std::uint32_t Sequence = 0;
			UiPoint Offset;
			UiPoint Size;
			bool HasWorld = false;
			UiMatrix3x4 CanvasToWorld = UiIdentity3x4;
			UiPoint CanvasSize;
			bool TwoSided = true;
			UiPoint LastPoint; // Canvas pixels of the last pointer position routed here.
		};

		Canvas* Find(UiCanvasHandle canvas);
		const Canvas* Find(UiCanvasHandle canvas) const;
		std::vector<Canvas*> ByPriority(); // Screen canvases top first, then world ones in insertion order.
		std::optional<UiPoint> MapCaptured(Canvas& canvas, const UiPointer& pointer) const;
		void SetFocused(UiCanvasHandle canvas);

		std::vector<Canvas> canvases;
		std::uint32_t nextHandle = 1;
		std::uint32_t nextSequence = 0;
		UiCanvasHandle hovered;
		UiCanvasHandle captured;
		UiCanvasHandle focused;
		std::optional<UiCameraView> camera;
	};
} // namespace Swim::UI
