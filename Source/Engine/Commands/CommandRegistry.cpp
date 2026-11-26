#include "CommandRegistry.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace Swim::Commands
{

	namespace
	{

		void ValidateName(const std::string& name)
		{
			if (name.empty())
			{
				throw std::invalid_argument("Commands require a name and callable handler");
			}

			for (unsigned char character : name)
			{
				if (std::isspace(character) || character == '(' || character == ')' || character == '"' || character == 0)
				{
					throw std::invalid_argument("Command names must be a single unquoted token");
				}
			}
		}

		std::string JoinArguments(const std::vector<std::string>& args)
		{
			std::string text;

			for (const auto& arg : args)
			{
				text += (text.empty() ? "" : " ") + arg;
			}

			return text;
		}

	} // namespace

	void CommandRegistry::Register(std::string name, Callback callback)
	{
		CommandDesc desc;
		desc.Name = std::move(name);
		Register(std::move(desc), std::move(callback));
	}

	void CommandRegistry::Register(CommandDesc desc, Callback callback)
	{
		if (!callback)
		{
			throw std::invalid_argument("Commands require a name and callable handler");
		}

		ValidateName(desc.Name);

		if (variables.contains(desc.Name))
		{
			throw std::invalid_argument("'" + desc.Name + "' is already a console variable");
		}

		std::string name = desc.Name;
		callbacks.insert_or_assign(name, std::move(callback));
		descriptions.insert_or_assign(std::move(name), std::move(desc));
	}

	ConsoleVariable& CommandRegistry::RegisterVariable(VariableDesc desc)
	{
		ValidateName(desc.Name);

		if (callbacks.contains(desc.Name))
		{
			throw std::invalid_argument("'" + desc.Name + "' is already a command");
		}

		if (const auto found = variables.find(desc.Name); found != variables.end())
		{
			return *found->second;
		}

		const std::string name = desc.Name;
		auto variable = std::make_unique<ConsoleVariable>(std::move(desc));
		ConsoleVariable& result = *variable;
		variables.emplace(name, std::move(variable));

		if (const auto pending = pendingValues.find(name); pending != pendingValues.end())
		{
			std::vector<std::string> tokens;

			if (!Tokenize(pending->second, tokens) || !result.SetFromText(JoinArguments(tokens), true))
			{
				std::cerr << "[Config] " << name << ": invalid value '" << pending->second << "'\n";
			}

			pendingValues.erase(pending);
		}

		// Hooked after the pending value: loading a saved value does not make the config dirty.
		result.latchRestart = startupComplete;
		result.registryHook = [this](const ConsoleVariable& changed)
		{
			OnVariableChanged(changed);
		};
		return result;
	}

	ConsoleVariable& CommandRegistry::AddBool(std::string name, bool defaultValue, std::string description, std::string category,
		VariableFlags flags)
	{
		VariableDesc desc;
		desc.Name = std::move(name);
		desc.Description = std::move(description);
		desc.Category = std::move(category);
		desc.Type = VariableType::Bool;
		desc.Flags = flags;
		desc.DefaultNumber = defaultValue ? 1.0 : 0.0;
		return RegisterVariable(std::move(desc));
	}

	ConsoleVariable& CommandRegistry::AddInt(std::string name, int64_t defaultValue, int64_t min, int64_t max, std::string description,
		std::string category, VariableFlags flags)
	{
		VariableDesc desc;
		desc.Name = std::move(name);
		desc.Description = std::move(description);
		desc.Category = std::move(category);
		desc.Type = VariableType::Int;
		desc.Flags = flags;
		desc.DefaultNumber = static_cast<double>(defaultValue);
		desc.Min = static_cast<double>(min);
		desc.Max = static_cast<double>(max);
		desc.Step = 1.0;
		return RegisterVariable(std::move(desc));
	}

	ConsoleVariable& CommandRegistry::AddFloat(std::string name, double defaultValue, double min, double max, std::string description,
		std::string category, VariableFlags flags)
	{
		VariableDesc desc;
		desc.Name = std::move(name);
		desc.Description = std::move(description);
		desc.Category = std::move(category);
		desc.Type = VariableType::Float;
		desc.Flags = flags;
		desc.DefaultNumber = defaultValue;
		desc.Min = min;
		desc.Max = max;
		return RegisterVariable(std::move(desc));
	}

	ConsoleVariable& CommandRegistry::AddEnum(std::string name, std::vector<std::string> choices, uint32_t defaultIndex,
		std::string description, std::string category, VariableFlags flags)
	{
		VariableDesc desc;
		desc.Name = std::move(name);
		desc.Description = std::move(description);
		desc.Category = std::move(category);
		desc.Type = VariableType::Enum;
		desc.Flags = flags;
		desc.DefaultNumber = static_cast<double>(defaultIndex);
		desc.Choices = std::move(choices);
		return RegisterVariable(std::move(desc));
	}

	ConsoleVariable& CommandRegistry::AddString(std::string name, std::string defaultValue, std::string description, std::string category,
		VariableFlags flags)
	{
		VariableDesc desc;
		desc.Name = std::move(name);
		desc.Description = std::move(description);
		desc.Category = std::move(category);
		desc.Type = VariableType::String;
		desc.Flags = flags;
		desc.DefaultText = std::move(defaultValue);
		return RegisterVariable(std::move(desc));
	}

	ConsoleVariable& CommandRegistry::AddColor(std::string name, VariableColor defaultValue, std::string description, std::string category,
		VariableFlags flags)
	{
		VariableDesc desc;
		desc.Name = std::move(name);
		desc.Description = std::move(description);
		desc.Category = std::move(category);
		desc.Type = VariableType::Color;
		desc.Flags = flags;
		desc.DefaultColor = defaultValue;
		return RegisterVariable(std::move(desc));
	}

	ConsoleVariable* CommandRegistry::FindVariable(std::string_view name)
	{
		const auto found = variables.find(std::string(name));
		return found == variables.end() ? nullptr : found->second.get();
	}

	const ConsoleVariable* CommandRegistry::FindVariable(std::string_view name) const
	{
		const auto found = variables.find(std::string(name));
		return found == variables.end() ? nullptr : found->second.get();
	}

	std::vector<ConsoleVariable*> CommandRegistry::GetVariables() const
	{
		std::vector<ConsoleVariable*> result;
		result.reserve(variables.size());

		for (const auto& [name, variable] : variables)
		{
			(void)name;
			result.push_back(variable.get());
		}

		std::sort(result.begin(), result.end(),
			[](const ConsoleVariable* a, const ConsoleVariable* b)
			{
				return a->GetName() < b->GetName();
			});
		return result;
	}

	std::vector<CommandInfo> CommandRegistry::GetCommandInfos() const
	{
		std::vector<CommandInfo> result;
		result.reserve(callbacks.size() + variables.size());

		for (const auto& [name, callback] : callbacks)
		{
			(void)callback;
			const auto desc = descriptions.find(name);

			if (desc != descriptions.end())
			{
				result.push_back({ name, desc->second.Description, desc->second.Category, desc->second.Usage, nullptr });
			}
			else
			{
				result.push_back({ name, {}, {}, {}, nullptr });
			}
		}

		for (const auto& [name, variable] : variables)
		{
			const auto& desc = variable->GetDesc();
			result.push_back({ name, desc.Description, desc.Category, "[value]", variable.get() });
		}

		std::sort(result.begin(), result.end(),
			[](const CommandInfo& a, const CommandInfo& b)
			{
				return a.Name < b.Name;
			});
		return result;
	}

	std::vector<std::string> CommandRegistry::GetNames() const
	{
		std::vector<std::string> names;
		names.reserve(callbacks.size() + variables.size());

		for (const auto& [name, callback] : callbacks)
		{
			(void)callback;
			names.push_back(name);
		}

		for (const auto& [name, variable] : variables)
		{
			(void)variable;
			names.push_back(name);
		}

		std::sort(names.begin(), names.end());
		return names;
	}

	bool CommandRegistry::Contains(std::string_view name) const
	{
		const std::string key(name);
		return callbacks.contains(key) || variables.contains(key);
	}

	bool CommandRegistry::Unregister(std::string_view name)
	{
		const std::string key(name);
		descriptions.erase(key);
		const bool removedCommand = callbacks.erase(key) != 0;
		const bool removedVariable = variables.erase(key) != 0;
		return removedCommand || removedVariable;
	}

	void CommandRegistry::Clear()
	{
		callbacks.clear();
		descriptions.clear();
		variables.clear();
	}

	bool CommandRegistry::Dispatch(std::string_view name, const std::vector<std::string>& args) const
	{
		const std::string key(name);

		if (const auto variable = variables.find(key); variable != variables.end())
		{
			ConsoleVariable& target = *variable->second;
			const auto& desc = target.GetDesc();

			if (args.empty())
			{
				std::cout << key << " = " << QuoteArgument(target.ToString()) << " (default " << QuoteArgument(target.DefaultToString())
						  << (target.IsRestartPending() ? ", applies after restart" : "") << ")";

				if (!desc.Description.empty())
				{
					std::cout << " - " << desc.Description;
				}

				if (target.GetType() == VariableType::Enum)
				{
					std::cout << " [";

					for (std::size_t i = 0; i < desc.Choices.size(); ++i)
					{
						std::cout << (i ? "|" : "") << desc.Choices[i];
					}

					std::cout << "]";
				}

				std::cout << '\n';
				return true;
			}

			if (HasFlag(desc.Flags, VariableFlags::ReadOnly))
			{
				throw std::invalid_argument(key + " is read-only");
			}

			const std::string text = JoinArguments(args);

			if (!target.SetFromText(text))
			{
				throw std::invalid_argument(key + ": invalid value '" + text + "'");
			}

			return true;
		}

		const auto found = callbacks.find(key);

		if (found == callbacks.end())
		{
			return false;
		}

		// A callback may unregister/replace itself or clear the registry.
		const Callback callback = found->second;
		callback(args);
		return true;
	}

	void CommandRegistry::ExecuteConfigLine(std::string_view line)
	{
		while (!line.empty() && std::isspace(static_cast<unsigned char>(line.front())))
		{
			line.remove_prefix(1);
		}

		if (line.empty() || line.front() == '#' || line.rfind("//", 0) == 0)
		{
			return;
		}

		std::vector<std::string> tokens;

		if (!Tokenize(line, tokens) || tokens.empty())
		{
			std::cerr << "[Config] Malformed line: " << line << '\n';
			return;
		}

		const std::string name = tokens.front();
		tokens.erase(tokens.begin());

		if (const auto found = sessionOverrides.find(name); found != sessionOverrides.end() && !tokens.empty())
		{
			std::string value;

			for (const auto& token : tokens)
			{
				value += (value.empty() ? "" : " ") + QuoteArgument(token);
			}

			found->second.Saved = std::move(value);
			return;
		}

		try
		{
			if (!Dispatch(name, tokens) && !tokens.empty())
			{
				std::string value;

				for (const auto& token : tokens)
				{
					value += (value.empty() ? "" : " ") + QuoteArgument(token);
				}

				pendingValues.insert_or_assign(name, std::move(value));
			}
		}
		catch (const std::exception& error)
		{
			std::cerr << "[Config] " << error.what() << '\n';
		}
	}

	bool CommandRegistry::OverrideForSession(std::string_view name, std::string_view text)
	{
		ConsoleVariable* variable = FindVariable(name);

		if (!variable)
		{
			return false;
		}

		SessionOverride entry;

		if (!variable->IsDefault())
		{
			entry.Saved = QuoteArgument(variable->ToString());
		}

		sessionOverrides.erase(std::string(name));

		if (!variable->SetFromText(text, true))
		{
			return false;
		}

		entry.Value = variable->ToString();
		sessionOverrides.insert_or_assign(std::string(name), std::move(entry));
		return true;
	}

	bool CommandRegistry::IsSessionOverride(std::string_view name) const
	{
		return sessionOverrides.contains(std::string(name));
	}

	bool CommandRegistry::ExecuteFile(const std::filesystem::path& path)
	{
		std::ifstream file(path, std::ios::binary);

		if (!file)
		{
			return false;
		}

		std::string line;

		while (std::getline(file, line))
		{
			if (!line.empty() && line.back() == '\r')
			{
				line.pop_back();
			}

			ExecuteConfigLine(line);
		}

		return true;
	}

	bool CommandRegistry::SaveConfig(const std::filesystem::path& path, VariableFlags flags) const
	{
		std::string out;

		for (const ConsoleVariable* variable : GetVariables())
		{
			if (!HasFlag(variable->GetDesc().Flags, flags))
			{
				continue;
			}

			if (const auto found = sessionOverrides.find(variable->GetName()); found != sessionOverrides.end())
			{
				if (found->second.Saved)
				{
					out += variable->GetName() + " " + *found->second.Saved + "\n";
				}
			}
			else if (!variable->IsDefault())
			{
				out += variable->GetName() + " " + QuoteArgument(variable->ToString()) + "\n";
			}
		}

		std::vector<std::pair<std::string, std::string>> pending(pendingValues.begin(), pendingValues.end());
		std::sort(pending.begin(), pending.end());

		for (const auto& [name, value] : pending)
		{
			out += name + " " + value + "\n";
		}

		for (const auto& [key, writer] : configWriters)
		{
			(void)key;
			writer(out);
		}

		std::error_code error;

		if (path.has_parent_path())
		{
			std::filesystem::create_directories(path.parent_path(), error);
		}

		std::filesystem::path temporary = path;
		temporary += ".tmp";

		{
			std::ofstream file(temporary, std::ios::binary | std::ios::trunc);

			if (!file || !file.write(out.data(), static_cast<std::streamsize>(out.size())))
			{
				return false;
			}
		}

		std::filesystem::rename(temporary, path, error);

		if (error)
		{
			std::filesystem::remove(temporary, error);
			return false;
		}

		return true;
	}

	void CommandRegistry::SetConfigWriter(std::string key, ConfigWriter writer)
	{
		RemoveConfigWriter(key);

		if (writer)
		{
			configWriters.emplace_back(std::move(key), std::move(writer));
		}
	}

	void CommandRegistry::RemoveConfigWriter(std::string_view key)
	{
		std::erase_if(configWriters,
			[key](const auto& entry)
			{
				return entry.first == key;
			});
	}

	void CommandRegistry::SetStartupComplete()
	{
		startupComplete = true;

		for (auto& [name, variable] : variables)
		{
			(void)name;
			variable->latchRestart = true;
		}
	}

	ConsoleVariable::SubscriptionId CommandRegistry::SubscribeVariableChanges(ChangeListener listener)
	{
		const auto id = nextListener++;
		changeListeners.emplace_back(id, std::move(listener));
		return id;
	}

	void CommandRegistry::UnsubscribeVariableChanges(ConsoleVariable::SubscriptionId id)
	{
		std::erase_if(changeListeners,
			[id](const auto& entry)
			{
				return entry.first == id;
			});
	}

	void CommandRegistry::OnVariableChanged(const ConsoleVariable& variable)
	{
		if (const auto found = sessionOverrides.find(variable.GetName());
			found != sessionOverrides.end() && variable.ToString() != found->second.Value)
		{
			sessionOverrides.erase(found);
		}

		if (HasFlag(variable.GetDesc().Flags, VariableFlags::Saved))
		{
			configDirty = true;
		}

		const auto listeners = changeListeners;

		for (const auto& [id, listener] : listeners)
		{
			(void)id;
			listener(variable);
		}
	}

	std::string CommandRegistry::QuoteArgument(std::string_view text)
	{
		bool needsQuotes = text.empty();

		for (const char character : text)
		{
			if (std::isspace(static_cast<unsigned char>(character)) || character == '"' || character == '\\' || character == '(' ||
				character == ')')
			{
				needsQuotes = true;
				break;
			}
		}

		if (!needsQuotes)
		{
			return std::string(text);
		}

		std::string quoted = "\"";

		for (const char character : text)
		{
			if (character == '"' || character == '\\')
			{
				quoted.push_back('\\');
			}

			quoted.push_back(character);
		}

		quoted.push_back('"');
		return quoted;
	}

	bool CommandRegistry::ParseAndDispatch(std::string_view command) const
	{
		std::vector<std::string> tokens;

		if (!Tokenize(command, tokens) || tokens.empty())
		{
			return false;
		}

		std::string name = std::move(tokens.front());
		tokens.erase(tokens.begin());
		return Dispatch(name, tokens);
	}

	bool CommandRegistry::Tokenize(std::string_view command, std::vector<std::string>& tokens)
	{
		while (!command.empty() && std::isspace(static_cast<unsigned char>(command.front())))
		{
			command.remove_prefix(1);
		}

		while (!command.empty() && std::isspace(static_cast<unsigned char>(command.back())))
		{
			command.remove_suffix(1);
		}

		if (!command.empty() && command.front() == '(')
		{
			if (command.size() < 2 || command.back() != ')')
			{
				return false;
			}

			command.remove_prefix(1);
			command.remove_suffix(1);
		}

		std::string token;
		bool quoted = false;
		bool started = false;

		for (std::size_t index = 0; index < command.size(); ++index)
		{
			const char character = command[index];

			if (character == '\0')
			{
				return false;
			}

			if (quoted && character == '\\' && index + 1 < command.size() &&
				(command[index + 1] == '"' || command[index + 1] == '\\'))
			{
				token.push_back(command[++index]);
				continue;
			}

			if (character == '"')
			{
				quoted = !quoted;
				started = true;
				continue;
			}

			if (!quoted && (character == '(' || character == ')'))
			{
				return false;
			}

			if (!quoted && std::isspace(static_cast<unsigned char>(character)))
			{
				if (started)
				{
					tokens.push_back(std::move(token));
					token.clear();
					started = false;
				}

				continue;
			}

			token.push_back(character);
			started = true;
		}

		if (quoted)
		{
			return false;
		}

		if (started)
		{
			tokens.push_back(std::move(token));
		}

		return true;
	}

} // namespace Swim::Commands
