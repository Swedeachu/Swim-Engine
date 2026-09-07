#pragma once

#include "Engine/Systems/Renderer/RHI/RhiContracts.h"
#include "Tests/Framework/Test.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace Swim::Testing
{

	inline Rhi::ValidationChecks ParseVulkanSmokeChecks(std::string_view profile)
	{
		if (profile.empty() || profile == "core")
		{
			return {};
		}
		if (profile == "sync")
		{
			return { true, false };
		}
		if (profile == "gpu")
		{
			return { false, true };
		}
		if (profile == "all")
		{
			return { true, true };
		}
		throw std::invalid_argument("SWIM_RHI_VALIDATION must be core, sync, gpu or all");
	}

	inline void RequireVulkanSmokeValidation(const Rhi::GraphicsSystem& graphics, const Rhi::ValidationChecks& checks)
	{
		const auto configured = graphics.GetValidationConfiguration();
		SWIM_REQUIRE_MESSAGE(graphics.IsValidationEnabled() && configured.Enabled,
			"Smoke requires active Vulkan validation");
		SWIM_REQUIRE_MESSAGE(configured.Checks == checks, "Smoke validation checks were not configured as requested");
	}

	inline void PrintVulkanSmokeDiagnostics(const Rhi::DiagnosticSnapshot& snapshot)
	{
		for (const auto& message : snapshot.Messages)
		{
			std::cerr << "[RHI diagnostic] " << message.Id << ": " << message.Text << '\n';
		}
		std::cerr << "[RHI validation] warnings=" << snapshot.Warnings << " errors=" << snapshot.Errors
			<< " dropped=" << snapshot.Dropped << '\n';
	}

	inline void RunValidatedVulkanSmoke(void (*run)(const Rhi::GraphicsSystemDesc&))
	{
		Rhi::GraphicsSystemDesc desc{};
		desc.Validation = Rhi::ValidationMode::Required;
		desc.Diagnostics = std::make_shared<Rhi::DiagnosticLog>();
		desc.EchoDiagnostics = false;
		const char* profile = std::getenv("SWIM_RHI_VALIDATION");
		desc.Checks = ParseVulkanSmokeChecks(profile ? profile : "core");
		std::cerr << "[RHI validation request] synchronization=" << desc.Checks.Synchronization
			<< " gpu-assisted=" << desc.Checks.GpuAssisted << '\n';
		try
		{
			run(desc);
		}
		catch (...)
		{
			PrintVulkanSmokeDiagnostics(desc.Diagnostics->Snapshot());
			throw;
		}
		// The inner function has destroyed resources, device, and instance. Include
		// teardown diagnostics, not just the messages observed before GPU draining.
		const auto snapshot = desc.Diagnostics->Snapshot();
		PrintVulkanSmokeDiagnostics(snapshot);
		SWIM_REQUIRE_MESSAGE(snapshot.IsClean(), "Vulkan smoke emitted validation warnings/errors or lost diagnostics (including teardown)");
	}

} // namespace Swim::Testing
