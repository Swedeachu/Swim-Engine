#include "Tests/Framework/TestRunner.h"

#include "Tests/Framework/Test.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <random>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace Swim::Testing
{

	namespace
	{

		char LowerAscii(char value)
		{
			return static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
		}

		bool EqualsIgnoreCase(std::string_view left, std::string_view right)
		{
			return left.size() == right.size()
				&& std::equal(left.begin(), left.end(), right.begin(), [](char a, char b)
				{
					return LowerAscii(a) == LowerAscii(b);
				});
		}

		bool StartsWith(std::string_view value, std::string_view prefix)
		{
			return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
		}

		bool TryParseInt(std::string_view text, int& value)
		{
			const char* begin = text.data();
			const char* end = begin + text.size();
			const std::from_chars_result result = std::from_chars(begin, end, value);
			return result.ec == std::errc{} && result.ptr == end;
		}

		bool TryParseSeed(std::string_view text, std::uint32_t& value)
		{
			const char* begin = text.data();
			const char* end = begin + text.size();
			const std::from_chars_result result = std::from_chars(begin, end, value);
			return result.ec == std::errc{} && result.ptr == end;
		}

		std::string EscapeXml(std::string_view text)
		{
			std::string escaped;
			escaped.reserve(text.size());
			for (const char character : text)
			{
				switch (character)
				{
				case '&': escaped += "&amp;"; break;
				case '<': escaped += "&lt;"; break;
				case '>': escaped += "&gt;"; break;
				case '"': escaped += "&quot;"; break;
				case '\'': escaped += "&apos;"; break;
				default: escaped += character; break;
				}
			}
			return escaped;
		}

		struct TestResult
		{
			const TestCase* Test = nullptr;
			std::vector<TestFailure> Failures;
			std::size_t CheckCount = 0;
			double DurationMilliseconds = 0.0;

			bool Passed() const
			{
				return Failures.empty();
			}
		};

		std::string FormatFailure(const TestFailure& failure)
		{
			std::string text = std::string(failure.Location.File) + "(" + std::to_string(failure.Location.Line) + "): ";
			text += failure.Expression;
			if (!failure.Message.empty())
			{
				text += " -- " + failure.Message;
			}
			return text;
		}

		TestResult RunSingleTest(const TestCase& test)
		{
			TestResult result;
			result.Test = &test;

			TestContext context;
			Detail::SetCurrentContext(&context);

			const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
			try
			{
				test.Function();
			}
			catch (const TestAbort&)
			{
				// The failing requirement is already recorded in the context.
			}
			catch (const std::exception& exception)
			{
				context.RecordFailure(test.Location, "unhandled std::exception", exception.what());
			}
			catch (...)
			{
				context.RecordFailure(test.Location, "unhandled non-standard exception", std::string{});
			}
			const std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();

			Detail::SetCurrentContext(nullptr);

			result.DurationMilliseconds = std::chrono::duration<double, std::milli>(end - start).count();
			result.Failures = context.GetFailures();
			result.CheckCount = context.GetCheckCount();
			return result;
		}

		bool WriteJUnitReport(
			const std::string& path,
			const std::vector<TestResult>& results,
			double totalMilliseconds)
		{
			std::ofstream report(path, std::ios::trunc);
			if (!report)
			{
				return false;
			}

			std::size_t failureCount = 0;
			for (const TestResult& result : results)
			{
				if (!result.Passed())
				{
					++failureCount;
				}
			}

			report << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
			report << "<testsuites name=\"SwimTests\" tests=\"" << results.size()
				<< "\" failures=\"" << failureCount
				<< "\" time=\"" << (totalMilliseconds / 1000.0) << "\">\n";

			for (const TestResult& result : results)
			{
				report << "\t<testcase classname=\"" << EscapeXml(result.Test->Suite)
					<< "\" name=\"" << EscapeXml(result.Test->Name)
					<< "\" time=\"" << (result.DurationMilliseconds / 1000.0) << "\"";

				if (result.Passed())
				{
					report << " />\n";
					continue;
				}

				report << ">\n";
				for (const TestFailure& failure : result.Failures)
				{
					report << "\t\t<failure message=\"" << EscapeXml(FormatFailure(failure)) << "\" />\n";
				}
				report << "\t</testcase>\n";
			}

			report << "</testsuites>\n";
			return static_cast<bool>(report);
		}

	} // namespace

	bool MatchesPattern(std::string_view pattern, std::string_view identifier)
	{
		if (pattern.empty())
		{
			return true;
		}

		// A wildcard-free pattern is also treated as a dotted-prefix selector so
		// "--filter=AssetCompiler" means "every case in every AssetCompiler suite".
		if (pattern.find_first_of("*?") == std::string_view::npos)
		{
			if (EqualsIgnoreCase(pattern, identifier))
			{
				return true;
			}

			if (identifier.size() > pattern.size()
				&& identifier[pattern.size()] == '.'
				&& EqualsIgnoreCase(pattern, identifier.substr(0, pattern.size())))
			{
				return true;
			}

			return false;
		}

		// Iterative glob match with backtracking over the last '*'.
		std::size_t patternIndex = 0;
		std::size_t identifierIndex = 0;
		std::size_t starIndex = std::string_view::npos;
		std::size_t identifierResumeIndex = 0;

		while (identifierIndex < identifier.size())
		{
			if (patternIndex < pattern.size()
				&& (pattern[patternIndex] == '?'
					|| LowerAscii(pattern[patternIndex]) == LowerAscii(identifier[identifierIndex])))
			{
				++patternIndex;
				++identifierIndex;
			}
			else if (patternIndex < pattern.size() && pattern[patternIndex] == '*')
			{
				starIndex = patternIndex;
				identifierResumeIndex = identifierIndex;
				++patternIndex;
			}
			else if (starIndex != std::string_view::npos)
			{
				patternIndex = starIndex + 1;
				++identifierResumeIndex;
				identifierIndex = identifierResumeIndex;
			}
			else
			{
				return false;
			}
		}

		while (patternIndex < pattern.size() && pattern[patternIndex] == '*')
		{
			++patternIndex;
		}

		return patternIndex == pattern.size();
	}

	void PrintRunnerUsage()
	{
		std::cout <<
			"SwimTests - the Swim Engine test suite.\n"
			"\n"
			"Usage:\n"
			"  SwimTests [options] [filter ...]\n"
			"\n"
			"Every case is identified as \"<suite>.<name>\". Positional arguments are\n"
			"treated as include filters.\n"
			"\n"
			"Options:\n"
			"  --filter=<pattern>   Run cases matching a glob pattern ('*', '?'), repeatable.\n"
			"                       A pattern without wildcards also matches by dotted prefix.\n"
			"  --exclude=<pattern>  Skip cases matching a glob pattern, repeatable.\n"
			"  --list               Print the selected case identifiers and exit.\n"
			"  --list-suites        Print the selected suite names and exit.\n"
			"  --repeat=<count>     Run the selected set <count> times.\n"
			"  --shuffle[=<seed>]   Run the selected set in a randomized order.\n"
			"  --stop-on-failure    Stop after the first failing case.\n"
			"  --report=<path>      Write a JUnit XML report for CI consumption.\n"
			"  --verbose            Print per-case check counts and timings.\n"
			"  --help               Print this message.\n"
			"\n"
			"Exit code is 0 when every selected case passes and 1 otherwise.\n";
	}

	OptionParseResult ParseRunnerOptions(int argc, char** argv)
	{
		OptionParseResult result;

		for (int index = 1; index < argc; ++index)
		{
			const std::string_view argument = argv[index] != nullptr ? std::string_view(argv[index]) : std::string_view{};

			if (argument == "--help" || argument == "-h" || argument == "/?")
			{
				result.Options.ShowHelp = true;
			}
			else if (argument == "--list")
			{
				result.Options.ListTests = true;
			}
			else if (argument == "--list-suites")
			{
				result.Options.ListSuites = true;
			}
			else if (argument == "--stop-on-failure")
			{
				result.Options.StopOnFailure = true;
			}
			else if (argument == "--verbose")
			{
				result.Options.Verbose = true;
			}
			else if (argument == "--shuffle")
			{
				result.Options.Shuffle = true;
			}
			else if (StartsWith(argument, "--shuffle="))
			{
				result.Options.Shuffle = true;
				if (!TryParseSeed(argument.substr(std::string_view("--shuffle=").size()), result.Options.Seed))
				{
					result.Errors.emplace_back("--shuffle expects an unsigned integer seed: " + std::string(argument));
				}
			}
			else if (StartsWith(argument, "--filter="))
			{
				result.Options.IncludePatterns.emplace_back(argument.substr(std::string_view("--filter=").size()));
			}
			else if (StartsWith(argument, "--exclude="))
			{
				result.Options.ExcludePatterns.emplace_back(argument.substr(std::string_view("--exclude=").size()));
			}
			else if (StartsWith(argument, "--repeat="))
			{
				if (!TryParseInt(argument.substr(std::string_view("--repeat=").size()), result.Options.Repeat)
					|| result.Options.Repeat < 1)
				{
					result.Errors.emplace_back("--repeat expects a positive integer: " + std::string(argument));
				}
			}
			else if (StartsWith(argument, "--report="))
			{
				result.Options.ReportPath = std::string(argument.substr(std::string_view("--report=").size()));
			}
			else if (StartsWith(argument, "-"))
			{
				result.Errors.emplace_back("unknown option: " + std::string(argument));
			}
			else
			{
				result.Options.IncludePatterns.emplace_back(argument);
			}
		}

		return result;
	}

	int RunTests(const RunnerOptions& options)
	{
		if (options.ShowHelp)
		{
			PrintRunnerUsage();
			return 0;
		}

		const std::vector<TestCase>& registered = TestRegistry::Get().GetTests();

		std::vector<const TestCase*> selected;
		selected.reserve(registered.size());
		for (const TestCase& test : registered)
		{
			const std::string identifier = test.GetId();

			const bool included = options.IncludePatterns.empty()
				|| std::any_of(options.IncludePatterns.begin(), options.IncludePatterns.end(),
					[&](const std::string& pattern) { return MatchesPattern(pattern, identifier); });
			if (!included)
			{
				continue;
			}

			const bool excluded = std::any_of(options.ExcludePatterns.begin(), options.ExcludePatterns.end(),
				[&](const std::string& pattern) { return MatchesPattern(pattern, identifier); });
			if (excluded)
			{
				continue;
			}

			selected.push_back(&test);
		}

		std::stable_sort(selected.begin(), selected.end(), [](const TestCase* left, const TestCase* right)
		{
			return left->GetId() < right->GetId();
		});

		if (options.ListSuites)
		{
			std::set<std::string> suites;
			for (const TestCase* test : selected)
			{
				suites.insert(test->Suite);
			}
			for (const std::string& suite : suites)
			{
				std::cout << suite << '\n';
			}
			return 0;
		}

		if (options.ListTests)
		{
			for (const TestCase* test : selected)
			{
				std::cout << test->GetId() << '\n';
			}
			return 0;
		}

		if (selected.empty())
		{
			// An empty selection almost always means a mistyped filter in a build
			// script, which must not be reported as a passing run.
			std::cerr << "[swim-tests] No test cases matched the requested filters.\n";
			return 1;
		}

		if (options.Shuffle)
		{
			std::mt19937 generator(options.Seed);
			std::shuffle(selected.begin(), selected.end(), generator);
		}

		std::set<std::string> suiteNames;
		for (const TestCase* test : selected)
		{
			suiteNames.insert(test->Suite);
		}

		std::cout << "[swim-tests] Running " << selected.size() << " case(s) from "
			<< suiteNames.size() << " suite(s)";
		if (options.Repeat > 1)
		{
			std::cout << ", repeated " << options.Repeat << " time(s)";
		}
		if (options.Shuffle)
		{
			std::cout << ", shuffled with seed " << options.Seed;
		}
		std::cout << ".\n";

		std::vector<TestResult> results;
		results.reserve(selected.size() * static_cast<std::size_t>(options.Repeat));

		std::vector<std::string> failedIdentifiers;
		std::size_t totalChecks = 0;
		bool stopped = false;

		const std::chrono::steady_clock::time_point runStart = std::chrono::steady_clock::now();

		for (int iteration = 0; iteration < options.Repeat && !stopped; ++iteration)
		{
			for (const TestCase* test : selected)
			{
				const std::string identifier = test->GetId();
				if (options.Verbose)
				{
					std::cout << "[ RUN      ] " << identifier << '\n';
				}

				TestResult result = RunSingleTest(*test);
				totalChecks += result.CheckCount;

				if (result.Passed())
				{
					if (options.Verbose)
					{
						std::cout << "[       OK ] " << identifier
							<< " (" << result.CheckCount << " checks, "
							<< result.DurationMilliseconds << " ms)\n";
					}
				}
				else
				{
					if (!options.Verbose)
					{
						std::cout << "[ RUN      ] " << identifier << '\n';
					}
					for (const TestFailure& failure : result.Failures)
					{
						std::cout << "             " << FormatFailure(failure) << '\n';
					}
					std::cout << "[  FAILED  ] " << identifier
						<< " (" << result.DurationMilliseconds << " ms)\n";
					failedIdentifiers.push_back(identifier);
				}

				results.push_back(std::move(result));

				if (options.StopOnFailure && !results.back().Passed())
				{
					stopped = true;
					break;
				}
			}
		}

		const std::chrono::steady_clock::time_point runEnd = std::chrono::steady_clock::now();
		const double totalMilliseconds = std::chrono::duration<double, std::milli>(runEnd - runStart).count();

		std::cout << "[----------] " << results.size() << " case(s) ran, "
			<< totalChecks << " check(s), " << totalMilliseconds << " ms.\n";

		if (failedIdentifiers.empty())
		{
			std::cout << "[  PASSED  ] " << results.size() << " case(s).\n";
		}
		else
		{
			std::cout << "[  PASSED  ] " << (results.size() - failedIdentifiers.size()) << " case(s).\n";
			std::cout << "[  FAILED  ] " << failedIdentifiers.size() << " case(s):\n";
			for (const std::string& identifier : failedIdentifiers)
			{
				std::cout << "[  FAILED  ]   " << identifier << '\n';
			}
		}

		if (stopped)
		{
			std::cout << "[swim-tests] Stopped early because --stop-on-failure was requested.\n";
		}

		if (!options.ReportPath.empty() && !WriteJUnitReport(options.ReportPath, results, totalMilliseconds))
		{
			std::cerr << "[swim-tests] Could not write the report file: " << options.ReportPath << '\n';
			return 1;
		}

		return failedIdentifiers.empty() ? 0 : 1;
	}

	int RunTests(int argc, char** argv)
	{
		const OptionParseResult parsed = ParseRunnerOptions(argc, argv);
		if (!parsed.IsValid())
		{
			for (const std::string& error : parsed.Errors)
			{
				std::cerr << "[swim-tests] " << error << '\n';
			}
			std::cerr << '\n';
			PrintRunnerUsage();
			return 2;
		}

		return RunTests(parsed.Options);
	}

} // namespace Swim::Testing
