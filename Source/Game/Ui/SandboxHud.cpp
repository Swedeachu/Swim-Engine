#include "Game/Ui/SandboxHud.h"

#include "Engine/Components/Tags.h"
#include "Engine/Components/Transform.h"
#include "Engine/Components/UiCanvas.h"
#include "Engine/Input/InputSystem.h"
#include "Engine/Runtime/SimulationClock.h"
#include "Engine/Runtime/FrameProfiler.h"
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
#include <sstream>

namespace Game
{

	using namespace Swim::UI;

	namespace
	{

		// Authored in sRGB (UI colours are linear): near-black panels, a full-strength
		// accent border and accent text.
		const UiColor PanelColor = UiSrgbHex(0x090c12, 0.95f);
		const UiColor PanelBorder = UiSrgbHex(0x1f7aff, 0.85f);
		const UiColor AccentText = UiSrgbHex(0x5ea2ff);

		std::string Lower(std::string text)
		{
			std::transform(text.begin(), text.end(), text.begin(),
				[](unsigned char c)
				{
					return static_cast<char>(std::tolower(c));
				});
			return text;
		}

		void Wrap(UiDocument& document, UiNodeId label)
		{
			auto style = document.GetStyle(label);
			style.TextWrap = Swim::Text::TextWrap::Word;
			style.Width = UiLength::Percent(1.0f);
			document.SetStyle(label, style);
		}

