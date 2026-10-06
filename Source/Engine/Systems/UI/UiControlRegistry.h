#pragma once

#include "Engine/Systems/UI/UiControlBehavior.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Swim::UI
{

	// Control types by name. The engine's controls (Button, Checkbox, Toggle, Slider,
	// ScrollBar, RadioGroup, ListView, Dropdown, Option) register here exactly like gameplay
	// controls do, so every control goes through the same document plumbing.
	//
	// Gameplay registers a control from its own .cpp (no engine edits):
	//
	//   SWIM_UI_CONTROL(HealthBarControl, "Game.HealthBar");
	//
	// or explicitly: UiControlRegistry::Global().Register<HealthBarControl>("Game.HealthBar").
	// A type may also provide a default widget (`Build`): the node tree it needs, so data-driven
	// UI can create it by name (CreateWidget(document, parent, "Game.HealthBar")).
	class UiControlRegistry
	{

	  public:

		using Factory = std::unique_ptr<UiControlBehavior> (*)();
		// Builds a default widget of this type under parent and returns its control node.
		using Builder = UiNodeId (*)(UiDocument& document, UiNodeId parent);

		// The process-wide registry (built-in controls are registered on first use).
		static UiControlRegistry& Global();

		// Throws std::invalid_argument for an empty or taken name, or no factory.
		void Register(std::string_view name, Factory factory, Builder builder = nullptr);

		template <class T> void Register(std::string_view name, Builder builder = nullptr)
		{
			Register(
				name,
				[]() -> std::unique_ptr<UiControlBehavior>
				{
					return std::make_unique<T>();
				},
				builder);
		}

		bool Contains(std::string_view name) const;

		// A new instance (its GetTypeName() is the name); null for unknown names.
		std::unique_ptr<UiControlBehavior> Create(std::string_view name) const;

		// The default widget builder of a type (null without one or for unknown names).
		Builder FindBuilder(std::string_view name) const;

		std::vector<std::string> GetNames() const;

	  private:

		struct Entry
		{
			std::string Name;
			Factory Create = nullptr;
			Builder Build = nullptr;
		};

		const Entry* Find(std::string_view name) const;

		std::vector<Entry> entries;

	};

	// Registers a control type when its translation unit is loaded (put it in the control's .cpp).
#define SWIM_UI_CONTROL(Type, Name)                                                                                                        \
	static const bool SwimUiControlRegistered_##Type = (::Swim::UI::UiControlRegistry::Global().Register<Type>(Name), true)

	// The same with a default widget builder (UiNodeId (*)(UiDocument&, UiNodeId parent)).
#define SWIM_UI_WIDGET(Type, Name, Builder)                                                                                                \
	static const bool SwimUiControlRegistered_##Type = (::Swim::UI::UiControlRegistry::Global().Register<Type>(Name, Builder), true)

} // namespace Swim::UI
