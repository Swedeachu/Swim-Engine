#pragma once
// Native smokes that run on every graphics backend. A smoke written against the RHI loads its
// programs through these helpers and registers with AddSmoke; it then runs once per backend
// (suites RHI.Vulkan.Smoke and RHI.D3D12.Smoke) under that backend's validation.
//
// Program paths are the SPIR-V build outputs (<dir>/<name>.spv, <dir>/<name>.reflection.json).
// The Slang reflection there is the RHI interface for every backend; D3D12 reads the DXIL
// beside it (<name>.dxil) and places the bindings with <name>.dxil.reflection.json.
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/VulkanRhiBackend.h"
#include "Tests/Fixtures/VulkanSmokeDiagnostics.h"
#include "Tests/Framework/Test.h"
#include "Tools/ShaderCompiler/ShaderNativeBindings.h"
#include "Tools/ShaderCompiler/ShaderRhiInterface.h"

#if defined(SWIM_RHI_D3D12)
#include "Engine/Systems/Renderer/RHI/Backends/D3D12/D3D12RhiBackend.h"
#endif

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Swim::Testing
{

	enum class SmokeBackend : std::uint8_t
	{
		Vulkan,
		D3D12,
	};

	inline SmokeBackend& ActiveSmokeBackend()
	{
		static SmokeBackend backend = SmokeBackend::Vulkan;
		return backend;
	}

	inline bool IsD3D12Smoke()
	{
		return ActiveSmokeBackend() == SmokeBackend::D3D12;
	}

	// <dir>/<name>.spv -> <dir>/<name>.dxil on D3D12.
	inline std::filesystem::path SmokeBytecodePath(const char* spirvPath)
	{
		std::filesystem::path path(spirvPath);

		if (IsD3D12Smoke())
		{
			path.replace_extension(".dxil");
		}

		return path;
	}

	inline std::vector<std::byte> ReadSmokeBytecode(const char* spirvPath)
	{
		const auto path = SmokeBytecodePath(spirvPath);
		std::ifstream file(path, std::ios::binary | std::ios::ate);
		SWIM_REQUIRE_MESSAGE(file, "Missing smoke bytecode: " + path.string());
		std::vector<std::byte> bytes(static_cast<std::size_t>(file.tellg()));
		file.seekg(0);
		file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		SWIM_REQUIRE(file && !bytes.empty());
		return bytes;
	}

	// Where the active backend's bytecode expects each binding (empty on Vulkan), by program.
	struct SmokeNativeLayout
	{
		std::vector<Rhi::NativeBinding> Bindings;
		std::vector<Rhi::NativeVertexInput> VertexInputs;
	};

	inline const SmokeNativeLayout& SmokeNatives(const char* reflectionPath)
	{
		static const SmokeNativeLayout none;

		if (!IsD3D12Smoke())
		{
			return none;
		}

		static std::map<std::string, SmokeNativeLayout> cache;
		auto& layout = cache[reflectionPath];

		if (layout.Bindings.empty() && layout.VertexInputs.empty())
		{
			// <dir>/<name>.reflection.json -> <dir>/<name>.dxil.reflection.json
			std::string nativePath(reflectionPath);
			constexpr std::string_view suffix = ".reflection.json";
			SWIM_REQUIRE(nativePath.ends_with(suffix));
			nativePath.insert(nativePath.size() - suffix.size(), ".dxil");
			const auto bindings = ShaderCompiler::LoadSlangReflectionJson(reflectionPath);
			SWIM_REQUIRE_MESSAGE(bindings, bindings.Error);
			const auto registers = ShaderCompiler::LoadSlangReflectionJson(nativePath);
			SWIM_REQUIRE_MESSAGE(registers, registers.Error);
			auto natives = ShaderCompiler::BuildNativeBindings(bindings.Reflection, registers.Reflection);
			SWIM_REQUIRE_MESSAGE(natives, natives.Error);
			const auto sizes = ShaderCompiler::LoadNativeSizeQueries(nativePath, natives.Bindings);
			SWIM_REQUIRE_MESSAGE(sizes.empty(), sizes);
			auto inputs = ShaderCompiler::LoadNativeVertexInputs(nativePath);
			SWIM_REQUIRE_MESSAGE(inputs, inputs.Error);
			layout.Bindings = std::move(natives.Bindings);
			layout.VertexInputs = std::move(inputs.Inputs);
		}

		return layout;
	}

	// BuildRhiShaderInterface of the reflection loaded from reflectionPath, with the active
	// backend's native layout.
	inline ShaderCompiler::ShaderRhiInterfaceResult BuildSmokeInterface(
		const ShaderCompiler::ShaderReflection& reflection, const char* reflectionPath)
	{
		auto converted = ShaderCompiler::BuildRhiShaderInterface(reflection);

		if (converted)
		{
			const auto& natives = SmokeNatives(reflectionPath);
			converted.Interface.NativeBindings = natives.Bindings;
			converted.Interface.NativeVertexInputs = natives.VertexInputs;
		}

		return converted;
	}

	inline Rhi::ShaderProgramInterfaceDesc SmokeInterfaceDesc(const Rhi::ShaderProgramInterface& programInterface)
	{
		return { programInterface.DescriptorSchemas, programInterface.PushConstants, programInterface.ComputeThreadGroupSize,
			programInterface.NativeBindings, programInterface.NativeVertexInputs };
	}

	// A program without a reflected interface (no descriptors or push constants).
	inline Rhi::ShaderProgramInterfaceDesc SmokeInputsOnlyDesc(const char* reflectionPath)
	{
		const auto& natives = SmokeNatives(reflectionPath);
		return { {}, {}, {}, natives.Bindings, natives.VertexInputs };
	}

	// The active backend's graphics system with the requested validation active.
	inline std::unique_ptr<Rhi::GraphicsSystem> CreateSmokeGraphicsSystem(const Rhi::GraphicsSystemDesc& desc)
	{
		std::unique_ptr<Rhi::GraphicsSystem> graphics;
#if defined(SWIM_RHI_D3D12)
		if (IsD3D12Smoke())
		{
			graphics = RhiD3D12::CreateGraphicsSystem(desc);
		}
#endif
		if (!IsD3D12Smoke())
		{
			graphics = RhiVulkan::CreateGraphicsSystem(desc);
		}

		SWIM_REQUIRE_MESSAGE(graphics && graphics->GetAdapterCount() > 0, "Smoke requires an adapter of the backend under test");
		const auto configured = graphics->GetValidationConfiguration();
		SWIM_REQUIRE_MESSAGE(graphics->IsValidationEnabled() && configured.Enabled, "Smoke requires active validation");
		SWIM_REQUIRE_MESSAGE(configured.Checks == desc.Checks, "Smoke validation checks were not configured as requested");
		return graphics;
	}

	// Runs a smoke under the active backend's validation; any unexpected message fails it.
	inline void RunValidatedSmoke(void (*run)(const Rhi::GraphicsSystemDesc&))
	{
		if (!IsD3D12Smoke())
		{
			RunValidatedVulkanSmoke(run);
			return;
		}

		Rhi::GraphicsSystemDesc desc{};
		desc.Validation = Rhi::ValidationMode::Required;
		desc.Diagnostics = std::make_shared<Rhi::DiagnosticLog>();
		desc.EchoDiagnostics = false;
		const char* profile = std::getenv("SWIM_RHI_VALIDATION");
		desc.Checks = ParseVulkanSmokeChecks(profile ? profile : "core");

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

	inline bool SmokesEnabled()
	{
		const char* enabled = std::getenv("SWIM_RUN_RHI_SMOKE");
		return enabled != nullptr && std::string_view(enabled) == "1";
	}

	// Registers Run as <name> in RHI.Vulkan.Smoke and, with the D3D12 backend built, RHI.D3D12.Smoke.
	template <void (*Run)(const Rhi::GraphicsSystemDesc&)> void AddSmoke(const char* name, Swim::Testing::SourceLocation location)
	{
		if (!SmokesEnabled())
		{
			return;
		}

		TestRegistry::Get().Add({ "RHI.Vulkan.Smoke", name, location,
			+[]
			{
				ActiveSmokeBackend() = SmokeBackend::Vulkan;
				RunValidatedSmoke(Run);
			} });
#if defined(SWIM_RHI_D3D12)
		TestRegistry::Get().Add({ "RHI.D3D12.Smoke", name, location,
			+[]
			{
				ActiveSmokeBackend() = SmokeBackend::D3D12;
				RunValidatedSmoke(Run);
				ActiveSmokeBackend() = SmokeBackend::Vulkan;
			} });
#endif
	}

} // namespace Swim::Testing
