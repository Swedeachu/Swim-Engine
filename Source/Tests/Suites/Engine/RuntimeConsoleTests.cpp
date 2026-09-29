#include "Engine/Commands/CommandRegistry.h"
#include "Engine/Runtime/RuntimeConsole.h"
#include "Engine/Runtime/RuntimeConsoleOverlay.h"
#include "Engine/Runtime/UiRuntime.h"
#include "Engine/Systems/Camera/Camera.h"
#include "Tests/Framework/Test.h"

#include <filesystem>
#include <iostream>

namespace
{

	Engine::UiRuntime::ViewDesc View()
	{
		Engine::Camera camera;
		Engine::UiRuntime::ViewDesc view;
		view.Camera.View = camera.GetViewRowMajor();
		view.Camera.Projection = camera.GetProjectionRowMajor();
		view.Camera.ViewportWidth = 1280.0f;
		view.Camera.ViewportHeight = 720.0f;
		return view;
	}

} // namespace

SWIM_TEST("Engine.RuntimeConsole", "RunsCommandsCapturesOutputAndReportsErrors")
{
	Swim::Commands::CommandRegistry registry;
	int value = 0;
	registry.Register("set",
		[&](const std::vector<std::string>& arguments)
		{
			value = arguments.empty() ? 0 : std::stoi(arguments[0]);
			std::cout << "value is " << value << '\n';
		});
	registry.Register("boom",
		[](const std::vector<std::string>&)
		{
			throw std::runtime_error("broken");
		});
	Engine::RuntimeConsole console(registry, 8, 3);
	const auto lines = [&]
	{
		return std::vector<std::string>(console.GetLines().begin(), console.GetLines().end());
	};
	const auto last = [&]
	{
		return console.GetLines().back();
	};
	const auto start = console.GetLines().size(); // The greeting.
	SWIM_CHECK(console.Execute("set 7"));
	SWIM_CHECK_EQUAL(value, 7);
	auto shown = lines();
	SWIM_REQUIRE_EQUAL(shown.size(), start + 2);
	SWIM_CHECK_EQUAL(shown[start], std::string("> set 7"));
	SWIM_CHECK_EQUAL(shown[start + 1], std::string("value is 7")); // Captured stdout.
	// Unknown commands, parse errors and exceptions are reported, not fatal.
	SWIM_CHECK(!console.Execute("nope 1"));
	SWIM_CHECK(last().find("unknown command: nope") != std::string::npos);
	SWIM_CHECK(!console.Execute("boom"));
	SWIM_CHECK_EQUAL(last(), std::string("error: broken"));
	SWIM_CHECK(!console.Execute("set (unbalanced"));
	SWIM_CHECK(console.Execute("   ")); // Blank: nothing happens.
	// Built-ins.
	console.Execute("clear");
	SWIM_CHECK(console.GetLines().empty());
	console.Execute("echo hello   world");
	SWIM_CHECK_EQUAL(last(), std::string("hello world"));
	console.Execute("help");
	const auto all = lines();
	SWIM_CHECK(std::find(all.begin(), all.end(), std::string("  set")) != all.end());
	// The scrollback keeps the last maxLines.
	SWIM_CHECK(console.GetLines().size() <= 8u);
}

SWIM_TEST("Engine.RuntimeConsole", "HistoryWalksLikeAShell")
{
	Swim::Commands::CommandRegistry registry;
	registry.Register("a",
		[](const std::vector<std::string>&)
		{
		});
	Engine::RuntimeConsole console(registry, 50, 3);
	SWIM_CHECK_EQUAL(console.HistoryUp("typed"), std::string("typed")); // Empty history.
	console.Execute("a 1");
	console.Execute("a 2");
	console.Execute("a 2"); // Repeats are stored once.
	console.Execute("a 3");
	console.Execute("a 4"); // Oldest ("a 1") drops: three entries.
	SWIM_CHECK_EQUAL(console.GetHistory().size(), std::size_t{ 3 });
	SWIM_CHECK_EQUAL(console.HistoryUp("draft"), std::string("a 4"));
	SWIM_CHECK_EQUAL(console.HistoryUp("a 4"), std::string("a 3"));
	SWIM_CHECK_EQUAL(console.HistoryUp("a 3"), std::string("a 2"));
	SWIM_CHECK_EQUAL(console.HistoryUp("a 2"), std::string("a 2")); // Stops at the oldest.
	SWIM_CHECK_EQUAL(console.HistoryDown(), std::string("a 3"));
	SWIM_CHECK_EQUAL(console.HistoryDown(), std::string("a 4"));
	SWIM_CHECK_EQUAL(console.HistoryDown(), std::string("draft")); // Back to what was typed.
	SWIM_CHECK_EQUAL(console.HistoryDown(), std::string("draft"));
}

