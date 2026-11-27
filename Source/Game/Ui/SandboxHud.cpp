#include "Game/Ui/SandboxHud.h"
#include "Game/Ui/SandboxHudShared.h"

#include "Engine/Components/Tags.h"
#include "Engine/Components/Transform.h"
#include "Engine/Components/UiCanvas.h"
#include "Engine/Input/InputSystem.h"
#include "Engine/Runtime/SimulationClock.h"
#include "Engine/Runtime/FrameProfiler.h"
#include "Engine/Runtime/FramePacing.h"
#include "Engine/Systems/Audio/AudioSystem.h"
#include "Engine/Runtime/RenderToggles.h"
#include "Engine/Runtime/UiRuntime.h"
#include "Engine/Systems/Camera/CameraSystem.h"
#include "Engine/Systems/Renderer/Runtime/FrameRenderer.h"
#include "Engine/Systems/Renderer/Runtime/RenderServices.h"
#include "Engine/Systems/Scene/RenderExtraction/Runtime/SceneRenderBridge.h"
#include "Engine/Systems/Scene/Scene.h"
#include "Engine/Systems/Scene/SceneCommandBuffer.h"
#include "Engine/Systems/UI/UiTheme.h"
#include "Game/Behaviors/BallShooter.h"
#include "Game/Behaviors/ReflectionLabFloor.h"
#include "Game/Scenes/Sandbox.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <functional>
#include <sstream>

namespace Game
{

	using namespace Swim::UI;

	using namespace SandboxHudDetail;

	SandboxHud::SandboxHud(Engine::Scene* sceneValue, entt::entity owner) : Behavior(sceneValue, owner)
	{
	}

	UiNodeId SandboxHud::CreateSection(UiNodeId parent)
	{
		UiStyle style;
		style.Flow = UiFlow::Column;
		style.Gap = 7.0f;
		style.Width = UiLength::Percent(1.0f);
		style.Padding = { 2, 2, 10, 2 };
		return CreateStyledNode(*document, parent, style);
	}

	UiNodeId SandboxHud::AddButton(UiNodeId parent, const std::string& text, const std::string& tooltip, std::function<void()> onClick)
	{
		const auto button = CreateButton(*document, parent, text);

		if (!tooltip.empty())
		{
			CreateTooltip(*document, button, tooltip, 0.45f);
		}

		document->OnClick(button, std::move(onClick));
		return button;
	}

	UiNodeId SandboxHud::AddSlider(
		UiNodeId parent, const std::string& label, float min, float max, float value, int decimals, std::function<void(float)> onChange)
	{
		CreateLabel(*document, parent, label);
		UiSliderDesc desc;
		desc.Min = min;
		desc.Max = max;
		desc.Value = std::clamp(value, min, max);
		desc.ShowValue = true;
		desc.Decimals = decimals;
		desc.EditableValue = true;
		const auto slider = CreateSlider(*document, parent, desc);
		document->OnValue(slider, std::move(onChange));
		return slider;
	}

	UiNodeId SandboxHud::AddCheckbox(UiNodeId parent, const std::string& label, bool value, std::function<void(bool)> onChange)
	{
		const auto box = CreateCheckbox(*document, parent, label, value ? UiCheckState::Checked : UiCheckState::Unchecked);
		document->OnChecked(box, std::move(onChange));
		return box;
	}

	UiNodeId SandboxHud::AddToggle(UiNodeId parent, const std::string& label, bool value, std::function<void(bool)> onChange)
	{
		const auto toggle = CreateToggle(*document, parent, label, value);
		document->OnChecked(toggle, std::move(onChange));
		return toggle;
	}

	UiNodeId SandboxHud::BoundSlider(UiNodeId parent, const std::string& label, float min, float max, int decimals,
		std::function<float()> get, std::function<void(float)> set)
	{
		CreateLabel(*document, parent, label);
		UiSliderDesc desc;
		desc.Min = min;
		desc.Max = max;
		desc.ShowValue = true;
		desc.Decimals = decimals;
		desc.EditableValue = true;
		const auto slider = CreateSlider(*document, parent, desc);
		// Two-way: edits call set; changes made elsewhere (console, presets, the fly camera's
		// zoom) show up on the document's next Update. Nothing to keep in sync by hand.
		document->BindValue(slider, std::move(get), std::move(set));
		return slider;
	}

