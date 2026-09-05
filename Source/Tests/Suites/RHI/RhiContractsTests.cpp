#include "Tests/Framework/Test.h"
#include "Engine/Systems/Renderer/RHI/RhiContracts.h"

#include <type_traits>

namespace
{

	template <typename T>
	concept HasDescriptorSchemasMember = requires(T value)
	{
		value.DescriptorSchemas;
	};

	template <typename T>
	concept HasDescriptorTypeMember = requires(T value)
	{
		value.Type;
	};

}

static_assert(!HasDescriptorSchemasMember<Swim::Rhi::PipelineLayoutDesc>);
static_assert(!HasDescriptorTypeMember<Swim::Rhi::DescriptorWrite>);

SWIM_TEST("RHI.Contracts", "CoreObjectsRemainBackendInterfaces")
{
	SWIM_CHECK(std::is_abstract_v<Swim::Rhi::Adapter>);
	SWIM_CHECK(std::is_abstract_v<Swim::Rhi::Device>);
	SWIM_CHECK(std::is_abstract_v<Swim::Rhi::Queue>);
	SWIM_CHECK(std::is_abstract_v<Swim::Rhi::Swapchain>);
	SWIM_CHECK(std::is_abstract_v<Swim::Rhi::CommandList>);
	SWIM_CHECK(std::is_abstract_v<Swim::Rhi::Buffer>);
	SWIM_CHECK(std::is_abstract_v<Swim::Rhi::Texture>);
	SWIM_CHECK(std::is_abstract_v<Swim::Rhi::ShaderProgram>);
	SWIM_CHECK(std::is_abstract_v<Swim::Rhi::GraphicsPipeline>);
	SWIM_CHECK(std::is_abstract_v<Swim::Rhi::ComputePipeline>);
}

SWIM_TEST("RHI.Contracts", "ResourceDescriptionsAreBackendNeutralValues")
{
	Swim::Rhi::BufferDesc buffer;
	buffer.Size = 4096;
	buffer.Usage = Swim::Rhi::BufferUsage::Storage | Swim::Rhi::BufferUsage::Indirect;

	Swim::Rhi::TextureDesc texture;
	texture.Extent = { 1920, 1080, 1 };
	texture.PixelFormat = Swim::Rhi::Format::RGBA16Float;
	texture.Usage = Swim::Rhi::TextureUsage::Sampled | Swim::Rhi::TextureUsage::ColorAttachment;

	SWIM_CHECK_EQUAL(buffer.Size, std::uint64_t(4096));
	SWIM_CHECK_EQUAL(texture.Extent.Width, std::uint32_t(1920));
	SWIM_CHECK_EQUAL(texture.PixelFormat, Swim::Rhi::Format::RGBA16Float);
}

SWIM_TEST("RHI.Contracts", "PipelineLayoutsAndTablesUseShaderOwnedReflection")
{
	Swim::Rhi::ShaderProgramDesc program;
	Swim::Rhi::PipelineLayoutDesc layout;
	Swim::Rhi::DescriptorTableDesc table;
	Swim::Rhi::DescriptorWrite write;

	layout.Program = nullptr;
	table.Layout = nullptr;
	table.Space = 3;
	write.Binding = 7;

	SWIM_CHECK(program.Interface.DescriptorSchemas.empty());
	SWIM_CHECK(program.Interface.PushConstants.empty());
	SWIM_CHECK_EQUAL(table.Space, std::uint32_t(3));
	SWIM_CHECK_EQUAL(write.Binding, std::uint32_t(7));
}
