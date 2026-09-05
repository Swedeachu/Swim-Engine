#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/VulkanRhiBackend.h"
#include "Tests/Framework/Test.h"

SWIM_TEST("RHI.Vulkan", "BackendRegistersExplicitlyWithGraphicsFactory")
{
	Swim::Rhi::GraphicsFactory factory;

	SWIM_CHECK(Swim::RhiVulkan::RegisterGraphicsBackend(factory));
	SWIM_CHECK(factory.IsAvailable(Swim::Rhi::GraphicsApi::Vulkan));
	SWIM_CHECK(!Swim::RhiVulkan::RegisterGraphicsBackend(factory));
	SWIM_CHECK(factory.Unregister(Swim::Rhi::GraphicsApi::Vulkan));
	SWIM_CHECK(!factory.IsAvailable(Swim::Rhi::GraphicsApi::Vulkan));
}
