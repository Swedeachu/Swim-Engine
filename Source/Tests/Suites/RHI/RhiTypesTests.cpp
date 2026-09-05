#include "Tests/Framework/Test.h"
#include "Engine/Systems/Renderer/RHI/RhiTypes.h"

#include <cstdint>

SWIM_TEST("RHI.Types", "ClassifiesDepthStencilFormats")
{
	SWIM_CHECK(Swim::Rhi::IsDepthFormat(Swim::Rhi::Format::D32Float));
	SWIM_CHECK(Swim::Rhi::IsDepthFormat(Swim::Rhi::Format::D24UnormS8Uint));
	SWIM_CHECK(!Swim::Rhi::IsDepthFormat(Swim::Rhi::Format::RGBA16Float));
	SWIM_CHECK(Swim::Rhi::HasStencil(Swim::Rhi::Format::D24UnormS8Uint));
	SWIM_CHECK(!Swim::Rhi::HasStencil(Swim::Rhi::Format::D32Float));
}

SWIM_TEST("RHI.Types", "ResourceStatesComposeWithoutBackendTypes")
{
	const Swim::Rhi::ResourceState shaderAccess =
		Swim::Rhi::ResourceState::ShaderRead |
		Swim::Rhi::ResourceState::ShaderWrite;

	SWIM_CHECK(Swim::Rhi::HasAny(shaderAccess, Swim::Rhi::ResourceState::ShaderRead));
	SWIM_CHECK(Swim::Rhi::HasAny(shaderAccess, Swim::Rhi::ResourceState::ShaderWrite));
	SWIM_CHECK(!Swim::Rhi::HasAny(shaderAccess, Swim::Rhi::ResourceState::CopySource));
}

SWIM_TEST("RHI.Capabilities", "DefaultsRequireExplicitBackendOptIn")
{
	const Swim::Rhi::GraphicsCapabilities capabilities;
	SWIM_CHECK(!capabilities.DescriptorIndexing);
	SWIM_CHECK(!capabilities.BufferDeviceAddress);
	SWIM_CHECK(!capabilities.MeshShaders);
	SWIM_CHECK(!capabilities.RayTracingPipeline);
	SWIM_CHECK_EQUAL(capabilities.MaxSamples, std::uint32_t(1));
	SWIM_CHECK_EQUAL(capabilities.MinUniformBufferOffsetAlignment, std::uint64_t(1));
}
