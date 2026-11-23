#pragma once

#include "ConsoleVariable.h"

#include <algorithm>
#include <filesystem>
#include <memory>
#include <charconv>
#include <functional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Swim::Commands
{

	struct CommandDesc
	{
		std::string Name;
		std::string Description;
		// Slash-separated grouping for listings, for example "Video/Display".
		std::string Category;
		// Argument summary shown by help, for example "<name> [value]".
		std::string Usage;
	};

	// A listing entry: a command, or a variable (Variable non-null).
	struct CommandInfo
	{
		std::string Name;
		std::string Description;
		std::string Category;
		std::string Usage;
		const ConsoleVariable* Variable = nullptr;
	};

	// In-process commands and typed console variables. This service owns no transport or
	// per-frame lifecycle.
	//
	// A variable is dispatched like a command: "name" prints it, "name value" sets it.
	// Config files are plain console lines ("name value", "bind ..."), so --exec and
	// ExecuteFile run the same text that SaveConfig writes. A config line naming a
	// variable that is not registered yet is kept and applied when that variable
	// registers, and kept on save, so settings of modules that load late survive.
	class CommandRegistry
	{

	public:

		using Callback = std::function<void(const std::vector<std::string>&)>;
		using ChangeListener = std::function<void(const ConsoleVariable&)>;
		using ConfigWriter = std::function<void(std::string& out)>;

		void Register(std::string name, Callback callback);

		void Register(CommandDesc desc, Callback callback);

		// A command with typed arguments: "name 1 2.5 on text" -> callback(int, float, bool,
		// std::string). Supported: integers, floating point, bool (1/0, true/false, on/off)
		// and std::string. Missing trailing arguments take their defaults (value-initialized);
		// extra or unparsable ones throw std::invalid_argument naming the command's usage.
		template <typename... Args, typename F> void RegisterTyped(std::string name, F callback);

		// Registers a variable. Registering a name that already is a variable returns the
		// existing one with its value kept (scenes may register on every load); a name
		// taken by a command throws std::invalid_argument. A pending config value for the
		// name is applied before this returns.
		ConsoleVariable& RegisterVariable(VariableDesc desc);

		ConsoleVariable& AddBool(std::string name, bool defaultValue, std::string description, std::string category = {},
			VariableFlags flags = VariableFlags::None);

		ConsoleVariable& AddInt(std::string name, int64_t defaultValue, int64_t min, int64_t max, std::string description,
			std::string category = {}, VariableFlags flags = VariableFlags::None);

		ConsoleVariable& AddFloat(std::string name, double defaultValue, double min, double max, std::string description,
			std::string category = {}, VariableFlags flags = VariableFlags::None);

		ConsoleVariable& AddEnum(std::string name, std::vector<std::string> choices, uint32_t defaultIndex, std::string description,
			std::string category = {}, VariableFlags flags = VariableFlags::None);

		ConsoleVariable& AddString(std::string name, std::string defaultValue, std::string description, std::string category = {},
			VariableFlags flags = VariableFlags::None);

		ConsoleVariable& AddColor(std::string name, VariableColor defaultValue, std::string description, std::string category = {},
			VariableFlags flags = VariableFlags::None);

		ConsoleVariable* FindVariable(std::string_view name);

		const ConsoleVariable* FindVariable(std::string_view name) const;

		// Every variable, sorted by name.
		std::vector<ConsoleVariable*> GetVariables() const;

		// Every command and variable with metadata, sorted by name (settings UI, tools).
		std::vector<CommandInfo> GetCommandInfos() const;

		// Every registered name (commands and variables), sorted (the console's "help").
		std::vector<std::string> GetNames() const;

		bool Contains(std::string_view name) const;

		// Removes a command or a variable (references to the variable dangle afterwards).
		bool Unregister(std::string_view name);

		void Clear();

		bool Dispatch(std::string_view name, const std::vector<std::string>& args) const;

		bool ParseAndDispatch(std::string_view command) const;

		// Runs every line of a config file. Blank lines and lines starting with "//" or "#"
		// are skipped; a line for an unknown name is kept as a pending value (see the class
		// comment); a failing line is reported on stderr and skipped. False when the file
		// cannot be read.
		bool ExecuteFile(const std::filesystem::path& path);

		// Runs one config line with ExecuteFile's rules.
		void ExecuteConfigLine(std::string_view line);

		// Writes every variable carrying `flags` whose value differs from its default, the
		// pending values of unregistered variables, then every config writer's lines
		// (bindings and the like). Atomic (written next to the target, then renamed).
		bool SaveConfig(const std::filesystem::path& path, VariableFlags flags = VariableFlags::Saved) const;

		// Adds lines to SaveConfig under a key (replaces the writer of the same key).
		void SetConfigWriter(std::string key, ConfigWriter writer);

		void RemoveConfigWriter(std::string_view key);

		// True after a saved variable changed or MarkConfigDirty, until MarkConfigSaved.
		bool IsConfigDirty() const { return configDirty; }

		void MarkConfigDirty() { configDirty = true; }

		void MarkConfigSaved() { configDirty = false; }

		// From now on RequiresRestart variables latch changes instead of applying them.
		void SetStartupComplete();

		// Called after any variable changes (after the variable's own subscribers).
		ConsoleVariable::SubscriptionId SubscribeVariableChanges(ChangeListener listener);

		void UnsubscribeVariableChanges(ConsoleVariable::SubscriptionId id);

		// Quotes a token for a console line when it needs quoting (spaces, quotes, empty).
		static std::string QuoteArgument(std::string_view text);

		template <typename T> static bool ParseArgument(const std::string& text, T& value)
		{
			using U = std::remove_cvref_t<T>;

			if constexpr (std::is_same_v<U, std::string>)
			{
				value = text;
				return true;
			}
			else if constexpr (std::is_same_v<U, bool>)
			{
				std::string lower = text;
				std::transform(lower.begin(), lower.end(), lower.begin(),
					[](unsigned char c)
					{
						return static_cast<char>(std::tolower(c));
					});

				if (lower == "1" || lower == "true" || lower == "on" || lower == "yes")
				{
					value = true;
					return true;
				}

				if (lower == "0" || lower == "false" || lower == "off" || lower == "no")
				{
					value = false;
					return true;
				}

				return false;
			}
			else if constexpr (std::is_floating_point_v<U>)
			{
				try
				{
					std::size_t used = 0;
					const double parsed = std::stod(text, &used);
					value = static_cast<U>(parsed);
					return used == text.size();
				}
				catch (...)
				{
					return false;
				}
			}
			else
			{
				static_assert(std::is_integral_v<U>, "RegisterTyped supports integers, floating point, bool and std::string");
				const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
				return result.ec == std::errc() && result.ptr == text.data() + text.size();
			}
		}

	private:

		static bool Tokenize(std::string_view command, std::vector<std::string>& tokens);

		void OnVariableChanged(const ConsoleVariable& variable);

		std::unordered_map<std::string, Callback> callbacks;
		std::unordered_map<std::string, CommandDesc> descriptions;
		std::unordered_map<std::string, std::unique_ptr<ConsoleVariable>> variables;
		std::unordered_map<std::string, std::string> pendingValues;
		std::vector<std::pair<std::string, ConfigWriter>> configWriters;
		std::vector<std::pair<ConsoleVariable::SubscriptionId, ChangeListener>> changeListeners;
		ConsoleVariable::SubscriptionId nextListener = 1;
		bool startupComplete = false;
		bool configDirty = false;

	};

	template <typename... Args, typename F> void CommandRegistry::RegisterTyped(std::string name, F callback)
	{
		const std::string usage = name;
		Register(std::move(name),
			[usage, callback = std::move(callback)](const std::vector<std::string>& arguments)
			{
				if (arguments.size() > sizeof...(Args))
				{
					throw std::invalid_argument(usage + ": takes " + std::to_string(sizeof...(Args)) + " argument(s)");
				}

				std::tuple<std::remove_cvref_t<Args>...> values{};
				std::size_t index = 0;
				std::size_t failed = 0;
				bool ok = true;
				std::apply(
					[&](auto&... value)
					{
						((ok && index < arguments.size() && !ParseArgument(arguments[index], value) ? (ok = false, failed = index + 1) : 0,
							 ++index),
							...);
					},
					values);

				if (!ok)
				{
					throw std::invalid_argument(usage + ": could not parse argument " + std::to_string(failed));
				}

				std::apply(callback, values);
			});
	}

} // namespace Swim::Commands
