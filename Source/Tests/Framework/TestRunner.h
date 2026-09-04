#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Swim::Testing
{

	struct RunnerOptions
	{
		// Glob patterns ('*' and '?', case-insensitive) matched against the full
		// "<suite>.<name>" identifier. A pattern with no wildcard also matches any
		// identifier that has it as a dotted prefix, so "--filter=Physics" selects
		// every case under every Physics suite.
		std::vector<std::string> IncludePatterns;
		std::vector<std::string> ExcludePatterns;

		bool ListTests = false;
		bool ListSuites = false;
		bool StopOnFailure = false;
		bool Verbose = false;
		bool Shuffle = false;
		bool ShowHelp = false;

		std::uint32_t Seed = 0;
		int Repeat = 1;

		std::string ReportPath;
	};

	struct OptionParseResult
	{
		RunnerOptions Options;
		std::vector<std::string> Errors;

		bool IsValid() const
		{
			return Errors.empty();
		}
	};

	OptionParseResult ParseRunnerOptions(int argc, char** argv);

	// Exposed so option handling stays testable without spawning the program.
	bool MatchesPattern(std::string_view pattern, std::string_view identifier);

	void PrintRunnerUsage();

	int RunTests(const RunnerOptions& options);
	int RunTests(int argc, char** argv);

} // namespace Swim::Testing
