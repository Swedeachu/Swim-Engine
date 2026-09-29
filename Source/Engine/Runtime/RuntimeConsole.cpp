#include "Engine/Runtime/RuntimeConsole.h"

#include <algorithm>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace Engine
{

	namespace
	{

		// Captures std::cout and std::cerr for the lifetime of the object.
		class CaptureOutput
		{

		  public:

			CaptureOutput() : previousOut(std::cout.rdbuf(buffer.rdbuf())), previousErr(std::cerr.rdbuf(buffer.rdbuf())) {}

			~CaptureOutput()
			{
				std::cout.rdbuf(previousOut);
				std::cerr.rdbuf(previousErr);
			}

			std::string Text() const { return buffer.str(); }

		  private:

			std::ostringstream buffer;
			std::streambuf* previousOut;
			std::streambuf* previousErr;

		};

	} // namespace

	RuntimeConsole::RuntimeConsole(Swim::Commands::CommandRegistry& registryValue, std::size_t maxLinesValue, std::size_t maxHistoryValue)
		: registry(registryValue), maxLines(std::max<std::size_t>(maxLinesValue, 1)), maxHistory(std::max<std::size_t>(maxHistoryValue, 1))
	{
		registry.Register("help",
			[this](const std::vector<std::string>&)
			{
				std::string text = "Commands:";

				for (const auto& name : registry.GetNames())
				{
					text += "\n  " + name;
				}

				Print(text);
			});
		registry.Register("clear",
			[this](const std::vector<std::string>&)
			{
				Clear();
			});
		registry.Register("echo",
			[this](const std::vector<std::string>& arguments)
			{
				std::string text;

				for (const auto& argument : arguments)
				{
					text += (text.empty() ? "" : " ") + argument;
				}

				Print(text);
			});
		Print("Swim Engine console. Type help for the commands; ` or Escape closes it.");
	}

	RuntimeConsole::~RuntimeConsole()
	{
		registry.Unregister("help");
		registry.Unregister("clear");
		registry.Unregister("echo");
	}

	void RuntimeConsole::SetOpen(bool value)
	{
		open = value;
		historyCursor = history.size();
		draft.clear();
	}

	void RuntimeConsole::Print(std::string_view text)
	{
		if (!text.empty() && text.back() == '\n')
		{
			text.remove_suffix(1);
		}

		std::size_t start = 0;

		while (true)
		{
			const std::size_t end = text.find('\n', start);
			lines.emplace_back(text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start));

			if (end == std::string_view::npos)
			{
				break;
			}

			start = end + 1;
		}

		while (lines.size() > maxLines)
		{
			lines.pop_front();
		}

		++revision;
	}

	void RuntimeConsole::Clear()
	{
		lines.clear();
		++revision;
	}

	bool RuntimeConsole::Execute(std::string_view input)
	{
		std::string line(input);

		while (!line.empty() && (line.back() == ' ' || line.back() == '\t' || line.back() == '\r' || line.back() == '\n'))
		{
			line.pop_back();
		}

		const auto first = line.find_first_not_of(" \t");

		if (first == std::string::npos)
		{
			return true;
		}

		line.erase(0, first);

		if (history.empty() || history.back() != line)
		{
			history.push_back(line);

			if (history.size() > maxHistory)
			{
				history.erase(history.begin());
			}
		}

		historyCursor = history.size();
		draft.clear();
		Print("> " + line);
		bool dispatched = false;
		std::string output;
		std::string error;
		{
			CaptureOutput capture;

			try
			{
				dispatched = registry.ParseAndDispatch(line);
			}
			catch (const std::exception& exception)
			{
				error = exception.what();
				dispatched = true;
			}

			output = capture.Text();
		}

		while (!output.empty() && output.back() == '\n')
		{
			output.pop_back();
		}

		if (!output.empty())
		{
			Print(output);
		}

		if (!error.empty())
		{
			Print("error: " + error);
			return false;
		}

		if (!dispatched)
		{
			const auto name = line.substr(0, line.find_first_of(" \t"));
			Print(registry.Contains(name) ? "error: could not parse: " + line : "unknown command: " + name + " (try help)");
			return false;
		}

		return true;
	}

	std::string RuntimeConsole::HistoryUp(std::string_view current)
	{
		if (history.empty())
		{
			return std::string(current);
		}

		if (historyCursor == history.size())
		{
			draft = std::string(current);
		}

		if (historyCursor > 0)
		{
			--historyCursor;
		}

		return history[historyCursor];
	}

	std::string RuntimeConsole::HistoryDown()
	{
		if (historyCursor >= history.size())
		{
			return draft;
		}

		++historyCursor;
		return historyCursor == history.size() ? draft : history[historyCursor];
	}

} // namespace Engine