	UiNodeId SandboxHud::BoundCheckbox(UiNodeId parent, const std::string& label, std::function<bool()> get, std::function<void(bool)> set)
	{
		const auto box = CreateCheckbox(*document, parent, label);
		document->BindValue(
			box,
			[get = std::move(get)]
			{
				return get() ? 1.0f : 0.0f;
			},
			[set = std::move(set)](float value)
			{
				set(value == 1.0f);
			});
		return box;
	}

	UiNodeId SandboxHud::SliderFor(UiNodeId parent, const std::string& label, float min, float max, float& value, int decimals)
	{
		CreateLabel(*document, parent, label);
		UiSliderDesc desc;
		desc.Min = min;
		desc.Max = max;
		desc.ShowValue = true;
		desc.Decimals = decimals;
		desc.EditableValue = true;
		const auto slider = CreateSlider(*document, parent, desc);
		document->BindValue(slider, value);
		return slider;
	}

	UiNodeId SandboxHud::CheckFor(UiNodeId parent, const std::string& label, bool& value)
	{
		const auto box = CreateCheckbox(*document, parent, label);
		document->BindChecked(box, value);
		return box;
	}

	UiNodeId SandboxHud::DropdownFor(UiNodeId parent, const std::string& label, const std::vector<std::string>& options,
		std::function<std::uint32_t()> get, std::function<void(std::uint32_t)> set)
	{
		CreateLabel(*document, parent, label);
		const auto dropdown = CreateDropdown(*document, parent, options);
		document->BindValue(
			dropdown.Root,
			[get = std::move(get)]
			{
				return static_cast<float>(get());
			},
			[set = std::move(set)](float value)
			{
				if (value >= 0.0f)
				{
					set(static_cast<std::uint32_t>(value));
				}

			});
		return dropdown.Root;
	}

	bool SandboxHud::Command(const std::string& command)
	{
		return scene->DispatchCommand(command);
	}

	int SandboxHud::Init()
	{
		sandbox = dynamic_cast<Sandbox*>(scene);
		render = scene->GetRenderServices();

		if (!render || !render->Ui)
		{
			return 0; // Headless without a UI runtime.
		}

		document = render->Ui->CreateDocument();

		if (auto* audio = scene->GetAudio())
		{
			// Interface sounds for every control of the panel (groups from the audio playground).
			document->SetObserver(
				[audio](const Swim::UI::UiEvent& event)
				{
					if (event.Kind == Swim::UI::UiEventKind::Click)
					{
						audio->PlayEvent("ui.activate");
					}
					else if (event.Kind == Swim::UI::UiEventKind::Enter)
					{
						audio->PlayEvent("ui.hover");
					}
				});
		}
		UiStyle rootStyle;
		rootStyle.Flow = UiFlow::Overlay;
		document->SetStyle(document->GetRoot(), rootStyle);

		BuildPanel();
		BuildDiagnostics();
		BuildHelp();

		Engine::UiCanvas canvas;
		canvas.Document = document;
		canvas.Mode = UiCanvasMode::Screen;
		canvas.Order = 10;
		scene->AddComponent<Engine::UiCanvas>(entity, canvas);

		if (sandbox && sandbox->GetInfoDocument() && sandbox->GetInfoButton())
		{
			infoDocument = sandbox->GetInfoDocument();
			infoButton = sandbox->GetInfoButton();
			sandbox->GetInfoDocument()->OnClick(infoButton,
				[this]
				{
					if (auto* shooter = sandbox->GetShooter())
					{
						shooter->Fire();
					}

				});
		}

		RefreshStatus();
		RefreshDiagnostics();
		return 0;
	}

	int SandboxHud::Exit()
	{
		if (document)
		{
			document->ClearCallbacks();
		}

		if (const auto info = infoDocument.lock(); info && info->Contains(infoButton))
		{
			info->ClearCallbacks(infoButton);
		}

		return 0;
	}

