#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/VulkanRhiBackend.h"

namespace
{
	[[maybe_unused]] Swim::Rhi::GraphicsSystemCreateFunction VulkanFactoryFunction = &Swim::RhiVulkan::CreateGraphicsSystem;
}
