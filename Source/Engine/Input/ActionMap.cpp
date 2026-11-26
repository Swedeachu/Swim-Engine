#include "Engine/Input/ActionMap.h"
#include "Engine/Input/InputNames.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace Swim::Input
{

	namespace
	{

		bool SameBindings(const std::vector<InputBinding>& a, const std::vector<InputBinding>& b)
		{
			if (a.size() != b.size())
			{
				return false;
			}

			for (std::size_t i = 0; i < a.size(); ++i)
			{
				if (!SameInput(a[i], b[i]))
				{
					return false;
				}
			}

			return true;
		}

		std::vector<InputBinding> ParseBindings(const std::vector<std::string>& texts, std::size_t first)
		{
			std::vector<InputBinding> bindings;

			if (texts.size() == first + 1 && texts[first] == "none")
			{
				return bindings;
			}

			for (std::size_t i = first; i < texts.size(); ++i)
			{
				InputBinding binding;

				if (!ParseBinding(texts[i], binding))
				{
					throw std::invalid_argument("unknown input '" + texts[i] + "'");
				}

				bindings.push_back(binding);
			}

			return bindings;
		}

	} // namespace

	ActionMap::ActionMap()
	{
		contexts.push_back({ { "default", 0, false, true } });
		textContext = RegisterContext({ "text", 1000, true, false });
	}

	InputContextId ActionMap::RegisterContext(InputContextDesc desc)
	{
		const InputContextId existing = FindContext(desc.Name);

		if (existing != InvalidInputContext)
		{
			return existing;
		}

		contexts.push_back({ std::move(desc) });
		return static_cast<InputContextId>(contexts.size() - 1);
	}

	InputContextId ActionMap::FindContext(std::string_view name) const
	{
		for (std::size_t i = 0; i < contexts.size(); ++i)
		{
			if (contexts[i].Desc.Name == name)
			{
				return static_cast<InputContextId>(i);
			}
		}

		return InvalidInputContext;
	}

	void ActionMap::SetContextActive(InputContextId context, bool active)
	{
		if (context < contexts.size() && context != DefaultContext)
		{
			contexts[context].Desc.Active = active;
		}
	}

	bool ActionMap::IsContextActive(InputContextId context) const
	{
		return context < contexts.size() && contexts[context].Desc.Active;
	}

	bool ActionMap::IsContextLive(InputContextId context) const
	{
		if (!IsContextActive(context))
		{
			return false;
		}

		const int32_t priority = contexts[context].Desc.Priority;

		for (std::size_t i = 0; i < contexts.size(); ++i)
		{
			const auto& other = contexts[i].Desc;

			if (i != context && other.Active && other.Captures && other.Priority > priority)
			{
				return false;
			}
		}

		return true;
	}

	ActionId ActionMap::RegisterAction(ActionDesc desc)
	{
		if (const auto found = actionIndex.find(desc.Name); found != actionIndex.end())
		{
			return found->second;
		}

		if (desc.Name.empty())
		{
			throw std::invalid_argument("Input actions need a name");
		}

		if (desc.Context >= contexts.size())
		{
			desc.Context = DefaultContext;
		}

		const auto id = static_cast<ActionId>(actions.size());
		Action action;
		action.Bindings = desc.DefaultBindings;

		for (auto& binding : action.Bindings)
		{
			binding.Action = id;
		}

		action.Desc = std::move(desc);
		actionIndex.emplace(action.Desc.Name, id);
		actions.push_back(std::move(action));

		if (const auto pending = pendingBinds.find(actions[id].Desc.Name); pending != pendingBinds.end())
		{
			try
			{
				actions[id].Bindings = ParseBindings(pending->second, 0);
			}
			catch (const std::exception& error)
			{
				std::cerr << "[Input] bind " << actions[id].Desc.Name << ": " << error.what() << '\n';
			}

			pendingBinds.erase(pending);
		}

		return id;
	}

	ActionId ActionMap::FindAction(std::string_view name) const
	{
		const auto found = actionIndex.find(std::string(name));
		return found == actionIndex.end() ? InvalidAction : found->second;
	}

	void ActionMap::SetBindings(ActionId action, std::vector<InputBinding> bindings)
	{
		if (action >= actions.size())
		{
			return;
		}

		for (auto& binding : bindings)
		{
			binding.Action = action;
		}

		actions[action].Bindings = std::move(bindings);
		NotifyChanged();
	}

	void ActionMap::AddBinding(ActionId action, const InputBinding& binding)
	{
		if (action >= actions.size())
		{
			return;
		}

		auto& bindings = actions[action].Bindings;

		for (const auto& existing : bindings)
		{
			if (SameInput(existing, binding))
			{
				return;
			}
		}

		bindings.push_back(binding);
		bindings.back().Action = action;
		NotifyChanged();
	}

	bool ActionMap::RemoveBinding(ActionId action, const InputBinding& binding)
	{
		if (action >= actions.size())
		{
			return false;
		}

		const auto removed = std::erase_if(actions[action].Bindings,
			[&](const InputBinding& existing)
			{
				return SameInput(existing, binding);
			});

		if (removed != 0)
		{
			NotifyChanged();
		}

		return removed != 0;
	}

	void ActionMap::ResetBindings(ActionId action)
	{
		if (action < actions.size())
		{
			SetBindings(action, actions[action].Desc.DefaultBindings);
		}
	}

	void ActionMap::ResetAllBindings()
	{
		for (auto& action : actions)
		{
			action.Bindings = action.Desc.DefaultBindings;
		}

		NotifyChanged();
	}

	bool ActionMap::HasDefaultBindings(ActionId action) const
	{
		return action < actions.size() && SameBindings(actions[action].Bindings, actions[action].Desc.DefaultBindings);
	}

	std::vector<ActionId> ActionMap::FindConflicts(ActionId action, const InputBinding& binding) const
	{
		std::vector<ActionId> conflicts;
		const InputContextId context = action < actions.size() ? actions[action].Desc.Context : DefaultContext;

		for (std::size_t i = 0; i < actions.size(); ++i)
		{
			const auto other = actions[i].Desc.Context;

			if (i == action || (other != context && other != DefaultContext && context != DefaultContext))
			{
				continue;
			}

			for (const auto& existing : actions[i].Bindings)
			{
				if (SameInput(existing, binding))
				{
					conflicts.push_back(static_cast<ActionId>(i));
					break;
				}
			}
		}

		return conflicts;
	}

	void ActionMap::Update(const InputSystem& input, const CommandRunner& runCommand)
	{
		int32_t blockBelow = std::numeric_limits<int32_t>::min();

		for (const auto& context : contexts)
		{
			if (context.Desc.Active && context.Desc.Captures)
			{
				blockBelow = std::max(blockBelow, context.Desc.Priority);
			}
		}

		const float wheel = input.GetMouseScrollDelta();

		for (auto& action : actions)
		{
			const auto& context = contexts[action.Desc.Context].Desc;
			const bool live = context.Active && context.Priority >= blockBelow;
			float value = 0.0f;

			for (const auto& binding : action.Bindings)
			{
				switch (binding.Type)
				{
				case InputBindingType::Key:
					value = std::max(value, input.IsKeyDown(binding.Key) ? 1.0f : 0.0f);
					break;
				case InputBindingType::ScanCode:
					value = std::max(value, input.IsScanCodeDown(binding.PhysicalKey) ? 1.0f : 0.0f);
					break;
				case InputBindingType::MouseButton:
					value = std::max(value, input.IsMouseButtonDown(binding.Mouse) ? 1.0f : 0.0f);
					break;
				case InputBindingType::MouseWheel:
					value = std::max(value, wheel * binding.Scale > 0.0f ? 1.0f : 0.0f);
					break;
				case InputBindingType::GamepadButton:
					value = std::max(value, input.IsAnyGamepadButtonDown(binding.Gamepad) ? 1.0f : 0.0f);
					break;
				case InputBindingType::GamepadAxis:
				{
					const float axis = input.GetAnyGamepadAxis(binding.Axis) * (binding.Scale >= 0.0f ? 1.0f : -1.0f);
					value = std::max(value, axis > action.Desc.Deadzone ? axis : 0.0f);
					break;
				}
				}
			}

			const bool held = value > 0.0f;

			// Held while the context was blocked (or when it came back): wait for a release.
			if (!live || (!action.WasLive && held))
			{
				action.Suppressed = held;
			}
			else if (action.Suppressed && !held)
			{
				action.Suppressed = false;
			}

			action.WasLive = live;
			const bool down = live && held && !action.Suppressed;
			auto& state = action.State;
			state.Pressed = down && !state.Down;
			state.Released = !down && state.Down;
			state.Down = down;
			state.Value = down ? value : 0.0f;

			if (state.Pressed && runCommand && !action.Desc.Command.empty())
			{
				runCommand(action.Desc.Command);
			}
		}

		mouseDelta = input.GetMousePositionDelta();
	}

	Platform::Float2 ActionMap::GetLookDelta(InputContextId context) const
	{
		if (!IsContextLive(context))
		{
			return {};
		}

		const float invert = invertMouseY ? -1.0f : 1.0f;

		if (lookCurve)
		{
			const Platform::Float2 look = lookCurve(mouseDelta, lookSetting);
			return { look.X, look.Y * invert };
		}

		return { mouseDelta.X * mouseSensitivity, mouseDelta.Y * mouseSensitivity * invert };
	}

	void ActionMap::ExecuteBind(const std::vector<std::string>& arguments)
	{
		if (arguments.empty())
		{
			throw std::invalid_argument("usage: bind <action> [none | <binding>...]");
		}

		const ActionId action = FindAction(arguments[0]);

		if (arguments.size() == 1)
		{
			if (action == InvalidAction)
			{
				throw std::invalid_argument("no input action " + arguments[0]);
			}

			std::string text = arguments[0] + " =";

			for (const auto& binding : actions[action].Bindings)
			{
				text += " " + FormatBinding(binding);
			}

			std::cout << text << (HasDefaultBindings(action) ? "" : " *") << '\n';
			return;
		}

		if (action == InvalidAction)
		{
			pendingBinds.insert_or_assign(arguments[0], std::vector<std::string>(arguments.begin() + 1, arguments.end()));
			NotifyChanged();
			return;
		}

		auto bindings = ParseBindings(arguments, 1);

		for (const auto& binding : bindings)
		{
			for (const ActionId other : FindConflicts(action, binding))
			{
				std::cout << "[Input] " << FormatBinding(binding) << " is also bound to " << actions[other].Desc.Name << '\n';
			}
		}

		SetBindings(action, std::move(bindings));
	}

	void ActionMap::WriteConfig(std::string& out) const
	{
		for (std::size_t i = 0; i < actions.size(); ++i)
		{
			if (HasDefaultBindings(static_cast<ActionId>(i)))
			{
				continue;
			}

			out += "bind " + actions[i].Desc.Name;

			if (actions[i].Bindings.empty())
			{
				out += " none";
			}

			for (const auto& binding : actions[i].Bindings)
			{
				const auto text = FormatBinding(binding);

				if (!text.empty())
				{
					out += " " + text;
				}
			}

			out += '\n';
		}

		for (const auto& [name, bindings] : pendingBinds)
		{
			out += "bind " + name;

			for (const auto& binding : bindings)
			{
				out += " " + binding;
			}

			out += '\n';
		}
	}

	void ActionMap::NotifyChanged()
	{
		if (changed)
		{
			changed();
		}
	}

} // namespace Swim::Input
