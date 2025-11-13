#include "PCH.h"
#include "CommandSystem.h"

#include <cctype>
#include <cstdlib>
#include <cerrno>
#include <limits>
#include <sstream>

namespace Engine
{

	int CommandSystem::Awake()
	{
		return 0;
	}

	int CommandSystem::Init()
	{
		return 0;
	}

	int CommandSystem::Exit()
	{
		commandRegistry.clear();
		return 0;
	}

	void CommandSystem::RegisterRaw(const std::string& commandName, std::function<void(const std::vector<std::string>&)> fn)
	{
		auto c = std::make_unique<RawCmd>();
		c->fn = std::move(fn);
		RegisterImpl(commandName, std::move(c));
	}

	void CommandSystem::RegisterImpl(const std::string& commandName, std::unique_ptr<ICmd> cmd)
	{
		commandRegistry[commandName] = std::move(cmd);
	}

	bool CommandSystem::ParseAndDispatch(const std::string& message)
	{
		std::vector<std::string> tokens;
		if (!SplitTokens(message, tokens) || tokens.empty())
		{
			return false;
		}

		const std::string name = tokens[0]; /// copy
		tokens.erase(tokens.begin()); // args only

		return Dispatch(name, tokens);
	}

	bool CommandSystem::Dispatch(const std::string& commandName, const std::vector<std::string>& args)
	{
		auto it = commandRegistry.find(commandName);
		if (it == commandRegistry.end())
		{
			return false;
		}

		return it->second->Call(args);
	}

	// Tokenization supports quotes and simple escapes
	bool CommandSystem::SplitTokens(const std::string& line, std::vector<std::string>& outTokens)
	{
		outTokens.clear();
		const char* s = line.c_str();
		size_t n = line.size();

		size_t i = 0;
		while (i < n)
		{
			// skip spaces
			while (i < n && std::isspace(static_cast<unsigned char>(s[i])))
			{
				++i;
			}

			if (i >= n)
			{
				break;
			}

			std::string tok;
			if (s[i] == '"')
			{
				++i;
				while (i < n && s[i] != '"')
				{
					if (s[i] == '\\' && i + 1 < n)
					{
						char next = s[i + 1];
						// handle simple escapes for quote and backslash
						if (next == '"' || next == '\\')
						{
							tok.push_back(next); i += 2;
							continue;
						}
					}

					tok.push_back(s[i++]);
				}
				// consume closing quote
				if (i < n && s[i] == '"')
				{
					++i;
				}
			}
			else
			{
				while (i < n && !std::isspace(static_cast<unsigned char>(s[i])))
				{
					tok.push_back(s[i++]);
				}
			}

			if (!tok.empty())
			{
				outTokens.push_back(std::move(tok));
			}
		}

		return true;
	}

	static bool ToInt(const std::string& s, long long& out)
	{
		if (s.empty())
		{
			return false;
		}

		char* end = nullptr;
		errno = 0;
		long long v = std::strtoll(s.c_str(), &end, 0); // base 0: handles 0x, 0, etc.

		if (errno != 0 || end == s.c_str() || *end != '\0')
		{
			return false;
		}

		out = v;

		return true;
	}

	static bool ToUInt(const std::string& s, unsigned long long& out)
	{
		if (s.empty())
		{
			return false;
		}

		char* end = nullptr;
		errno = 0;
		unsigned long long v = std::strtoull(s.c_str(), &end, 0);

		if (errno != 0 || end == s.c_str() || *end != '\0')
		{
			return false;
		}

		out = v;

		return true;
	}

	static bool ToFloatLike(const std::string& s, double& out)
	{
		if (s.empty())
		{
			return false;
		}

		char* end = nullptr;
		errno = 0;
		double v = std::strtod(s.c_str(), &end);

		if (errno != 0 || end == s.c_str() || *end != '\0')
		{
			return false;
		}

		out = v;

		return true;
	}

	bool CommandSystem::ConvertArg(const std::string& s, std::string& out)
	{
		out = s;
		return true;
	}

	bool CommandSystem::ConvertArg(const std::string& s, bool& out)
	{
		std::string lower;
		lower.reserve(s.size());

		for (char c : s)
		{
			lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
		}

		if (lower == "1" || lower == "true" || lower == "yes" || lower == "on")
		{
			out = true;
			return true;
		}

		if (lower == "0" || lower == "false" || lower == "no" || lower == "off")
		{
			out = false;
			return true;
		}

		long long v{};

		if (ToInt(s, v))
		{
			out = (v != 0);
			return true;
		}

		return false;
	}

	bool CommandSystem::ConvertArg(const std::string& s, int& out)
	{
		long long v{};

		if (!ToInt(s, v))
		{
			return false;
		}

		if (v < std::numeric_limits<int>::min() || v > std::numeric_limits<int>::max())
		{
			return false;
		}

		out = static_cast<int>(v);

		return true;
	}

	bool CommandSystem::ConvertArg(const std::string& s, unsigned& out)
	{
		unsigned long long v{};

		if (!ToUInt(s, v))
		{
			return false;
		}

		if (v > std::numeric_limits<unsigned>::max())
		{
			return false;
		}

		out = static_cast<unsigned>(v);

		return true;
	}

	bool CommandSystem::ConvertArg(const std::string& s, long& out)
	{
		long long v{};

		if (!ToInt(s, v))
		{
			return false;
		}

		if (v < std::numeric_limits<long>::min() || v > std::numeric_limits<long>::max())
		{
			return false;
		}

		out = static_cast<long>(v);

		return true;
	}

	bool CommandSystem::ConvertArg(const std::string& s, unsigned long& out)
	{
		unsigned long long v{};

		if (!ToUInt(s, v))
		{
			return false;
		}

		if (v > std::numeric_limits<unsigned long>::max())
		{
			return false;
		}

		out = static_cast<unsigned long>(v);

		return true;
	}

	bool CommandSystem::ConvertArg(const std::string& s, long long& out)
	{
		return ToInt(s, out);
	}

	bool CommandSystem::ConvertArg(const std::string& s, unsigned long long& out)
	{
		return ToUInt(s, out);
	}

	bool CommandSystem::ConvertArg(const std::string& s, float& out)
	{
		double d{};

		if (!ToFloatLike(s, d))
		{
			return false;
		}

		if (d < -std::numeric_limits<float>::max() || d > std::numeric_limits<float>::max())
		{
			return false;
		}

		out = static_cast<float>(d);

		return true;
	}

	bool CommandSystem::ConvertArg(const std::string& s, double& out)
	{
		double d{};

		if (!ToFloatLike(s, d))
		{
			return false;
		}

		out = d;

		return true;
	}

} // namespace Engine
