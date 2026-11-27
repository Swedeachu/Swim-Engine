#include "Engine/Systems/Renderer/Runtime/ShaderLibrary.h"
#include "Tests/Suites/RHID3D12/D3D12Smoke.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace
{
#ifdef SWIM_RUNTIME_SHADER_DIR
	// Every compute program of the staged runtime set loads through ShaderLibrary on a D3D12
	// device: its DXIL, its register map and the root signature and pipeline built from them.
	void RunRuntimeComputePrograms(const Swim::Rhi::GraphicsSystemDesc& graphicsDesc)
	{
		using namespace Swim;
		auto graphics = Testing::CreateValidatedD3D12System(graphicsDesc);
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

			if (file.size() <= suffix.size() || !file.ends_with(suffix) || file.ends_with(".dxil.reflection.json"))
			{
				continue;
			}

			std::ifstream stream(entry.path());
			const std::string reflection((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
			const auto name = file.substr(0, file.size() - suffix.size());

			if (reflection.find("\"stage\": \"compute\"") == std::string::npos || !std::filesystem::exists(root / (name + ".dxil")))
			{
				continue;
			}

			if (name == "ScreenSpaceLinearDepth" && !adapter.GetInfo().Capabilities.StorageImageExtendedFormats)
			{
				continue;
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

		std::cerr << "[D3D12 runtime programs] compute programs loaded: " << loaded << '\n';
		SWIM_CHECK(loaded > 0);
	}

	[[maybe_unused]] const bool registered = []
	{
		if (Swim::Testing::D3D12SmokeEnabled())
		{
			Swim::Testing::TestRegistry::Get().Add({ "RHI.D3D12.Smoke", "EveryRuntimeComputeProgramLoads", SWIM_TEST_LOCATION,
				+[]
				{
					Swim::Testing::RunValidatedD3D12Smoke(&RunRuntimeComputePrograms);
				} });
		}

		return true;
	}();
#endif
} // namespace
