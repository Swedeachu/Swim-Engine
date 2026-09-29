#pragma once

#include "Engine/Commands/CommandRegistry.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <vector>

namespace Engine
{

	// The runtime console's model: runs lines through the engine's CommandRegistry, keeps a
	// scrollback (the echoed command, whatever it printed to stdout/stderr, and errors) and a
	// command history navigated like a shell (Up: older, Down: newer, then back to what was
	// being typed). Built-in commands: help (every command), clear, echo. UI-free, so it is
	// testable headless; RuntimeConsoleOverlay draws it and feeds it keys.
	class RuntimeConsole
	{

	  public:

		explicit RuntimeConsole(Swim::Commands::CommandRegistry& registry, std::size_t maxLines = 400, std::size_t maxHistory = 64);

		~RuntimeConsole();

		RuntimeConsole(const RuntimeConsole&) = delete;

		RuntimeConsole& operator=(const RuntimeConsole&) = delete;

		bool IsOpen() const { return open; }

		void SetOpen(bool value);

		void Toggle() { SetOpen(!open); }

		// Echoes "> line", dispatches it, and appends its output. Blank lines do nothing.
		// Returns false for an unknown command, a parse error or a command that threw (the
		// scrollback says which).
		bool Execute(std::string_view line);

		void Print(std::string_view text); // Split into lines.

		void Clear();

		// History navigation. `current` is the text being edited (kept to return to).
		std::string HistoryUp(std::string_view current);

		std::string HistoryDown();

		const std::deque<std::string>& GetLines() const { return lines; }

		const std::vector<std::string>& GetHistory() const { return history; }

		// Bumped whenever the scrollback changes (the overlay re-renders its text then).
		std::uint64_t GetRevision() const { return revision; }

	  private:

		Swim::Commands::CommandRegistry& registry;
		std::size_t maxLines;
		std::size_t maxHistory;
		std::deque<std::string> lines;
		std::vector<std::string> history;
		std::size_t historyCursor = 0; // == history.size(): editing a new line.
		std::string draft;
		std::uint64_t revision = 0;
		bool open = false;

	};

} // namespace Engine
