#include "CommandRegistry.h"

#include <cctype>
#include <stdexcept>
#include <utility>

namespace Swim::Commands
{

	void CommandRegistry::Register(std::string name, Callback callback)
	{
		if (name.empty() || !callback)
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
		callbacks.insert_or_assign(std::move(name), std::move(callback));
	}

	bool CommandRegistry::Unregister(std::string_view name)
	{
		return callbacks.erase(std::string(name)) != 0;
	}

	void CommandRegistry::Clear()
	{
		callbacks.clear();
	}

	bool CommandRegistry::Dispatch(std::string_view name, const std::vector<std::string>& args) const
	{
		const auto found = callbacks.find(std::string(name));
		if (found == callbacks.end())
		{
			return false;
		}
		// A callback may unregister/replace itself or clear the registry.
		const Callback callback = found->second;
		callback(args);
		return true;
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
