#include "Tests/Framework/Test.h"

#if defined(SWIM_ENVIRONMENT_SKY_REFLECTION_PATH) && defined(SWIM_ENVIRONMENT_DOWNSAMPLE_REFLECTION_PATH) &&                               \
	defined(SWIM_ENVIRONMENT_PREFILTER_REFLECTION_PATH) && defined(SWIM_ENVIRONMENT_IRRADIANCE_REFLECTION_PATH) &&                         \
	defined(SWIM_ENVIRONMENT_BRDF_LUT_REFLECTION_PATH) && defined(SWIM_RHI_PBR_GALLERY_REFLECTION_PATH)
#include "Engine/Systems/Renderer/Environment/EnvironmentBindings.h"
#include "Engine/Systems/Renderer/Environment/ProceduralSky.h"
#include "Tools/ShaderCompiler/ShaderRhiInterface.h"

#include <map>
#include <string>

using namespace Swim;

namespace
{
	struct ExpectedBinding
	{
		Rhi::DescriptorType Type;
		Rhi::TextureViewDimension Dimension = Rhi::TextureViewDimension::Texture2D;
		Rhi::Format StorageFormat = Rhi::Format::Undefined;
	};

	// Loads a program's reflection and checks its one descriptor space, thread group
	// and push-constant block against the C++ contract.
	Rhi::ShaderProgramInterface CheckProgram(const char* path, std::uint32_t groupX, std::uint32_t groupY, std::uint32_t pushBytes,
		const std::map<std::uint32_t, ExpectedBinding>& expected)
	{
		const auto parsed = ShaderCompiler::LoadSlangReflectionJson(path);
		SWIM_REQUIRE_MESSAGE(parsed, parsed.Error);
		auto converted = ShaderCompiler::BuildRhiShaderInterface(parsed.Reflection);
		SWIM_REQUIRE_MESSAGE(converted, converted.Error);
		const auto& interface = converted.Interface;
		if (groupX != 0)
		{
			SWIM_CHECK((interface.ComputeThreadGroupSize == std::array<std::uint32_t, 3>{ groupX, groupY, 1 }));
		}
		if (pushBytes != 0)
		{
			SWIM_REQUIRE_EQUAL(interface.PushConstants.size(), 1u);
			SWIM_CHECK_EQUAL(interface.PushConstants[0].Size, pushBytes);
		}
		else
		{
			SWIM_CHECK(interface.PushConstants.empty());
		}
		SWIM_REQUIRE_EQUAL(interface.DescriptorSchemas.size(), 1u);
		const auto& bindings = interface.DescriptorSchemas[0].Bindings;
		SWIM_REQUIRE_EQUAL(bindings.size(), expected.size());
		for (const auto& binding : bindings)
		{
			const auto found = expected.find(binding.Binding);
			SWIM_REQUIRE(found != expected.end());
			SWIM_CHECK(binding.Type == found->second.Type);
			if (binding.Type == Rhi::DescriptorType::SampledTexture)
			{
				SWIM_CHECK(binding.SampledDimension == found->second.Dimension);
			}
			if (binding.Type == Rhi::DescriptorType::StorageTexture)
			{
				SWIM_CHECK(binding.StorageTextureFormat == found->second.StorageFormat);
			}
		}
		return interface;
	}
} // namespace

// Every environment program's bindings, thread groups and push constants match EnvironmentBindings.h.
SWIM_TEST("ShaderCompiler.EnvironmentLayout", "EnvironmentProgramsMatchTheirCppContracts")
{
	using T = Rhi::DescriptorType;
	using D = Rhi::TextureViewDimension;
	constexpr auto rgba16 = Rhi::Format::RGBA16Float;
	static_assert(sizeof(Render::Environment::ProceduralSkyConstants) == Render::EnvironmentSkyBindings::PushConstantBytes);
	CheckProgram(SWIM_ENVIRONMENT_SKY_REFLECTION_PATH, 8, 8, Render::EnvironmentSkyBindings::PushConstantBytes,
		{ { Render::EnvironmentSkyBindings::Destination, { T::StorageTexture, D::Texture2D, rgba16 } } });
	CheckProgram(SWIM_ENVIRONMENT_DOWNSAMPLE_REFLECTION_PATH, 8, 8, Render::EnvironmentDownsampleBindings::PushConstantBytes,
		{ { Render::EnvironmentDownsampleBindings::Source, { T::SampledTexture, D::Texture2D } },
			{ Render::EnvironmentDownsampleBindings::Destination, { T::StorageTexture, D::Texture2D, rgba16 } } });
	CheckProgram(SWIM_ENVIRONMENT_PREFILTER_REFLECTION_PATH, 8, 8, Render::EnvironmentPrefilterBindings::PushConstantBytes,
		{ { Render::EnvironmentPrefilterBindings::Source, { T::SampledTexture, D::TextureCube } },
			{ Render::EnvironmentPrefilterBindings::Sampler, { T::Sampler } },
			{ Render::EnvironmentPrefilterBindings::Destination, { T::StorageTexture, D::Texture2D, rgba16 } } });
	CheckProgram(SWIM_ENVIRONMENT_IRRADIANCE_REFLECTION_PATH, Render::EnvironmentIrradianceBindings::ThreadGroupSize, 1,
		Render::EnvironmentIrradianceBindings::PushConstantBytes,
		{ { Render::EnvironmentIrradianceBindings::Source, { T::SampledTexture, D::Texture2DArray } },
			{ Render::EnvironmentIrradianceBindings::Output, { T::StorageBuffer } } });
	CheckProgram(SWIM_ENVIRONMENT_BRDF_LUT_REFLECTION_PATH, 8, 8, Render::EnvironmentBrdfLutBindings::PushConstantBytes,
		{ { Render::EnvironmentBrdfLutBindings::Destination, { T::StorageTexture, D::Texture2D, rgba16 } } });
}

// The gallery smoke's records and resources (Tests/Suites/RHIVulkan/VulkanPbrGallerySmokeTests.cpp).
SWIM_TEST("ShaderCompiler.EnvironmentLayout", "PbrGalleryProgramMatchesItsSmokeLayout")
{
	using T = Rhi::DescriptorType;
	using D = Rhi::TextureViewDimension;
	CheckProgram(SWIM_RHI_PBR_GALLERY_REFLECTION_PATH, 0, 0, 0,
		{ { 0, { T::ReadOnlyStorageBuffer } }, { 1, { T::ReadOnlyStorageBuffer } }, { 2, { T::ReadOnlyStorageBuffer } },
			{ 3, { T::SampledTexture, D::TextureCube } }, { 4, { T::SampledTexture, D::Texture2D } }, { 5, { T::Sampler } } });
	const auto parsed = ShaderCompiler::LoadSlangReflectionJson(SWIM_RHI_PBR_GALLERY_REFLECTION_PATH);
	SWIM_REQUIRE_MESSAGE(parsed, parsed.Error);
	std::map<std::string, std::uint32_t> sizes;
	for (const auto& parameter : parsed.Reflection.GlobalParameters)
	{
		sizes[parameter.Name] = parameter.ElementSize;
	}
	SWIM_CHECK_EQUAL(sizes.at("Spheres"), 48u);
	SWIM_CHECK_EQUAL(sizes.at("Views"), 64u);
}
#endif
