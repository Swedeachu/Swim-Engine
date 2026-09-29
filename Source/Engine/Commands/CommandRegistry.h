#pragma once

#include <algorithm>
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

	// In-process commands. This service owns no transport or per-frame lifecycle.
	class CommandRegistry
	{

	public:

		using Callback = std::function<void(const std::vector<std::string>&)>;

		void Register(std::string name, Callback callback);

		// A command with typed arguments: "name 1 2.5 on text" -> callback(int, float, bool,
		// std::string). Supported: integers, floating point, bool (1/0, true/false, on/off)
		// and std::string. Missing trailing arguments take their defaults (value-initialized);
		// extra or unparsable ones throw std::invalid_argument naming the command's usage.
		template <typename... Args, typename F> void RegisterTyped(std::string name, F callback);

		// Every registered name, sorted (the console's "help").
		std::vector<std::string> GetNames() const;

		bool Contains(std::string_view name) const { return callbacks.contains(std::string(name)); }

		bool Unregister(std::string_view name);

		void Clear();

		bool Dispatch(std::string_view name, const std::vector<std::string>& args) const;

		bool ParseAndDispatch(std::string_view command) const;

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

		std::unordered_map<std::string, Callback> callbacks;

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
