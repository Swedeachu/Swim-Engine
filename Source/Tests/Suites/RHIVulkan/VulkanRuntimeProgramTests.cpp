#include "Engine/Platform/PlatformSystem.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/VulkanRhiBackend.h"
#include "Engine/Systems/Renderer/Runtime/ShaderLibrary.h"
#include "Tests/Fixtures/VulkanSmokeDiagnostics.h"
#include "Tests/Framework/Test.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

namespace
{
#ifdef SWIM_RUNTIME_SHADER_DIR
	// Every compute program of the staged runtime set loads through ShaderLibrary on a real
	// device: its interface passes the library's checks and the backend creates its layout and
	// pipeline. Optional programs are covered too, before a renderer depends on them.
	void RunRuntimeComputePrograms(const Swim::Rhi::GraphicsSystemDesc& graphicsDesc)
	{
		using namespace Swim;
		Platform::PlatformSystem platform;
		SWIM_REQUIRE_MESSAGE(platform.Initialize(), "Runtime program smoke requires working SDL Vulkan platform services");
		auto graphics = RhiVulkan::CreateGraphicsSystem(graphicsDesc);
		SWIM_REQUIRE(graphics);
		Testing::RequireVulkanSmokeValidation(*graphics, graphicsDesc.Checks);
		auto& adapter = graphics->GetAdapter(0);
		auto device = adapter.CreateDevice();
		SWIM_REQUIRE(device);
		const std::filesystem::path root(SWIM_RUNTIME_SHADER_DIR);
		Engine::ShaderLibrary shaders(*device, root);
		std::size_t loaded = 0;

		for (const auto& entry : std::filesystem::directory_iterator(root))
		{
			const auto file = entry.path().filename().string();
			constexpr std::string_view suffix = ".reflection.json";

			if (file.size() <= suffix.size() || !file.ends_with(suffix))
			{
				continue;
			}

			std::ifstream stream(entry.path());
			const std::string reflection((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
			const auto name = file.substr(0, file.size() - suffix.size());

			if (reflection.find("\"stage\": \"compute\"") == std::string::npos || !std::filesystem::exists(root / (name + ".spv")))
			{
				continue;
			}

			if (name == "ScreenSpaceLinearDepth" && !adapter.GetInfo().Capabilities.StorageImageExtendedFormats)
			{
				continue; // The renderer loads it only with the capability.
			}

			try
			{
				const auto program = shaders.LoadCompute(name);
				SWIM_CHECK_MESSAGE(program.Pipeline != nullptr, name);
				++loaded;
			}
			catch (const std::exception& error)
			{
				SWIM_CHECK_MESSAGE(false, name + ": " + error.what());
			}
		}

		SWIM_CHECK(loaded > 0);
	}

	[[maybe_unused]] const bool registered = []
	{
		const char* enabled = std::getenv("SWIM_RUN_RHI_SMOKE");

		if (enabled != nullptr && std::string_view(enabled) == "1")
		{
			Swim::Testing::TestRegistry::Get().Add({ "RHI.Vulkan.Smoke", "EveryRuntimeComputeProgramLoads", SWIM_TEST_LOCATION,
				+[]
				{
					Swim::Testing::RunValidatedVulkanSmoke(&RunRuntimeComputePrograms);
				} });
		}

		return true;
	}();
#endif
} // namespace