	void SandboxHud::BuildPanel()
	{
		auto& ui = *render->Ui;
		UiStyle style;
		style.Absolute = true;
		style.AnchorMin = { 0.0f, 0.0f };
		style.AnchorMax = { 0.0f, 1.0f };
		style.Pivot = { 0.0f, 0.0f };
		style.Margin = { 12, 12, 0, 12 };
		style.Width = UiLength::Pixels(490.0f);
		style.Flow = UiFlow::Column;
		style.Padding = { 14, 12, 12, 12 };
		style.Gap = 8;
		style.Background = PanelColor;
		style.BorderWidth = 1.5f;
		style.BorderColor = PanelBorder;
		style.CornerRadius = 10;
		style.HitTest = true; // The panel's background keeps the pointer from the game.
		panel = CreateStyledNode(*document, document->GetRoot(), style);

		const auto titleRow = CreateRow(*document, panel, 8);
		const auto title = CreateLabel(*document, titleRow, ui.GetBoldFonts(), "Swim Engine", 22.0f);
		{
			auto titleStyle = document->GetStyle(title);
			titleStyle.TextColor = AccentText;
			document->SetStyle(title, titleStyle);
		}
		CreateLabel(*document, titleRow, ui.GetFonts(), "sandbox", 16.0f);

		tabs = CreateRadioGroup(*document, panel, {}, -1, UiOrientation::Horizontal);
		const std::array<const char*, 4> tabNames{ "Simulation", "Rendering", "Camera/Post", "Scene" };
		static_assert(tabNames.size() == Sandbox::SandboxTabCount);

		for (std::size_t i = 0; i < tabNames.size(); ++i)
		{
			tabOptions[i] = AddRadioOption(*document, tabs, tabNames[i]);
		}

		document->SetValue(tabs, 0.0f);
		document->OnValue(tabs,
			[this](float value)
			{
				ShowSection(static_cast<std::uint32_t>(std::max(value, 0.0f)));
			});

		UiStyle scrollStyle;
		scrollStyle.Grow = 1.0f;
		scrollStyle.Shrink = 1.0f;
		scrollStyle.Width = UiLength::Percent(1.0f);
		scrollStyle.MinSize = { 0.0f, 80.0f };
		const auto scroll = CreateScrollArea(*document, panel, scrollStyle, true);

		for (auto& node : sections)
		{
			node = CreateSection(scroll.Viewport);
		}

		BuildSimulation(sections[0]);
		BuildRendering(sections[1]);
		BuildCamera(sections[2]);
		BuildScene(sections[3]);
		ShowSection(0);
	}

	void SandboxHud::ShowSection(std::uint32_t index)
	{
		section = std::min<std::uint32_t>(index, static_cast<std::uint32_t>(sections.size() - 1));

		for (std::uint32_t i = 0; i < sections.size(); ++i)
		{
			auto style = document->GetStyle(sections[i]);
			style.Visible = i == section;
			document->SetStyle(sections[i], style);
		}
	}

	void SandboxHud::BuildDiagnostics()
	{
		UiStyle style;
		style.Absolute = true;
		style.AnchorMin = { 1.0f, 0.0f };
		style.AnchorMax = { 1.0f, 0.0f };
		style.Pivot = { 1.0f, 0.0f };
		style.Offset = { -12.0f, 12.0f };
		style.Width = UiLength::Pixels(360.0f);
		style.Padding = { 12, 10, 12, 10 };
		style.Background = PanelColor;
		style.BorderWidth = 1.5f;
		style.BorderColor = PanelBorder;
		style.CornerRadius = 8;
		diagnostics = CreateStyledNode(*document, document->GetRoot(), style);
		diagnosticsText = CreateLabel(*document, diagnostics, render->Ui->GetMonoFonts(), "...", 13.0f);
	}

	void SandboxHud::BuildHelp()
	{
		UiStyle style;
		style.Absolute = true;
		style.AnchorMin = { 0.5f, 1.0f };
		style.AnchorMax = { 0.5f, 1.0f };
		style.Pivot = { 0.5f, 1.0f };
		style.Offset = { 0.0f, -10.0f };
		style.Padding = { 12, 6, 12, 6 };
		style.Background = UiSrgbHex(0x090c12, 0.9f);
		style.BorderWidth = 1.5f;
		style.BorderColor = PanelBorder;
		style.CornerRadius = 8;
		help = CreateStyledNode(*document, document->GetRoot(), style);
		CreateLabel(*document, help, render->Ui->GetFonts(),
			"RMB + WASD fly  |  1-7 views  |  F fire  |  P pause  |  N step  |  C all UI  |  V panel  |  X stats", 14.0f);
	}

	void SandboxHud::SetPanelVisible(UiNodeId node, bool visible)
	{
		auto style = document->GetStyle(node);
		style.Visible = visible;
		document->SetStyle(node, style);
	}

