#pragma once

#include "Engine/Systems/UI/UiDocument.h"

#include <string>

namespace Swim::UI
{

	struct UiTooltipDesc
	{
		std::string Text;
		float DelaySeconds = 0.5f;
	};

	// A themed tooltip (a hidden child of the root) registered on target.
	UiNodeId CreateTooltip(UiDocument& document, UiNodeId target, std::string text, float delaySeconds = 0.5f);

} // namespace Swim::UI
