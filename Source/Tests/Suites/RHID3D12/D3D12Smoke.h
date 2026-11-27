#pragma once

#include "Engine/Systems/Renderer/RHI/Backends/D3D12/D3D12RhiBackend.h"
#include "Tests/Fixtures/VulkanSmokeDiagnostics.h"
#include "Tests/Framework/Test.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string_view>
#include <vector>

namespace Swim::Testing
{

	// Real-driver D3D12 smokes register only with SWIM_RUN_RHI_SMOKE=1, like the Vulkan ones.
	inline bool D3D12SmokeEnabled()
	{
		const char* enabled = std::getenv("SWIM_RUN_RHI_SMOKE");
		return enabled != nullptr && std::string_view(enabled) == "1";
	}

	inline std::vector<std::byte> ReadD3D12SmokeFile(const std::filesystem::path& path)
	{
		std::ifstream file(path, std::ios::binary | std::ios::ate);
		SWIM_REQUIRE_MESSAGE(file, "Missing smoke artifact: " + path.string());
		const auto size = file.tellg();
		SWIM_REQUIRE(size > 0);
		std::vector<std::byte> bytes(static_cast<std::size_t>(size));
		file.seekg(0);
		file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		SWIM_REQUIRE(file);
		return bytes;
	}

	// The debug layer is required; SWIM_RHI_VALIDATION=gpu|sync|all adds GPU-based or
	// synchronized queue validation. Any warning, error or dropped message fails the smoke,
	// teardown included.
	inline void RunValidatedD3D12Smoke(void (*run)(const Rhi::GraphicsSystemDesc&))
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

		const auto snapshot = desc.Diagnostics->Snapshot();
		PrintVulkanSmokeDiagnostics(snapshot);
		SWIM_REQUIRE_MESSAGE(snapshot.Errors == 0 && snapshot.Warnings == 0 && snapshot.Dropped == 0,
			"D3D12 smoke emitted debug-layer warnings/errors or lost diagnostics (including teardown)");
	}

	inline std::unique_ptr<Rhi::GraphicsSystem> CreateValidatedD3D12System(const Rhi::GraphicsSystemDesc& desc)
	{
		auto graphics = RhiD3D12::CreateGraphicsSystem(desc);
		SWIM_REQUIRE_MESSAGE(graphics && graphics->GetAdapterCount() > 0, "D3D12 smoke requires a feature level 12.0 adapter");
		const auto configured = graphics->GetValidationConfiguration();
		SWIM_REQUIRE_MESSAGE(graphics->IsValidationEnabled() && configured.Enabled, "D3D12 smoke requires the debug layer");
		SWIM_REQUIRE_MESSAGE(configured.Checks == desc.Checks, "D3D12 validation checks were not configured as requested");
		return graphics;
	}

} // namespace Swim::Testing