	void SandboxHud::Shortcuts()
	{
		if (!input || !render || !render->Ui || render->Ui->GetInputFrame().KeyboardCaptured)
		{
			return;
		}

		using Swim::Platform::KeyCode;

		if (input->IsKeyTriggered(KeyCode::P))
		{
			Command(scene->GetEngineState() == Engine::EngineState::Paused ? "resume" : "pause");
		}

		if (input->IsKeyTriggered(KeyCode::N))
		{
			Command("step");
		}

		// C: every sandbox UI surface (panel, diagnostics, help bar, world panels and
		// labels) on/off; V: only the control panel; X: only the diagnostics.
		if (input->IsKeyTriggered(KeyCode::C) && sandbox)
		{
			sandbox->SetHudVisible(!sandbox->IsHudVisible());
		}

		if (input->IsKeyTriggered(KeyCode::V))
		{
			panelVisible = !panelVisible;
			SetPanelVisible(panel, panelVisible);
		}

		if (input->IsKeyTriggered(KeyCode::X))
		{
			diagnosticsVisible = !diagnosticsVisible;
			SetPanelVisible(diagnostics, diagnosticsVisible);
		}

		// 1 .. 5: camera bookmarks.
		constexpr KeyCode bookmarkKeys[] = { KeyCode::Num1, KeyCode::Num2, KeyCode::Num3, KeyCode::Num4, KeyCode::Num5, KeyCode::Num6,
			KeyCode::Num7 };

		for (std::uint32_t i = 0; i < std::size(bookmarkKeys) && sandbox; ++i)
		{
			if (input->IsKeyTriggered(bookmarkKeys[i]))
			{
				sandbox->GoToBookmark(i);
			}
		}
	}

	void SandboxHud::RefreshStatus()
	{
		const auto state = scene->GetEngineState();
		SetLabelText(*document, stateLabel, "State: " + std::string(Engine::ToString(state)));
		SetLabelText(*document, pauseButton, state == Engine::EngineState::Paused ? "Resume" : "Pause", document->GetTheme()->Fonts,
			document->GetTheme()->Class(UiThemeClass::Button).TextSize);

		if (const auto* clock = scene->GetClock())
		{
			SetLabelText(*document, clockLabel,
				Fixed(clock->GetSimulatedSeconds(), 1) + " s  |  " + std::to_string(clock->GetFixedStepCount()) + " steps @ " +
					Fixed(1.0 / clock->GetFixedDelta(), 0) + " Hz  |  x" + Fixed(clock->GetTimeScale(), 2));
		}

		if (sandbox)
		{
			const auto* shooter = sandbox->GetShooter();
			SetLabelText(*document, ballLabel,
				"Balls fired: " + std::to_string(shooter ? shooter->GetFired() : 0) +
					"  |  impacts: " + std::to_string(sandbox->GetImpacts()) +
					"  |  in flight: " + std::to_string(scene->CountWithTag(Engine::Tags::Projectile)));
		}
	}

	void SandboxHud::RefreshDiagnostics()
	{
		if (!diagnosticsVisible)
		{
			return;
		}

		std::ostringstream text;
		text << "FPS " << scene->GetFPS();

		if (render->Stats)
		{
			const auto& s = *render->Stats;
			text << "   " << s.Width << "x" << s.Height << "\n";
			text << "CPU " << Fixed(s.CpuMilliseconds, 2) << " ms   GPU ";
			text << (s.GpuTimingsAvailable ? Fixed(s.GpuMilliseconds, 2) + " ms" : std::string("n/a")) << "  (" << s.Passes << " passes)\n";

			for (std::uint32_t i = 0; i < std::min<std::uint32_t>(s.TopPassCount, 4); ++i)
			{
				std::string name = s.TopPasses[i].Name.substr(0, 26);
				name.resize(26, ' ');
				text << "  " << name << Fixed(s.TopPasses[i].Milliseconds, 2) << " ms\n";
			}

			text << "Objects " << s.RenderObjects << "   materials " << s.Materials << "\n";
			text << "Lights " << s.DirectionalLights << " dir + " << s.LocalLights << " local\n";
			text << "Shadow views " << s.ShadowViews << " (" << s.ShadowCasters << " casters)\n";
			text << "Emitters " << s.ParticleEmitters << "   skinned " << s.SkinnedInstances << "\n";
			text << "UI quads " << s.UiQuads << "   page slots " << s.PageSlots << "\n";
			text << "Meshes " << s.ResidentMeshes << "   textures " << s.ResidentTextures << "   pending " << s.PendingAssets << "\n";
		}

		if (render->Pacing)
		{
			const auto& pacing = *render->Pacing;
			const char* modes[] = { "auto", "vsync", "mailbox", "immediate" };
			text << "Present " << modes[static_cast<int>(pacing.PresentMode) & 3] << "   latency " << Fixed(pacing.LatencyMilliseconds, 1)
				 << " ms   queued " << pacing.QueuedFrames << "\n";
		}

		text << "Entities " << scene->GetEntityCount() << "   physics steps " << scene->GetPhysicsStepCount() << "\n";
		text << "State " << Engine::ToString(scene->GetEngineState());

		if (const auto* clock = scene->GetClock())
		{
			text << "   x" << Fixed(clock->GetTimeScale(), 2);
		}

		SetLabelText(*document, diagnosticsText, text.str(), render->Ui->GetMonoFonts(), 13.0f);
	}

