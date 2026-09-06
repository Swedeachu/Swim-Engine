#include "Engine/Platform/PlatformSystem.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/VulkanRhiBackend.h"
#include "Tests/Fixtures/TemporaryPipelineCacheFile.h"
#include "Tests/Fixtures/VulkanSmokeDiagnostics.h"
#include "Tests/Framework/Test.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string_view>

namespace
{

	Swim::Rhi::PipelineCacheData CompilePipelines(Swim::Rhi::Device& device, std::span<const std::byte> bytecode)
	{
		using namespace Swim;
		const std::array<Rhi::ShaderStageArtifact, 2> stages{{
			{ Rhi::ShaderStageMask::Vertex, "vertexMain", bytecode },
			{ Rhi::ShaderStageMask::Fragment, "fragmentMain", bytecode }
		}};
		auto program = device.CreateShaderProgram({ stages, {}, "pipeline cache smoke" });
		SWIM_REQUIRE(program);
		auto layout = device.CreatePipelineLayout({ program.get(), "cache smoke layout" });
		SWIM_REQUIRE(layout);
		const auto format = Rhi::Format::RGBA8Unorm;
		Rhi::GraphicsPipelineDesc desc{};
		desc.Program = program.get();
		desc.Layout = layout.get();
		desc.ColorFormats = { &format, 1 };
		desc.DepthStencil.DepthTest = desc.DepthStencil.DepthWrite = false;
		for (auto cull : { Rhi::CullMode::None, Rhi::CullMode::Back, Rhi::CullMode::None })
		{
			desc.Raster.Cull = cull;
			SWIM_REQUIRE(device.CreateGraphicsPipeline(desc));
		}
		return device.GetPipelineCacheData();
	}

	void RunPipelineCacheSmoke(const Swim::Rhi::GraphicsSystemDesc& graphicsDesc)
	{
#ifndef SWIM_RHI_TRIANGLE_SPIRV_PATH
		SWIM_REQUIRE_MESSAGE(false, "Pipeline cache smoke requires SWIM_BUILD_SHADER_COMPILER=ON");
#else
		using namespace Swim;
		Platform::PlatformSystem platform;
		SWIM_REQUIRE_MESSAGE(platform.Initialize(), "Pipeline cache smoke requires a working SDL desktop video driver");
		auto graphics = RhiVulkan::CreateGraphicsSystem(graphicsDesc);
		SWIM_REQUIRE_MESSAGE(graphics, "Pipeline cache smoke requires the full Swim Vulkan baseline and validation");
		SWIM_REQUIRE(graphics->IsValidationEnabled());
		const auto bytecode = platform.GetFileSystem().ReadFileBlocking(SWIM_RHI_TRIANGLE_SPIRV_PATH);
		Testing::TemporaryPipelineCacheFile file;
		Rhi::PipelineCacheData coldData;
		const auto coldStart = std::chrono::steady_clock::now();
		{
			auto device = graphics->GetAdapter(0).CreateDevice();
			SWIM_REQUIRE(device);
			SWIM_CHECK(device->GetPipelineCacheData().Status == Rhi::PipelineCacheDataStatus::Empty);
			coldData = CompilePipelines(*device, bytecode);
			SWIM_REQUIRE(coldData.Status == Rhi::PipelineCacheDataStatus::Ready);
		}
		const auto coldEnd = std::chrono::steady_clock::now();
		file.Save(coldData.Bytes);
		const auto persisted = file.Load();
		SWIM_CHECK(persisted == coldData.Bytes);
		const auto warmStart = std::chrono::steady_clock::now();
		{
			auto device = graphics->GetAdapter(0).CreateDevice();
			SWIM_REQUIRE(device);
			SWIM_REQUIRE(device->LoadPipelineCache(persisted) == Rhi::PipelineCacheLoadStatus::Loaded);
			const auto warmData = CompilePipelines(*device, bytecode);
			SWIM_REQUIRE(warmData.Status == Rhi::PipelineCacheDataStatus::Ready);
			SWIM_CHECK(!warmData.Bytes.empty());
			SWIM_CHECK(device->LoadPipelineCache(persisted) == Rhi::PipelineCacheLoadStatus::AlreadyInitialized);
		}
		const auto warmEnd = std::chrono::steady_clock::now();
		std::fprintf(stderr, "[Swim pipeline cache] persisted=%zu bytes cold-device/build/export=%.3f ms warm-device/load/build/export=%.3f ms\n",
			persisted.size(), std::chrono::duration<double, std::milli>(coldEnd - coldStart).count(),
			std::chrono::duration<double, std::milli>(warmEnd - warmStart).count());
		// Timing is informational: a driver may ignore compatible data or provide
		// its own cache. This test establishes reuse plumbing, not a speedup.
#endif
	}

	[[maybe_unused]] const bool registered = []
	{
		const char* enabled = std::getenv("SWIM_RUN_RHI_SMOKE");
		if (enabled != nullptr && std::string_view(enabled) == "1")
		{
			Swim::Testing::TestRegistry::Get().Add({ "RHI.Vulkan.Smoke", "PipelineCachePersistenceAndReuse", SWIM_TEST_LOCATION,
				+[] { Swim::Testing::RunValidatedVulkanSmoke(&RunPipelineCacheSmoke); } });
		}
		return true;
	}();

} // namespace