		std::string Fixed(double value, int decimals)
		{
			char buffer[64];
			std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, value);
			return buffer;
		}

	} // namespace

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

		bindings.OnClick(button, std::move(onClick));
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
		bindings.OnValue(slider, std::move(onChange));
		return slider;
	}

	UiNodeId SandboxHud::AddCheckbox(UiNodeId parent, const std::string& label, bool value, std::function<void(bool)> onChange)
	{
		const auto box = CreateCheckbox(*document, parent, label, value ? UiCheckState::Checked : UiCheckState::Unchecked);
		bindings.OnChecked(box, std::move(onChange));
		return box;
	}

	UiNodeId SandboxHud::AddToggle(UiNodeId parent, const std::string& label, bool value, std::function<void(bool)> onChange)
	{
		const auto toggle = CreateToggle(*document, parent, label, value);
		bindings.OnChecked(toggle, std::move(onChange));
		return toggle;
	}

	void SandboxHud::Bind(UiNodeId node, std::function<float()> get, bool check)
	{
		synced.push_back({ node, std::move(get), check });
	}

	UiNodeId SandboxHud::SliderFor(UiNodeId parent, const std::string& label, float min, float max, float& value, int decimals)
	{
		float* target = &value;
		const auto slider = AddSlider(parent, label, min, max, value, decimals,
			[target](float v)
			{
				*target = v;
			});
		Bind(
			slider,
			[target]
			{
				return *target;
			},
			false);
		return slider;
	}

	UiNodeId SandboxHud::CheckFor(UiNodeId parent, const std::string& label, bool& value)
	{
		bool* target = &value;
		const auto box = AddCheckbox(parent, label, value,
			[target](bool on)
			{
				*target = on;
			});
		Bind(
			box,
			[target]
			{
				return *target ? 1.0f : 0.0f;
			},
			true);
		return box;
	}

	UiNodeId SandboxHud::DropdownFor(UiNodeId parent, const std::string& label, const std::vector<std::string>& options,
		std::function<std::uint32_t()> get, std::function<void(std::uint32_t)> set)
	{
		CreateLabel(*document, parent, label);
		const auto dropdown = CreateDropdown(*document, parent, options, static_cast<std::int32_t>(get()));
		bindings.OnValue(dropdown.Root,
			[set = std::move(set)](float value)
			{
				if (value >= 0.0f)
				{
					set(static_cast<std::uint32_t>(value));
				}

			});
		Bind(
			dropdown.Root,
			[get = std::move(get)]
			{
				return static_cast<float>(get());
			},
			false);
		return dropdown.Root;
	}

	void SandboxHud::SyncControls()
	{
		if (!document)
		{
			return;
		}

		for (const auto& control : synced)
		{
			const float value = control.Get();

			if (control.Check)
			{
				const auto state = value != 0.0f ? UiCheckState::Checked : UiCheckState::Unchecked;

				if (document->GetChecked(control.Node) != state)
				{
					document->SetChecked(control.Node, state);
				}
			}
			else if (std::abs(document->GetValue(control.Node) - value) > 1.0e-6f * std::max(1.0f, std::abs(value)))
			{
				document->SetValue(control.Node, value);
			}
		}
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
			infoBindings.OnClick(sandbox->GetInfoButton(),
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
		bindings.OnValue(tabs,
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

	void SandboxHud::BuildSimulation(UiNodeId parent)
	{
		stateLabel = CreateLabel(*document, parent, "State: -");
		clockLabel = CreateLabel(*document, parent, "-");
		Wrap(*document, clockLabel);
		const auto row = CreateRow(*document, parent);
		playButton = AddButton(row, "Play", "Play (from Stopped: the scene starts fresh)",
			[this]
			{
				Command(scene->GetEngineState() == Engine::EngineState::Paused ? "resume" : "play");
			});
		pauseButton = AddButton(row, "Pause", "Pause / resume the simulation (P)",
			[this]
			{
				Command(scene->GetEngineState() == Engine::EngineState::Paused ? "resume" : "pause");
			});
		AddButton(row, "Step", "Advance one fixed step while paused (N)",
			[this]
			{
				Command("step");
			});
		AddButton(row, "Stop", "Stop: the scene resets to its initial state",
			[this]
			{
				Command("stop");
			});
		const auto* clock = scene->GetClock();
		timeScaleSlider = AddSlider(parent, "Time scale", 0.0f, 3.0f, clock ? static_cast<float>(clock->GetTimeScale()) : 1.0f, 2,
			[this](float value)
			{
				Command("timescale " + Fixed(value, 3));
			});

		CreateHeading(*document, parent, "Camera");
		Wrap(*document, CreateLabel(*document, parent, "View (keys 1-7; the camera flies in every state)"));
		std::vector<std::string> views;

		for (std::uint32_t i = 0; i < Sandbox::GetBookmarkCount(); ++i)
		{
			views.emplace_back(Sandbox::GetBookmarkName(i));
		}

		const auto bookmarks = CreateDropdown(*document, parent, views, 0);
		bookmarkDropdown = bookmarks.Root;
		bindings.OnValue(bookmarks.Root,
			[this](float value)
			{
				// Ignore the echo of a view chosen elsewhere (keys, commands).
				if (sandbox && value >= 0.0f && static_cast<std::uint32_t>(value) != sandbox->GetLastBookmark())
				{
					sandbox->GoToBookmark(static_cast<std::uint32_t>(value));
				}

			});

		CreateHeading(*document, parent, "Physics playground");
		const auto balls = CreateRow(*document, parent);
		AddButton(balls, "Fire ball", "Fire from the camera (left mouse button or F)",
			[this]
			{
				if (auto* shooter = sandbox ? sandbox->GetShooter() : nullptr)
				{
					shooter->Fire();
				}

			});
		AddButton(balls, "Drop 10 balls", "Drop ten balls onto the pyramid",
			[this]
			{
				if (sandbox)
				{
					sandbox->SpawnBalls(10);
				}

			});
		AddToggle(parent, "Rain balls", sandbox && sandbox->GetRainBalls(),
			[this](bool on)
			{
				if (sandbox)
				{
					sandbox->SetRainBalls(on);
				}

			});
		ballLabel = CreateLabel(*document, parent, "-");
		Wrap(*document, ballLabel);

		CreateHeading(*document, parent, "Scene");
		AddButton(parent, "Reset sandbox...", "Rebuild the scene from scratch",
			[this]
			{
				OpenModal(*document, resetModal);
			});
		resetModal = CreateModal(*document, "Reset the sandbox?");
		CreateLabel(*document, resetModal.Content, "Every spawned object is removed and the scene is rebuilt.");
		resetCancel = AddModalButton(*document, resetModal, "Cancel");
		resetConfirm = AddModalButton(*document, resetModal, "Reset");
		bindings.OnClick(resetCancel,
			[this]
			{
				document->ClosePopup(resetModal.Root);
			});
		bindings.OnClick(resetConfirm,
			[this]
			{
				document->ClosePopup(resetModal.Root);
				Command("reload");
			});
	}

	void SandboxHud::BuildRendering(UiNodeId parent)
	{
		if (!render->Settings)
		{
			return;
		}

		auto& s = *render->Settings;
		CreateHeading(*document, parent, "Features");
		CheckFor(parent, "Shadows (cascades, spot, point)", s.Shadows);
		CheckFor(parent, "Sky background", s.SkyBackground);
		CheckFor(parent, "Image-based lighting", s.Environment);
		CheckFor(parent, "Temporal anti-aliasing", s.TemporalAntiAliasing);
		CheckFor(parent, "Ambient occlusion (GTAO)", s.ScreenSpace.AmbientOcclusion.Enabled);
		CheckFor(parent, "Height fog", s.ScreenSpace.Fog.Enabled); // Keeps the scene's own (tropical) fog settings.

		// Reflections: screen space -> local probes -> the environment (sandbox.ssr,
		// sandbox.ssrhistory, sandbox.ssrbackfaces, sandbox.probes, sandbox.reflectdebug).
		CreateHeading(*document, parent, "Reflections");
		CheckFor(parent, "Screen-space reflections", s.ScreenSpace.Reflections.Enabled);
		CheckFor(parent, "SSR history (temporal)", s.ScreenSpace.Reflections.History);
		CheckFor(parent, "SSR back-face thickness", s.ScreenSpace.Reflections.BackFaces);
		CheckFor(parent, "Reflection probes", s.ReflectionProbes.Enabled);
		AddSlider(parent, "Probe faces per frame", 1.0f, 12.0f, float(s.ReflectionProbes.FacesPerFrame), 0,
			[&s](float value)
			{
				s.ReflectionProbes.FacesPerFrame = static_cast<std::uint32_t>(std::lround(value));
			});
		DropdownFor(
			parent, "Probe resolution (per face)", { "64", "128", "256" },
			[&s]
			{
				return s.ReflectionProbes.Resolution >= 256u ? 2u : (s.ReflectionProbes.Resolution >= 128u ? 1u : 0u);
			},
			[&s](std::uint32_t value)
			{
				s.ReflectionProbes.Resolution = 64u << std::min(value, 2u);
			});
		DropdownFor(
			parent, "Reflection debug view", { "Off", "Sources (SSR / probe / environment)", "Probe age" },
			[&s]
			{
				return static_cast<std::uint32_t>(s.ScreenSpace.Reflections.Debug);
			},
			[&s](std::uint32_t value)
			{
				s.ScreenSpace.Reflections.Debug = static_cast<Swim::Render::ReflectionDebugView>(std::min(value, 2u));
			});

		if (sandbox && sandbox->GetLabFloor())
		{
			auto* floor = sandbox->GetLabFloor();
			DropdownFor(
				parent, "Reflection lab floor", { "Checker", "Green", "Rainbow (animated)", "Removed" },
				[floor]
				{
					return static_cast<std::uint32_t>(floor->GetMode());
				},
				[floor](std::uint32_t value)
				{
					floor->SetMode(static_cast<ReflectionLabFloor::Mode>(std::min(value, 3u)));
				});
		}

		// Render features added by the sandbox (Engine/Systems/Renderer/Features).
		if (sandbox)
		{
			CreateHeading(*document, parent, "Atmosphere");

			if (auto* clouds = sandbox->GetClouds())
			{
				CheckFor(parent, "Volumetric clouds", clouds->Enabled);
				SliderFor(parent, "Cloud coverage", 0.0f, 1.0f, clouds->Settings.Coverage, 2);
				CheckFor(parent, "Clouds in reflections (environment)", clouds->Settings.Environment);
				CheckFor(parent, "Clouds change the ambient light", s.EnvironmentFeatureAmbient);
			}

			if (auto* shafts = sandbox->GetSunShafts())
			{
				AddCheckbox(parent, "Sun shafts (god rays)", shafts->Enabled,
					[shafts](bool on)
					{
						shafts->Enabled = on;
					});
				AddSlider(parent, "Shaft intensity", 0.0f, 0.3f, shafts->Settings.Intensity, 3,
					[shafts](float value)
					{
						shafts->Settings.Intensity = value;
					});
			}

			if (auto* flare = sandbox->GetLensFlare())
			{
				AddCheckbox(parent, "Lens flare", flare->Enabled,
					[flare](bool on)
					{
						flare->Enabled = on;
					});
				AddSlider(parent, "Flare intensity", 0.0f, 1.5f, flare->Settings.Intensity, 2,
					[flare](float value)
					{
						flare->Settings.Intensity = value;
					});
			}
		}

		CheckFor(parent, "GPU particles", s.Particles);

		if (sandbox && sandbox->GetLensing())
		{
			// The black hole (bookmark 9): its lens on the GravitationalLensing feature.
			auto* lensing = sandbox->GetLensing();
			CreateHeading(*document, parent, "Black hole");
			CheckFor(parent, "Black hole (lensing + gas)", lensing->Enabled);
			const auto lensSlider = [&](const std::string& label, float min, float max, int decimals, float Engine::GravitationalLensing::Lens::*field)
			{
				const auto get = [lensing, field]
				{
					return lensing->Lenses.empty() ? 0.0f : lensing->Lenses.front().*field;
				};
				const auto slider = AddSlider(parent, label, min, max, get(), decimals,
					[lensing, field](float value)
					{
						for (auto& lens : lensing->Lenses)
						{
							lens.*field = value;
						}

					});
				Bind(slider, get, false);
			};
			lensSlider("Gas density", 0.0f, 4.0f, 2, &Engine::GravitationalLensing::Lens::GasDensity);
			lensSlider("Gas brightness", 0.0f, 12.0f, 1, &Engine::GravitationalLensing::Lens::GasBrightness);
			lensSlider("Gas orbit speed (rad/s at 3 Rs)", 0.0f, 8.0f, 1, &Engine::GravitationalLensing::Lens::GasSpeed);
			lensSlider("Bending strength", 0.0f, 3.0f, 2, &Engine::GravitationalLensing::Lens::Strength);
			AddSlider(parent, "Electron-shell rings", 0.0f, 3.0f, 3.0f, 0,
				[lensing](float value)
				{
					for (auto& lens : lensing->Lenses)
					{
						lens.Orbits = static_cast<std::uint32_t>(std::lround(value));
					}

				});
		}

		CreateHeading(*document, parent, "Sun and sky");
		AddSlider(parent, "Sun elevation", 2.0f, 89.0f, sandbox ? sandbox->GetSunElevation() : 40.0f, 0,
			[this](float value)
			{
				if (sandbox)
				{
					sandbox->SetSunAngles(value, sandbox->GetSunAzimuth());
				}

			});
		AddSlider(parent, "Sun azimuth", 0.0f, 360.0f, sandbox ? sandbox->GetSunAzimuth() : 200.0f, 0,
			[this](float value)
			{
				if (sandbox)
				{
					sandbox->SetSunAngles(sandbox->GetSunElevation(), value);
				}

			});
		SliderFor(parent, "Environment intensity", 0.0f, 3.0f, s.EnvironmentIntensity, 2);

		CreateHeading(*document, parent, "Debug");
		const auto debug = CreateDropdown(*document, parent, { "Lit", "Cluster light heatmap" }, static_cast<std::int32_t>(s.Debug));
		bindings.OnValue(debug.Root,
			[&s](float value)
			{
				s.Debug = value >= 1.0f ? Swim::Render::ForwardPlusDebugMode::ClusterHeatmap : Swim::Render::ForwardPlusDebugMode::None;
			});

		// Every profiling switch (Engine::RenderToggles: renderer passes, render features,
		// scene parts), live; and a 240-frame capture whose summary shows below.
		if (render->Toggles)
		{
			CreateHeading(*document, parent, "Profiling switches");
			const auto row = CreateRow(*document, parent);
			AddButton(row, "All on", "Every switch back on",
				[this]
				{
					render->Toggles->Set("all", true);
				});
			AddButton(row, "Profile 240 frames", "Measures every CPU zone and GPU pass (also: profile <frames> [warmup] [csv])",
				[this]
				{
					Command("profile 240 30");
					profilePending = true;
				});
			profileLabel = CreateLabel(*document, parent, "No capture yet.");
			Wrap(*document, profileLabel);
			auto* toggles = render->Toggles;

			for (const auto& toggle : toggles->List())
			{
				const std::string name = toggle.Name;
				const auto box = AddCheckbox(parent, name, toggle.Get(),
					[toggles, name](bool on)
					{
						toggles->Set(name, on);
					});
				CreateTooltip(*document, box, toggle.Description, 0.45f);
				Bind(
					box,
					[toggles, name]
					{
						return toggles->Get(name).value_or(false) ? 1.0f : 0.0f;
					},
					true);
			}
		}
	}

	void SandboxHud::BuildCamera(UiNodeId parent)
	{
		if (!render->Settings)
		{
			return;
		}

		auto& s = *render->Settings;
		auto& post = s.Post;

		// Presets derive every value below from a physical camera description
		// (Engine::CameraLook); each stays editable afterwards ("sandbox.camera <n>").
		if (sandbox)
		{
			CreateHeading(*document, parent, "Camera preset");
			std::vector<std::string> names;

			for (std::uint32_t p = 0; p < Engine::CameraPresetCount; ++p)
			{
				names.emplace_back(Engine::CameraPresetName(static_cast<Engine::CameraPreset>(p)));
			}

			DropdownFor(
				parent, "Look", names,
				[this]
				{
					return static_cast<std::uint32_t>(sandbox->GetCameraPreset());
				},
				[this](std::uint32_t value)
				{
					if (static_cast<Engine::CameraPreset>(value) != sandbox->GetCameraPreset())
					{
						sandbox->ApplyCameraPreset(static_cast<Engine::CameraPreset>(std::min(value, Engine::CameraPresetCount - 1)));
					}

				});
		}

		if (auto* cameras = scene->GetCameraSystem())
		{
			const auto fov = AddSlider(parent, "Field of view (vertical, degrees)", 20.0f, 110.0f, cameras->GetCamera().GetFieldOfView(), 0,
				[cameras](float value)
				{
					cameras->GetCamera().SetFieldOfView(value);
				});
			Bind(
				fov,
				[cameras]
				{
					return cameras->GetCamera().GetFieldOfView(); // The fly camera's zoom changes it too.
				},
				false);
		}

		if (sandbox && sandbox->GetDepthOfField())
		{
			auto* dof = sandbox->GetDepthOfField();
			CreateHeading(*document, parent, "Depth of field");
			CheckFor(parent, "Depth of field (thin lens)", dof->Enabled);
			SliderFor(parent, "Aperture (f-number)", 0.7f, 22.0f, dof->Settings.FNumber, 1);
			SliderFor(parent, "Focus distance (m, 0 = autofocus)", 0.0f, 60.0f, dof->Settings.FocusDistance, 1);
			SliderFor(parent, "Anamorphic squeeze (oval bokeh)", 1.0f, 2.0f, dof->Settings.AnamorphicSqueeze, 2);
			SliderFor(parent, "Max blur (px at 1080p)", 2.0f, 32.0f, dof->Settings.MaxBlurPixels, 0);
		}

		if (sandbox && sandbox->GetCameraLens())
		{
			auto& lens = *sandbox->GetCameraLens();
			CreateHeading(*document, parent, "Lens");
			CheckFor(parent, "Lens effects", lens.Enabled);
			SliderFor(parent, "Distortion (+ barrel, - pincushion)", -0.3f, 0.3f, lens.Settings.Distortion, 3);
			SliderFor(parent, "Fisheye", 0.0f, 1.0f, lens.Settings.Fisheye, 2);
			SliderFor(parent, "Chromatic aberration", 0.0f, 0.02f, lens.Settings.ChromaticAberration, 4);
			SliderFor(parent, "Corner softness (px)", 0.0f, 6.0f, lens.Settings.Softness, 2);
			SliderFor(parent, "Vignette (cos^4 exponent)", 0.0f, 2.0f, lens.Settings.Vignette, 2);
			SliderFor(parent, "Halation", 0.0f, 0.5f, lens.Settings.Halation, 3);
			SliderFor(parent, "Halation threshold", 0.5f, 8.0f, lens.Settings.HalationThreshold, 2);
			SliderFor(parent, "Filter red", 0.5f, 1.2f, lens.Settings.Filter[0], 2);
			SliderFor(parent, "Filter green", 0.5f, 1.2f, lens.Settings.Filter[1], 2);
			SliderFor(parent, "Filter blue", 0.5f, 1.2f, lens.Settings.Filter[2], 2);
		}

		if (sandbox && sandbox->GetFilmSensor())
		{
			auto& sensor = *sandbox->GetFilmSensor();
			CreateHeading(*document, parent, "Sensor / film");
			CheckFor(parent, "Grain and sharpening", sensor.Enabled);
			SliderFor(parent, "Grain", 0.0f, 0.08f, sensor.Settings.Grain, 3);
			SliderFor(parent, "Grain size (px at 1080p)", 0.5f, 4.0f, sensor.Settings.GrainSize, 1);
			SliderFor(parent, "Grain colour", 0.0f, 1.0f, sensor.Settings.GrainColor, 2);
			SliderFor(parent, "Sharpening", 0.0f, 1.0f, sensor.Settings.Sharpen, 2);
		}

		CreateHeading(*document, parent, "Exposure and bloom");
		DropdownFor(
			parent, "Exposure", { "Manual", "Automatic" },
			[&post]
			{
				return static_cast<std::uint32_t>(post.Exposure.Mode);
			},
			[&post](std::uint32_t value)
			{
				post.Exposure.Mode = static_cast<Swim::Render::ExposureMode>(std::min(value, 1u));
			});
		SliderFor(parent, "Exposure compensation (EV)", -4.0f, 4.0f, post.Exposure.Compensation, 1);
		SliderFor(parent, "Manual EV100", -2.0f, 16.0f, post.Exposure.ManualEv100, 1);
		CheckFor(parent, "Bloom", post.Bloom.Enabled);
		SliderFor(parent, "Bloom intensity", 0.0f, 0.2f, post.Bloom.Intensity, 3);
		SliderFor(parent, "Bloom threshold", 0.2f, 4.0f, post.Bloom.Threshold, 2);

		CreateHeading(*document, parent, "Colour and tone");
		SliderFor(parent, "White balance temperature", -100.0f, 100.0f, post.Grading.Temperature, 0);
		SliderFor(parent, "White balance tint", -100.0f, 100.0f, post.Grading.Tint, 0);
		SliderFor(parent, "Contrast", 0.5f, 1.6f, post.Grading.Contrast, 2);
		SliderFor(parent, "Saturation", 0.0f, 2.0f, post.Grading.Saturation, 2);
		DropdownFor(
			parent, "Tone mapper", { "Clamp", "Reinhard", "ACES", "PBR Neutral" },
			[&post]
			{
				return static_cast<std::uint32_t>(post.ToneMap.Operator);
			},
			[&post](std::uint32_t value)
			{
				post.ToneMap.Operator = static_cast<Swim::Render::ToneMapper>(std::min(value, 3u));
			});
	}

	void SandboxHud::BuildScene(UiNodeId parent)
	{
		sceneSummary = CreateLabel(*document, parent, "-");
		Wrap(*document, sceneSummary);
		const auto filterRow = CreateRow(*document, parent);
		CreateLabel(*document, filterRow, "Filter");
		UiTextEditOptions options;
		options.MaxBytes = 64;
		filterField = CreateTextField(*document, filterRow, options);
		{
			auto style = document->GetStyle(filterField);
			style.Width = UiLength::Pixels(250.0f);
			document->SetStyle(filterField, style);
		}
		bindings.OnText(filterField,
			[this](const std::string& text)
			{
				filter = Lower(text);
				RefreshEntities(true);
			});

		UiVirtualListDesc desc;
		desc.ItemCount = 0;
		desc.Style.Width = UiLength::Percent(1.0f);
		desc.Style.Height = UiLength::Pixels(230.0f);
		desc.Bind = [this](UiDocument& doc, UiNodeId row, std::uint32_t index)
		{
			SetLabelText(doc, row, index < listedNames.size() ? listedNames[index] : std::string());
		};
		entityList = std::make_unique<UiVirtualList>(*document, parent, std::move(desc));
		bindings.OnValue(entityList->GetRoot(),
			[this](float value)
			{
				const auto index = static_cast<std::int64_t>(value);

				if (index < 0 || static_cast<std::size_t>(index) >= listedEntities.size())
				{
					SetLabelText(*document, entityDetails, "Select an entity.");
					return;
				}

				const entt::entity selected = listedEntities[static_cast<std::size_t>(index)];
				std::ostringstream text;
				text << scene->GetEntityName(selected) << "  (id " << scene->GetSerializedEntityId(selected).Value << ")";

				if (const auto* tags = scene->GetTags(selected))
				{
					text << "\nTags:";

					for (const auto tag : tags->Values)
					{
						const auto name = scene->GetTagRegistry().GetName(tag);
						text << ' ' << (name.empty() ? std::string_view("?") : name);
					}
				}

				if (const auto* transform = scene->GetRegistry().try_get<Engine::Transform>(selected))
				{
					const glm::vec3 p = transform->GetWorldPosition(scene->GetRegistry());
					text << "\nPosition: " << Fixed(p.x, 2) << ", " << Fixed(p.y, 2) << ", " << Fixed(p.z, 2);
				}

				SetLabelText(*document, entityDetails, text.str());
			});
		entityDetails = CreateLabel(*document, parent, "Select an entity.");
		{
			auto style = document->GetStyle(entityDetails);
			style.TextWrap = Swim::Text::TextWrap::Word;
			style.Width = UiLength::Percent(1.0f);
			document->SetStyle(entityDetails, style);
		}
		const auto row = CreateRow(*document, parent);
		AddButton(row, "Focus", "Move the camera to look at the selected entity",
			[this]
			{
				const auto index = static_cast<std::int64_t>(document->GetValue(entityList->GetRoot()));

				if (index < 0 || static_cast<std::size_t>(index) >= listedEntities.size() || !scene->GetCameraSystem())
				{
					return;
				}

				const entt::entity selected = listedEntities[static_cast<std::size_t>(index)];

				if (const auto* transform = scene->GetRegistry().try_get<Engine::Transform>(selected))
				{
					const glm::vec3 target = transform->GetWorldPosition(scene->GetRegistry());
					scene->GetCameraSystem()->GetCamera().LookAt(target + glm::vec3(0.0f, 2.5f, 6.0f), target);
					scene->GetCameraSystem()->RequestCameraCut();
				}

			});
		AddButton(row, "Delete", "Destroy the selected entity (and its children)",
			[this]
			{
				const auto index = static_cast<std::int64_t>(document->GetValue(entityList->GetRoot()));

				if (index < 0 || static_cast<std::size_t>(index) >= listedEntities.size())
				{
					return;
				}

				const entt::entity selected = listedEntities[static_cast<std::size_t>(index)];

				if (scene->HasTag(selected, Engine::Tags::Ui) || scene->HasTag(selected, Engine::Tags::Camera))
				{
					SetLabelText(*document, entityDetails, "The camera and UI entities are protected.");
					return;
				}

				scene->GetCommandBuffer().Destroy(selected);
				entityTimer = 1.0f; // Refresh next frame.
			});
		spawnButton = AddButton(row, "Spawn...", "Drop a dynamic shape in front of the camera",
			[this]
			{
				OpenMenu(*document, spawnMenu, spawnButton, UiPopupSide::Below);
			});
		spawnMenu = CreateMenu(*document);
		const std::array<std::pair<const char*, Engine::BuiltinMesh>, 5> kinds{ { { "Cube", Engine::BuiltinMesh::Cube },
			{ "Sphere", Engine::BuiltinMesh::Sphere }, { "Capsule", Engine::BuiltinMesh::Capsule }, { "Torus", Engine::BuiltinMesh::Torus },
			{ "Cone", Engine::BuiltinMesh::Cone } } };

		for (std::size_t i = 0; i < kinds.size(); ++i)
		{
			if (i == 3)
			{
				AddMenuSeparator(*document, spawnMenu);
			}

			spawnItems[i] = AddMenuItem(*document, spawnMenu, kinds[i].first);
			const auto kind = kinds[i].second;
			bindings.OnClick(spawnItems[i],
				[this, kind]
				{
					if (sandbox)
					{
						sandbox->SpawnPrimitive(kind);
						entityTimer = 1.0f;
					}

				});
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

		text << "Physics impacts: " << sandbox->GetImpacts() << "\n";
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
			// Set the radio and show the section directly: the bindings do not report a
			// value that was already set before their first poll.
			document->SetValue(tabs, static_cast<float>(sandbox->GetRequestedTab()));
			ShowSection(sandbox->GetRequestedTab());
			sandbox->RequestTab(UINT32_MAX);
		}

		if (sandbox && bookmarkDropdown && document->GetValue(bookmarkDropdown) != static_cast<float>(sandbox->GetLastBookmark()))
		{
			document->SetValue(bookmarkDropdown, static_cast<float>(sandbox->GetLastBookmark()));
		}

		bindings.Process(*document);
		SyncControls();

		if (profilePending && render && render->Profiler && !render->Profiler->IsCapturing())
		{
			profilePending = false;
			SetLabelText(*document, profileLabel, Engine::FrameProfiler::Summary(render->Profiler->GetReport(), 6));
		} // After Process: a value the user just edited is already stored.

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

		if (sandbox && sandbox->GetInfoDocument())
		{
			infoBindings.Process(*sandbox->GetInfoDocument());
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
