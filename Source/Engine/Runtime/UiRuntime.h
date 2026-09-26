#pragma once

#include "Engine/Systems/Renderer/Runtime/FrameRenderer.h"
#include "Engine/Systems/Text/GlyphAtlas.h"
#include "Engine/Systems/UI/UiCanvas.h"
#include "Engine/Systems/UI/UiCanvasRouter.h"
#include "Engine/Systems/UI/UiTheme.h"
#include "Engine/Systems/UiInput/UiInputBridge.h"

#include <entt/entt.hpp>

#include <filesystem>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

namespace Swim::Input
{
	class InputSystem;
}

namespace Swim::Text
{
	class FontCollection;
}

namespace Engine
{
	class Scene;

	// The runtime side of retained UI (Phase 23): the fonts (Resources/Fonts), the theme
	// every document shares, one glyph atlas, the canvas router and the input bridge.
	// Each frame it mirrors the active scene's UiCanvas components into router canvases
	// (screen overlays, world panels, billboards placed by their Transforms), lays their
	// documents out, routes this frame's input to them (reporting whether the UI took the
	// pointer or keyboard so gameplay and the fly camera can stand back), advances their
	// animations and builds the frame renderer's UI draw list. Owner thread only.
	class UiRuntime
	{
	  public:
		// Loads DejaVuSans(-Bold/Mono).ttf from <resourceRoot>/Fonts; throws
		// std::runtime_error when the regular face is missing.
		explicit UiRuntime(const std::filesystem::path& resourceRoot);
		~UiRuntime();

		const std::shared_ptr<const Swim::Text::FontCollection>& GetFonts() const { return fonts; }

		const std::shared_ptr<const Swim::Text::FontCollection>& GetBoldFonts() const { return boldFonts; }

		const std::shared_ptr<const Swim::Text::FontCollection>& GetMonoFonts() const { return monoFonts; }

		const std::shared_ptr<const Swim::UI::UiTheme>& GetTheme() const { return theme; }

		Swim::UI::UiCanvasRouter& GetRouter() { return router; }

		Swim::Text::GlyphAtlas& GetAtlas() { return atlas; }

		// A new document using the shared theme.
		std::shared_ptr<Swim::UI::UiDocument> CreateDocument() const;

		struct ViewDesc
		{
			Swim::UI::UiCameraView Camera; // Viewport size included.
			float DpiScale = 1.0f;
		};

		// 1. Mirror the scene's canvases and lay them out (null scene: remove every canvas).
		void Sync(Scene* scene, const ViewDesc& view);
		// 2. Route one accepted input frame (after InputSystem::AdvanceFrame).
		const Swim::UI::UiInputFrame& ApplyInput(const Swim::Input::InputSystem* input, float deltaSeconds);
		// 3. After the scene updated: re-layout changed documents, animate, and build the
		//    draw list (world canvases first, then screen overlays by order).
		std::span<const UiDrawItem> Finish(float deltaSeconds);

		const Swim::UI::UiInputFrame& GetInputFrame() const { return inputFrame; }

		// The UI owns the pointer or keyboard this frame.
		bool IsCapturingInput() const { return inputFrame.PointerOverUi || inputFrame.KeyboardCaptured; }

		std::uint32_t GetCanvasCount() const { return static_cast<std::uint32_t>(canvases.size()); }

	  private:
		struct CanvasState
		{
			Swim::UI::UiCanvasHandle Handle;
			std::shared_ptr<Swim::UI::UiDocument> Document;
			Swim::UI::UiCanvasMode Mode = Swim::UI::UiCanvasMode::Screen;
			Swim::UI::UiPoint LayoutSize;
			float DpiScale = 1.0f;
			Swim::UI::UiPoint Offset;
			Swim::UI::UiMatrix3x4 CanvasToWorld = Swim::UI::UiIdentity3x4;
			Swim::UI::UiWorldPlacement Placement;
			bool DepthTest = true;
			std::int32_t Order = 0;
			std::uint64_t Sequence = 0;
			bool Culled = false; // No valid placement this frame (billboard anchor behind the camera).
		};

		void RemoveAll();

		std::shared_ptr<const Swim::Text::FontCollection> fonts;
		std::shared_ptr<const Swim::Text::FontCollection> boldFonts;
		std::shared_ptr<const Swim::Text::FontCollection> monoFonts;
		std::shared_ptr<const Swim::UI::UiTheme> theme;
		Swim::Text::GlyphAtlas atlas;
		Swim::UI::UiCanvasRouter router;
		Swim::UI::UiInputBridge bridge;
		Swim::UI::UiInputFrame inputFrame;
		Scene* scene = nullptr;
		ViewDesc view;
		std::unordered_map<entt::entity, CanvasState> canvases;
		std::uint64_t sequence = 0;
		std::vector<UiDrawItem> drawList;
	};
} // namespace Engine