	void SandboxHud::RefreshInfoPanel()
	{
		if (!sandbox || !sandbox->GetInfoDocument() || !sandbox->GetInfoBody())
		{
			return;
		}

		std::ostringstream text;
		text << "State: " << Engine::ToString(scene->GetEngineState()) << "    FPS: " << scene->GetFPS() << "\n";

		if (render->Stats)
		{
			text << render->Stats->RenderObjects << " GPU scene objects, " << render->Stats->LocalLights << " clustered lights\n";
		}

		text << "Physics impacts: " << sandbox->GetImpacts() << ", balls in the sound zone: " << sandbox->GetZoneOccupants() << "\n";
		text << "This panel is UI in the world: depth-tested and clickable.";
		SetLabelText(*sandbox->GetInfoDocument(), sandbox->GetInfoBody(), text.str(), render->Ui->GetFonts(), 20.0f);
	}

	void SandboxHud::RefreshEntities(bool force)
	{
		if (!entityList)
		{
			return;
		}

		std::vector<entt::entity> entities;
		std::vector<std::string> names;
		const auto& registry = scene->GetRegistry();

		for (const auto [entityId, name] : registry.view<Engine::EntityName>().each())
		{
			if (!filter.empty() && Lower(name.Value).find(filter) == std::string::npos)
			{
				continue;
			}

			entities.push_back(entityId);
		}

		std::sort(entities.begin(), entities.end(),
			[&](entt::entity a, entt::entity b)
			{
				return scene->GetSerializedEntityId(a).Value < scene->GetSerializedEntityId(b).Value;
			});

		for (const auto e : entities)
		{
			names.push_back(scene->GetEntityName(e));
		}

		if (!force && names == listedNames)
		{
			return;
		}

		listedEntities = std::move(entities);
		listedNames = std::move(names);
		entityList->SetItemCount(static_cast<std::uint32_t>(listedNames.size()));
		entityList->Refresh();
		SetLabelText(*document, sceneSummary,
			std::to_string(scene->GetEntityCount()) + " entities  |  " + std::to_string(listedNames.size()) + " listed  |  " +
				std::to_string(scene->CountWithTag(Engine::Tags::Physics)) + " physics bodies");
	}

	void SandboxHud::Update(double dt)
	{
		if (!document)
		{
			return;
		}

		Shortcuts();

		if (sandbox && sandbox->GetRequestedTab() != UINT32_MAX)
		{
			// Programmatic setters stay silent; update the visible section directly.
			document->SetValue(tabs, static_cast<float>(sandbox->GetRequestedTab()));
			ShowSection(sandbox->GetRequestedTab());
			sandbox->RequestTab(UINT32_MAX);
		}

		if (sandbox && bookmarkDropdown && document->GetValue(bookmarkDropdown) != static_cast<float>(sandbox->GetLastBookmark()))
		{
			document->SetValue(bookmarkDropdown, static_cast<float>(sandbox->GetLastBookmark()));
		}

		if (profilePending && render && render->Profiler && !render->Profiler->IsCapturing())
		{
			profilePending = false;
			SetLabelText(*document, profileLabel, Engine::FrameProfiler::Summary(render->Profiler->GetReport(), 6));
		}

		if (sandbox)
		{
			// The HUD and every world canvas (info panel, zone labels) follow the UI switch.
			const bool visible = sandbox->IsHudVisible();

			for (auto [canvasEntity, canvas] : scene->GetRegistry().view<Engine::UiCanvas>().each())
			{
				(void)canvasEntity;
				canvas.Visible = visible;
			}
		}

		refreshTimer += static_cast<float>(dt);
		entityTimer += static_cast<float>(dt);

		if (refreshTimer >= 0.2f)
		{
			refreshTimer = 0.0f;
			RefreshStatus();
			RefreshDiagnostics();
			RefreshInfoPanel();
		}

		if (entityTimer >= 1.0f)
		{
			entityTimer = 0.0f;
			RefreshEntities(false);
		}

		if (entityList && section == 3)
		{
			document->EnsureLayout();
			entityList->Update();
		}
	}

} // namespace Game
