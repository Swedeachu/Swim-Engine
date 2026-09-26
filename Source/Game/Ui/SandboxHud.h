#pragma once

#include "Engine/Systems/Entity/Behavior.h"
#include "Engine/Systems/UI/UiDocument.h"
#include "Engine/Systems/UI/UiWidgets.h"
#include "Game/Ui/UiBindings.h"

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace Engine
{
	struct RenderServices;
}

namespace Game
{
	class Sandbox;

	// The sandbox's screen UI (one screen canvas on the HUD entity), running in every
	// engine state on real time:
	//
	//   control panel   Simulation (play / pause / step / stop, time scale, ball rain,
	//                   reset with a confirmation modal), Rendering (every renderer switch,
	//                   tone mapping, exposure, sun, bloom, debug view), Scene (a filtered,
	//                   virtualized entity browser, focus/delete, a spawn menu)
	//   diagnostics     frame timings, the costliest GPU passes, scene and renderer counters
	//   help bar        the controls
	//
	// It also drives the world-space info panel. Every widget kind of the UI module is used:
	// panels, labels, buttons, toggles, checkboxes, sliders (with editable values), a radio
	// group, dropdowns, a text field, a list view, a virtual list, a scroll area, a menu,
	// tooltips and a modal dialog. Shortcuts (when the UI does not own the keyboard):
	// P pause/resume, N step, F fire, 1-7 camera bookmarks, C all UI on/off (panel,
	// diagnostics, help bar, world panels and labels), V the panel only, X the diagnostics only.
	class SandboxHud : public Engine::Behavior
	{
	  public:
		SandboxHud(Engine::Scene* scene, entt::entity owner);

		int Init() override;
		void Update(double dt) override;

		bool UsesRealTime() const override { return true; }

		const std::shared_ptr<Swim::UI::UiDocument>& GetDocument() const { return document; }

		// Test hooks.
		Swim::UI::UiNodeId GetPauseButton() const { return pauseButton; }

		std::uint32_t GetVisibleSection() const { return section; }

		Swim::UI::UiNodeId GetBookmarkDropdown() const { return bookmarkDropdown; }

	  private:
		Swim::UI::UiNodeId CreateSection(Swim::UI::UiNodeId parent);
		Swim::UI::UiNodeId AddButton(
			Swim::UI::UiNodeId parent, const std::string& text, const std::string& tooltip, std::function<void()> onClick);
		Swim::UI::UiNodeId AddSlider(Swim::UI::UiNodeId parent, const std::string& label, float min, float max, float value, int decimals,
			std::function<void(float)> onChange);
		Swim::UI::UiNodeId AddCheckbox(Swim::UI::UiNodeId parent, const std::string& label, bool value, std::function<void(bool)> onChange);
		Swim::UI::UiNodeId AddToggle(Swim::UI::UiNodeId parent, const std::string& label, bool value, std::function<void(bool)> onChange);
		void BuildPanel();
		void BuildSimulation(Swim::UI::UiNodeId parent);
		void BuildRendering(Swim::UI::UiNodeId parent);
		void BuildScene(Swim::UI::UiNodeId parent);
		void BuildDiagnostics();
		void BuildHelp();
		void ShowSection(std::uint32_t index);
		void RefreshStatus();
		void RefreshDiagnostics();
		void RefreshInfoPanel();
		void RefreshEntities(bool force);
		void Shortcuts();
		bool Command(const std::string& command);
		void SetPanelVisible(Swim::UI::UiNodeId node, bool visible);

		Sandbox* sandbox = nullptr;
		Engine::RenderServices* render = nullptr;
		std::shared_ptr<Swim::UI::UiDocument> document;
		UiBindings bindings;
		UiBindings infoBindings;

		Swim::UI::UiNodeId panel;
		Swim::UI::UiNodeId tabs;
		std::array<Swim::UI::UiNodeId, 3> sections{};
		std::uint32_t section = 0;
		Swim::UI::UiNodeId stateLabel;
		Swim::UI::UiNodeId clockLabel;
		Swim::UI::UiNodeId playButton;
		Swim::UI::UiNodeId pauseButton;
		Swim::UI::UiNodeId ballLabel;
		Swim::UI::UiNodeId timeScaleSlider;
		Swim::UI::UiNodeId bookmarkDropdown;
		Swim::UI::UiModal resetModal;
		Swim::UI::UiNodeId resetConfirm;
		Swim::UI::UiNodeId resetCancel;

		Swim::UI::UiNodeId filterField;
		std::unique_ptr<Swim::UI::UiVirtualList> entityList;
		std::vector<entt::entity> listedEntities;
		std::vector<std::string> listedNames;
		std::string filter;
		Swim::UI::UiNodeId entityDetails;
		Swim::UI::UiNodeId sceneSummary;
		Swim::UI::UiPopupList spawnMenu;
		Swim::UI::UiNodeId spawnButton;
		std::array<Swim::UI::UiNodeId, 5> spawnItems{};

		Swim::UI::UiNodeId diagnostics;
		Swim::UI::UiNodeId diagnosticsText;
		Swim::UI::UiNodeId help;

		float refreshTimer = 0.0f;
		float entityTimer = 0.0f;
		bool panelVisible = true;
		bool diagnosticsVisible = true;
	};
} // namespace Game
