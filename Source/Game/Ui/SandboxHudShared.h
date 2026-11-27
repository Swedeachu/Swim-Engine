#pragma once
#include "Game/Ui/SandboxHud.h"

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

// Helpers shared by SandboxHud.cpp and SandboxHudTabs.cpp.
namespace Game::SandboxHudDetail
{

	using namespace Swim::UI;

	// Authored in sRGB (UI colours are linear): near-black panels, a full-strength
	// accent border and accent text.
	const UiColor PanelColor = UiSrgbHex(0x090c12, 0.95f);
	const UiColor PanelBorder = UiSrgbHex(0x1f7aff, 0.85f);
	const UiColor AccentText = UiSrgbHex(0x5ea2ff);

	inline std::string Lower(std::string text)
	{
		std::transform(text.begin(), text.end(), text.begin(),
			[](unsigned char c)
			{
				return static_cast<char>(std::tolower(c));
			});
		return text;
	}

	inline void Wrap(UiDocument& document, UiNodeId label)
	{
		auto style = document.GetStyle(label);
		style.TextWrap = Swim::Text::TextWrap::Word;
		style.Width = UiLength::Percent(1.0f);
		document.SetStyle(label, style);
	}

	inline std::string Fixed(double value, int decimals)
	{
		char buffer[64];
		std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, value);
		return buffer;
	}

} // namespace Game::SandboxHudDetail
