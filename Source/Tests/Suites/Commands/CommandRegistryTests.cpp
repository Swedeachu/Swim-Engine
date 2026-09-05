#include "Engine/Commands/CommandRegistry.h"
#include "Tests/Framework/Test.h"

#include <stdexcept>

SWIM_TEST("Commands.Registry", "PlainAndWrappedCommandsPreserveQuotedArguments")
{
	Swim::Commands::CommandRegistry registry;
	std::vector<std::string> received;
	registry.Register("run", [&](const std::vector<std::string>& args) { received = args; });
	SWIM_CHECK(registry.ParseAndDispatch("run one two"));
	SWIM_CHECK((received == std::vector<std::string>{ "one", "two" }));

	// Bind the raw string to a name before handing it to ParseAndDispatch. A
	// raw string containing literal '(' / ')' characters, passed directly as
	// a macro argument, can make MSVC's legacy (non-conforming) preprocessor
	// mis-scan the SWIM_CHECK(...) argument boundary and split the token
	// stream mid-literal -- it never reaches the compiler proper as one
	// string. Passing a plain identifier sidesteps that entirely.
	const std::string_view quotedArgsCommand =
		R"cmd( (run "" "Enemy (Grunt)" "a\"b" "C:\models\mesh") )cmd";
	SWIM_CHECK(registry.ParseAndDispatch(quotedArgsCommand));
	SWIM_CHECK((received == std::vector<std::string>{ "", "Enemy (Grunt)", "a\"b", "C:\\models\\mesh" }));
	SWIM_CHECK(registry.ParseAndDispatch("(run)"));
	SWIM_CHECK(received.empty());
}

SWIM_TEST("Commands.Registry", "MalformedCommandsNeverInvokeHandlers")
{
	Swim::Commands::CommandRegistry registry;
	int calls = 0;
	registry.Register("run", [&](const auto&) { ++calls; });
	for (const auto* invalid : { "", "  ", "()", "(run", "run)", "(run))", "run (arg)", "run \"unterminated", "unknown" })
	{
		SWIM_CHECK(!registry.ParseAndDispatch(invalid));
	}
	SWIM_CHECK(!registry.ParseAndDispatch(std::string_view("run\0x", 5)));
	SWIM_CHECK_EQUAL(calls, 0);
}

SWIM_TEST("Commands.Registry", "ReplacementAndSelfRemovalKeepExecutingCallbackAlive")
{
	Swim::Commands::CommandRegistry registry;
	int result = 0;
	registry.Register("run", [&](const auto&) { result = 1; });
	registry.Register("run", [&](const auto&)
	{
		registry.Clear();
		result = 2;
	});
	SWIM_CHECK(registry.Dispatch("run", {}));
	SWIM_CHECK_EQUAL(result, 2);
	SWIM_CHECK(!registry.Dispatch("run", {}));
	registry.Register("run", [&](const auto&) { result = 3; });
	SWIM_CHECK(registry.Unregister("run"));
	SWIM_CHECK(!registry.Unregister("run"));
	SWIM_CHECK(!registry.Dispatch("run", {}));
}

SWIM_TEST("Commands.Registry", "InvalidRegistrationAndHandlerFailuresRemainExplicit")
{
	Swim::Commands::CommandRegistry registry;
	SWIM_CHECK_THROWS(registry.Register("", [](const auto&) {}), std::invalid_argument);
	SWIM_CHECK_THROWS(registry.Register("two words", [](const auto&) {}), std::invalid_argument);
	SWIM_CHECK_THROWS(registry.Register("run", {}), std::invalid_argument);
	registry.Register("run", [](const auto&) { throw std::runtime_error("handler failed"); });
	SWIM_CHECK_THROWS(registry.Dispatch("run", {}), std::runtime_error);
}
