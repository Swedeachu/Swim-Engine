#include "Game/Ui/SandboxHudShared.h"

// The panel's tabs: simulation, rendering, camera and scene.
namespace Game
{

	using namespace SandboxHudDetail;

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
		document->OnValue(bookmarks.Root,
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
		document->OnClick(resetCancel,
			[this]
			{
				document->ClosePopup(resetModal.Root);
			});
		document->OnClick(resetConfirm,
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
		CheckFor(parent, "Planar reflections (mirrors)", s.PlanarReflections.Enabled);
		AddSlider(parent, "Probe faces per frame", 1.0f, 32.0f, float(s.ReflectionProbes.FacesPerFrame), 0,
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
			const auto lensSlider =
				[&](const std::string& label, float min, float max, int decimals, float Engine::GravitationalLensing::Lens::* field)
			{
				BoundSlider(
					parent, label, min, max, decimals,
					[lensing, field]
					{
						return lensing->Lenses.empty() ? 0.0f : lensing->Lenses.front().*field;
					},
					[lensing, field](float value)
					{
						for (auto& lens : lensing->Lenses)
						{
							lens.*field = value;
						}
					});
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
		document->OnValue(debug.Root,
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
				const auto box = BoundCheckbox(
					parent, name,
					[toggles, name]
					{
						return toggles->Get(name).value_or(false);
					},
					[toggles, name](bool on)
					{
						toggles->Set(name, on);
					});
				CreateTooltip(*document, box, toggle.Description, 0.45f);
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
			BoundSlider(
				parent, "Field of view (vertical, degrees)", 20.0f, 110.0f, 0,
				[cameras]
				{
					return cameras->GetCamera().GetFieldOfView(); // The fly camera's zoom changes it too.
				},
				[cameras](float value)
				{
					cameras->GetCamera().SetFieldOfView(value);
				});
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
		document->OnText(filterField,
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
		document->OnValue(entityList->GetRoot(),
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
			document->OnClick(spawnItems[i],
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

} // namespace Game
