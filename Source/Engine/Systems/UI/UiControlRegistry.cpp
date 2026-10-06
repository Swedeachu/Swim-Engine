#include "Engine/Systems/UI/UiControlRegistry.h"

#include "Engine/Systems/UI/Internal/UiBuiltInControls.h"

#include <stdexcept>

namespace Swim::UI
{

	UiControlRegistry& UiControlRegistry::Global()
	{
		// Built-ins are registered explicitly here (not by static registrars) so they exist even
		// when the linker drops a widget's translation unit that nothing else references.
		static UiControlRegistry registry = []
		{
			UiControlRegistry built;
			Internal::RegisterBuiltInControls(built);
			return built;
		}();
		return registry;
	}

	void UiControlRegistry::Register(std::string_view name, Factory factory, Builder builder)
	{
		if (name.empty() || !factory)
		{
			throw std::invalid_argument("A UI control type needs a name and a factory");
		}

		if (Find(name))
		{
			throw std::invalid_argument("UI control type '" + std::string(name) + "' is already registered");
		}

		entries.push_back({ std::string(name), factory, builder });
	}

	const UiControlRegistry::Entry* UiControlRegistry::Find(std::string_view name) const
	{
		for (const auto& entry : entries)
		{
			if (entry.Name == name)
			{
				return &entry;
			}
		}

		return nullptr;
	}

	bool UiControlRegistry::Contains(std::string_view name) const
	{
		return Find(name) != nullptr;
	}

	std::unique_ptr<UiControlBehavior> UiControlRegistry::Create(std::string_view name) const
	{
		const auto* entry = Find(name);

		if (!entry)
		{
			return nullptr;
		}

		auto behavior = entry->Create();

		if (behavior)
		{
			behavior->typeName = entry->Name;
		}

		return behavior;
	}

	UiControlRegistry::Builder UiControlRegistry::FindBuilder(std::string_view name) const
	{
		const auto* entry = Find(name);
		return entry ? entry->Build : nullptr;
	}

	std::vector<std::string> UiControlRegistry::GetNames() const
	{
		std::vector<std::string> names;
		names.reserve(entries.size());

		for (const auto& entry : entries)
		{
			names.push_back(entry.Name);
		}

		return names;
	}

	std::string_view UiControlTypeName(UiControlKind kind)
	{
		switch (kind)
		{
		case UiControlKind::Button: return "Button";
		case UiControlKind::Checkbox: return "Checkbox";
		case UiControlKind::Toggle: return "Toggle";
		case UiControlKind::Slider: return "Slider";
		case UiControlKind::ScrollBar: return "ScrollBar";
		case UiControlKind::RadioGroup: return "RadioGroup";
		case UiControlKind::ListView: return "ListView";
		case UiControlKind::Dropdown: return "Dropdown";
		case UiControlKind::Option: return "Option";
		default: return {};
		}
	}

} // namespace Swim::UI
