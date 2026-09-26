#pragma once

#include <span>
#include <string_view>

namespace Game
{
	// What building the Phase 22/23 runtime found (fixed issues, workarounds and open
	// limitations), shown in the sandbox's Findings tab and recorded in
	// docs/EngineRuntime.md. Keep the two in sync.
	struct Finding
	{
		std::string_view Title;
		std::string_view Detail;
		std::string_view Status; // "Fixed", "Workaround" or "Open".
	};

	std::span<const Finding> GetFindings();
} // namespace Game
