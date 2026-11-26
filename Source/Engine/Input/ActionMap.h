#pragma once

#include "Engine/Input/InputMap.h"
#include "Engine/Input/InputSystem.h"

#include <cstdint>
#include <functional>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Swim::Input
{

	using ActionId = uint32_t;
	using InputContextId = uint32_t;

	inline constexpr ActionId InvalidAction = std::numeric_limits<ActionId>::max();

	inline constexpr InputContextId InvalidInputContext = std::numeric_limits<InputContextId>::max();

	// A layer of actions that can be switched on and off (gameplay, menus, a vehicle, a
	// spectator view). While an active context with Captures is on, every context with a
	// lower priority is blocked: its held actions report a release that frame and stay
	// released until their inputs are let go after the block ends.
	struct InputContextDesc
	{
		std::string Name;
		int32_t Priority = 0;
		bool Captures = false;
		bool Active = false;
	};

	struct ActionDesc
	{
		std::string Name;
		std::string Description;
		// Slash-separated settings path, for example "Controls/Movement".
		std::string Category;
		InputContextId Context = 0;
		std::vector<InputBinding> DefaultBindings;
		// Console line run when the action is pressed (empty: none).
		std::string Command;
		// Axis bindings below this magnitude count as released.
		float Deadzone = 0.2f;
	};

	struct ActionState
	{
		float Value = 0.0f;
		bool Down = false;
		bool Pressed = false;
		bool Released = false;
	};

	// Named actions with rebindable inputs, evaluated once per frame (no allocation in
	// Update). Bindings are physical (scan codes), several per action. Contexts gate which
	// actions are live. Bindings that differ from the defaults are written as console lines
	// ("bind <action> <binding>..." or "bind <action> none") through WriteConfig, and a bind
	// line for an action that is not registered yet is kept until it registers.
	//
	// Two built-in contexts: DefaultContext (priority 0, always active) and the text
	// context (priority 1000, captures) that the engine turns on while a text field or
	// the console takes typing.
	class ActionMap
	{

	public:

		using CommandRunner = std::function<void(std::string_view line)>;

		static constexpr InputContextId DefaultContext = 0;

		ActionMap();

		InputContextId RegisterContext(InputContextDesc desc);

		InputContextId FindContext(std::string_view name) const;

		void SetContextActive(InputContextId context, bool active);

		bool IsContextActive(InputContextId context) const;

		// True when the context is active and no capturing context above it is.
		bool IsContextLive(InputContextId context) const;

		InputContextId GetTextContext() const { return textContext; }

		// Registers an action; an existing name returns the existing id (bindings kept).
		ActionId RegisterAction(ActionDesc desc);

		ActionId FindAction(std::string_view name) const;

		std::size_t GetActionCount() const { return actions.size(); }

		const ActionDesc& GetDesc(ActionId action) const { return actions[action].Desc; }

		const std::vector<InputBinding>& GetBindings(ActionId action) const { return actions[action].Bindings; }

		void SetBindings(ActionId action, std::vector<InputBinding> bindings);

		void AddBinding(ActionId action, const InputBinding& binding);

		bool RemoveBinding(ActionId action, const InputBinding& binding);

		void ResetBindings(ActionId action);

		void ResetAllBindings();

		bool HasDefaultBindings(ActionId action) const;

		// Other actions that use the same input in a context that can be live at the same
		// time (the same context, or either one is DefaultContext).
		std::vector<ActionId> FindConflicts(ActionId action, const InputBinding& binding) const;

		// Evaluates every action against this frame's input and runs the commands of the
		// actions pressed this frame.
		void Update(const InputSystem& input, const CommandRunner& runCommand = {});

		const ActionState& GetState(ActionId action) const { return actions[action].State; }

		bool IsDown(ActionId action) const { return action < actions.size() && actions[action].State.Down; }

		bool WasPressed(ActionId action) const { return action < actions.size() && actions[action].State.Pressed; }

		bool WasReleased(ActionId action) const { return action < actions.size() && actions[action].State.Released; }

		float GetValue(ActionId action) const { return action < actions.size() ? actions[action].State.Value : 0.0f; }

		// positive - negative, for paired directional actions (move left/right).
		float GetAxis(ActionId positive, ActionId negative) const { return GetValue(positive) - GetValue(negative); }

		// This frame's relative mouse motion scaled by the sensitivity (or the look curve),
		// Y inverted when set; zero while `context` is not live.
		Platform::Float2 GetLookDelta(InputContextId context = DefaultContext) const;

		// The default scaling: look units per mouse count.
		void SetMouseSensitivity(float value) { mouseSensitivity = value; }

		// The raw sensitivity setting (look_sensitivity, 0..100) handed to the look curve.
		void SetLookSetting(float value) { lookSetting = value; }

		float GetLookSetting() const { return lookSetting; }

		// A game's own sensitivity curve: this frame's raw mouse counts and the raw setting in,
		// look delta out (in whatever unit the game turns by, for example degrees), used
		// instead of the default scaling. Y inversion still applies afterwards. Empty
		// restores the default.
		using LookCurve = std::function<Platform::Float2(Platform::Float2 counts, float setting)>;

		void SetLookCurve(LookCurve curve) { lookCurve = std::move(curve); }

		// This frame's raw relative mouse motion (counts), whatever the context.
		Platform::Float2 GetRawMouseDelta() const { return mouseDelta; }

		void SetInvertMouseY(bool value) { invertMouseY = value; }

		// The "bind" console command: "bind <action>" prints, "bind <action> none" clears,
		// "bind <action> <binding>..." replaces. Unknown actions are kept as pending.
		// Throws std::invalid_argument for an unparsable binding.
		void ExecuteBind(const std::vector<std::string>& arguments);

		void WriteConfig(std::string& out) const;

		// Called after any binding change made through this class (settings dirty tracking).
		void SetChangedCallback(std::function<void()> callback) { changed = std::move(callback); }

	private:

		struct Context
		{
			InputContextDesc Desc;
		};

		struct Action
		{
			ActionDesc Desc;
			std::vector<InputBinding> Bindings;
			ActionState State;
			bool Suppressed = false;
			bool WasLive = false;
		};

		void NotifyChanged();

		std::vector<Context> contexts;
		std::vector<Action> actions;
		std::unordered_map<std::string, ActionId> actionIndex;
		std::unordered_map<std::string, std::vector<std::string>> pendingBinds;
		InputContextId textContext = InvalidInputContext;
		Platform::Float2 mouseDelta{};
		float mouseSensitivity = 1.0f;
		float lookSetting = 50.0f;
		LookCurve lookCurve;
		bool invertMouseY = false;
		std::function<void()> changed;

	};

} // namespace Swim::Input
