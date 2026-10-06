#pragma once

#include "Engine/Systems/UI/UiDocument.h"

#include <string>

namespace Swim::UI
{

	struct UiModal
	{
		UiNodeId Root;	  // The full-canvas scrim (the popup; blocks the page below).
		UiNodeId Dialog;  // The centered panel [title, content, buttons].
		UiNodeId Title;	  // Empty without a title.
		UiNodeId Content; // Add the body here.
		UiNodeId Buttons; // A right-aligned row: AddModalButton.
	};

	struct UiModalDesc
	{
		std::string Title;
	};

	UiModal CreateModal(UiDocument& document, std::string title);

	UiNodeId AddModalButton(UiDocument& document, const UiModal& modal, std::string label);

	// Opens it modal: only the dialog takes input, Tab stays inside, focus goes to its first
	// focusable node and returns on close. Presses on the scrim do not close it; Escape does.
	void OpenModal(UiDocument& document, const UiModal& modal);

} // namespace Swim::UI
