#include "Engine/Systems/UI/UiWidgets.h"

#include <stdexcept>

namespace Swim::UI
{

	UiWidget CreateWidget(UiDocument& document, UiNodeId parent, std::string_view type)
	{
		auto& registry = UiControlRegistry::Global();

		if (!registry.Contains(type))
		{
			throw std::invalid_argument("Unknown UI control type: " + std::string(type));
		}

		if (!document.Contains(parent))
		{
			throw std::invalid_argument("UI widget parent is not in the document");
		}

		if (const auto build = registry.FindBuilder(type))
		{
			return UiWidget(document, build(document, parent));
		}

		const auto node = document.Create(parent);
		document.SetControl(node, type);
		return UiWidget(document, node);
	}

} // namespace Swim::UI