SWIM_TEST("Engine.RuntimeConsole", "TypedCommandsParseTheirArguments")
{
	Swim::Commands::CommandRegistry registry;
	float f = 0.0f;
	int i = 0;
	bool b = false;
	std::string s;
	registry.RegisterTyped<float, int, bool, std::string>("typed",
		[&](float fa, int ia, bool ba, std::string sa)
		{
			f = fa;
			i = ia;
			b = ba;
			s = sa;
		});
	SWIM_CHECK(registry.ParseAndDispatch("typed 2.5 -3 on \"two words\""));
	SWIM_CHECK_EQUAL(f, 2.5f);
	SWIM_CHECK_EQUAL(i, -3);
	SWIM_CHECK(b);
	SWIM_CHECK_EQUAL(s, std::string("two words"));
	// Missing trailing arguments default; bad or extra ones throw (the console shows it).
	SWIM_CHECK(registry.ParseAndDispatch("typed 1"));
	SWIM_CHECK_EQUAL(i, 0);
	SWIM_CHECK(!b);
	SWIM_CHECK_THROWS(registry.ParseAndDispatch("typed x"), std::invalid_argument);
	SWIM_CHECK_THROWS(registry.ParseAndDispatch("typed 1 2 off s extra"), std::invalid_argument);
	Engine::RuntimeConsole console(registry);
	SWIM_CHECK(!console.Execute("typed 1 z"));
	SWIM_CHECK(std::string(console.GetLines().back()).find("could not parse argument 2") != std::string::npos);
	const auto names = registry.GetNames();
	SWIM_CHECK(std::is_sorted(names.begin(), names.end()));
}

SWIM_TEST("Engine.RuntimeConsole", "TheOverlayOpensFocusesRunsAndCloses")
{
	Swim::Commands::CommandRegistry registry;
	int runs = 0;
	registry.Register("ping",
		[&](const std::vector<std::string>&)
		{
			++runs;
		});
	Engine::UiRuntime ui{ std::filesystem::path(SWIM_TEST_ASSET_ROOT) };
	Engine::RuntimeConsole console(registry);
	Engine::RuntimeConsoleOverlay overlay(ui, console);
	ui.Sync(nullptr, View());
	// Closed: not drawn, not focused.
	SWIM_CHECK(ui.Finish(0.016f).empty());
	SWIM_CHECK(!overlay.IsOpen());
	overlay.SetOpen(true);
	ui.Sync(nullptr, View());
	const auto drawn = ui.Finish(0.016f);
	SWIM_REQUIRE_EQUAL(drawn.size(), std::size_t{ 1 });
	SWIM_CHECK(drawn[0].Document == overlay.GetDocument().get());
	auto& document = *overlay.GetDocument();
	SWIM_CHECK(document.GetFocus() == overlay.GetInputField());
	SWIM_CHECK(document.WantsTextInput()); // Keyboard captured: gameplay input stands back.
	// Typing and Enter run the line and clear it; the console key never stays in the line.
	document.TextInput("ping`");
	overlay.AfterInput();
	SWIM_CHECK_EQUAL(overlay.GetInputText(), std::string("ping"));
	document.KeyDown(Swim::UI::UiKey::Enter);
	overlay.AfterInput();
	SWIM_CHECK_EQUAL(runs, 1);
	SWIM_CHECK(overlay.GetInputText().empty());
	SWIM_CHECK_EQUAL(console.GetHistory().back(), std::string("ping"));
	// Closing hides it and drops focus.
	overlay.SetOpen(false);
	ui.Sync(nullptr, View());
	SWIM_CHECK(ui.Finish(0.016f).empty());
	SWIM_CHECK(!document.WantsTextInput());
}
