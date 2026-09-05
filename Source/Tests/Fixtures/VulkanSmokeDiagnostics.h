#pragma once

#include "Engine/Systems/Renderer/RHI/RhiDiagnostics.h"
#include "Tests/Framework/Test.h"

#include <iostream>

namespace Swim::Testing
{

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
