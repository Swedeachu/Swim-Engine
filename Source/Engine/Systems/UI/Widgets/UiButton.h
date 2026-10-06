#pragma once

#include "Engine/Systems/UI/UiControlBehavior.h"

#include <string>

namespace Swim::UI
{

	// A button: Click on release inside, or Enter/Space/gamepad A while focused. Everything
	// a button does is the document's default (press, hover, focus, Click), so its behaviour
	// only refuses values.
	class UiButtonControl : public UiControlBehavior
	{

	  public:

		void SetValue(UiControlContext& context, float value) override;

	};

	struct UiButtonDesc
	{
		std::string Label;
	};

	// A themed text button.
	UiNodeId CreateButton(UiDocument& document, UiNodeId parent, std::string label);

	inline UiNodeId CreateButton(UiDocument& document, UiNodeId parent, const UiButtonDesc& desc)
	{
		return CreateButton(document, parent, desc.Label);
	}

} // namespace Swim::UI
