#pragma once

#include <functional>
#include <string>
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
		bool Unregister(std::string_view name);
		void Clear();
		bool Dispatch(std::string_view name, const std::vector<std::string>& args) const;
		bool ParseAndDispatch(std::string_view command) const;

	private:

		static bool Tokenize(std::string_view command, std::vector<std::string>& tokens);

		std::unordered_map<std::string, Callback> callbacks;
	};

} // namespace Swim::Commands
