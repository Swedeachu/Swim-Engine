#include "Tests/Framework/Test.h"
#include "Tools/ShaderCompiler/ShaderRhiInterface.h"

using namespace Swim;

namespace
{

	ShaderCompiler::ShaderReflectionResult ParseSampled(std::string_view scalar, bool local, bool array)
	{
		const std::string resource = "{\"kind\":\"resource\",\"baseShape\":\"texture2D\",\"resultType\":{\"kind\":\"scalar\",\"scalarType\":\"" +
			std::string(scalar) + "\"}}";
		const std::string type = array ? "{\"kind\":\"array\",\"elementCount\":2,\"elementType\":" + resource + "}" : resource;
		const std::string parameter = "{\"name\":\"image\",\"binding\":{\"kind\":\"descriptorTableSlot\",\"index\":5,\"space\":1},\"type\":" + type + "}";
		return ShaderCompiler::ParseSlangReflectionJson("{\"parameters\":[" + (local ? std::string{} : parameter) +
			"],\"entryPoints\":[{\"name\":\"computeMain\",\"stage\":\"compute\",\"threadGroupSize\":[8,1,1],\"parameters\":[" +
			(local ? parameter : std::string{}) + "]}]}");
	}

}

SWIM_TEST("ShaderCompiler.SampledTextures", "NumericClassesConvertForGlobalsLocalsAndArrays")
{
	const std::array scalars{ "float32", "uint32", "int32" };
	const std::array classes{ Rhi::SampledTextureClass::Float, Rhi::SampledTextureClass::Uint, Rhi::SampledTextureClass::Sint };
	for (std::size_t index = 0; index < scalars.size(); ++index)
	{
		for (bool local : { false, true })
		{
			for (bool array : { false, true })
			{
				const auto parsed = ParseSampled(scalars[index], local, array);
				SWIM_REQUIRE_MESSAGE(parsed, parsed.Error);
				const auto converted = ShaderCompiler::BuildRhiShaderInterface(parsed.Reflection);
				SWIM_REQUIRE_MESSAGE(converted, converted.Error);
				SWIM_REQUIRE_EQUAL(converted.Interface.DescriptorSchemas.size(), 1u);
				const auto& schema = converted.Interface.DescriptorSchemas[0];
				SWIM_CHECK_EQUAL(schema.Space, 1u);
				SWIM_REQUIRE_EQUAL(schema.Bindings.size(), 1u);
				const auto& binding = schema.Bindings[0];
				SWIM_CHECK_EQUAL(binding.Type, Rhi::DescriptorType::SampledTexture);
				SWIM_CHECK_EQUAL(binding.SampledClass, classes[index]);
				SWIM_CHECK_EQUAL(binding.StorageTextureFormat, Rhi::Format::Undefined);
				SWIM_CHECK_EQUAL(binding.Count, array ? 2u : 1u);
				SWIM_CHECK_EQUAL(binding.Binding, 5u);
				SWIM_CHECK_EQUAL(binding.Stages, Rhi::ShaderStageMask::Compute);
			}
		}
	}
}

SWIM_TEST("ShaderCompiler.SampledTextures", "UnsupportedScalarShapeAndResultWidthDiscardTheInterface")
{
	for (const auto scalar : { "", "float16", "float64", "int16", "int64", "uint64", "bool" })
	{
		const auto parsed = ParseSampled(scalar, false, false);
		SWIM_REQUIRE(parsed);
		const auto converted = ShaderCompiler::BuildRhiShaderInterface(parsed.Reflection);
		SWIM_CHECK(!converted);
		SWIM_CHECK(converted.Interface.DescriptorSchemas.empty());
		SWIM_CHECK((converted.Interface.ComputeThreadGroupSize == std::array<std::uint32_t, 3>{}));
	}
	const auto parsed = ParseSampled("int32", false, false);
	SWIM_REQUIRE(parsed);
	for (std::uint32_t invalid = 0; invalid < 6; ++invalid)
	{
		auto reflection = parsed.Reflection;
		auto& image = reflection.GlobalParameters[0];
		if (invalid == 0)
		{
			image.ResourceArray = true;
			image.ResourceShape = "texture3D";
		}
		if (invalid == 1)
		{
			image.ResourceMultisample = true;
		}
		if (invalid == 2)
		{
			image.ResourceShape = "unknownTexture";
		}
		if (invalid == 3)
		{
			image.ResourceComponentCount = 0;
		}
		if (invalid == 4)
		{
			image.ResourceComponentCount = 5;
		}
		if (invalid == 5)
		{
			image.ResourceAccess = "readWrite";
		}
		const auto result = ShaderCompiler::BuildRhiShaderInterface(reflection);
		SWIM_CHECK(!result);
		SWIM_CHECK(result.Interface.DescriptorSchemas.empty());
	}
}

SWIM_TEST("ShaderCompiler.SampledTextures", "GraphicsVisibilityAndCollisionRulesRemainExplicit")
{
	const auto parsed = ParseSampled("uint32", true, true);
	SWIM_REQUIRE(parsed);
	auto reflection = parsed.Reflection;
	reflection.EntryPoints[0].Stage = ShaderCompiler::ShaderStage::Fragment;
	auto result = ShaderCompiler::BuildRhiShaderInterface(reflection);
	SWIM_REQUIRE(result);
	SWIM_CHECK_EQUAL(result.Interface.DescriptorSchemas[0].Bindings[0].Stages, Rhi::ShaderStageMask::Fragment);
	reflection.EntryPoints[0].Stage = ShaderCompiler::ShaderStage::Vertex;
	result = ShaderCompiler::BuildRhiShaderInterface(reflection);
	SWIM_REQUIRE(result);
	SWIM_CHECK_EQUAL(result.Interface.DescriptorSchemas[0].Bindings[0].Stages, Rhi::ShaderStageMask::Vertex);
	reflection.GlobalParameters.push_back(reflection.EntryPoints[0].Parameters[0]);
	reflection.GlobalParameters[0].ResourceScalarType = "int32";
	result = ShaderCompiler::BuildRhiShaderInterface(reflection);
	SWIM_CHECK(!result);
	SWIM_CHECK(result.Interface.DescriptorSchemas.empty());
}

#ifdef SWIM_RHI_SAMPLED_INTEGER_REFLECTION_PATH
SWIM_TEST("ShaderCompiler.SampledTextures", "PinnedSlangPreservesThreeClassesAndComputeEntry")
{
	const auto parsed = ShaderCompiler::LoadSlangReflectionJson(SWIM_RHI_SAMPLED_INTEGER_REFLECTION_PATH);
	SWIM_REQUIRE_MESSAGE(parsed, parsed.Error);
	SWIM_REQUIRE_EQUAL(parsed.Reflection.EntryPoints.size(), 1u);
	SWIM_CHECK_EQUAL(parsed.Reflection.EntryPoints[0].Name, std::string("computeMain"));
	const auto converted = ShaderCompiler::BuildRhiShaderInterface(parsed.Reflection);
	SWIM_REQUIRE_MESSAGE(converted, converted.Error);
	SWIM_REQUIRE_EQUAL(converted.Interface.DescriptorSchemas.size(), 2u);
	const auto& first = converted.Interface.DescriptorSchemas[0].Bindings;
	const auto& second = converted.Interface.DescriptorSchemas[1].Bindings;
	SWIM_REQUIRE_EQUAL(first.size(), 2u);
	SWIM_REQUIRE_EQUAL(second.size(), 2u);
	SWIM_CHECK_EQUAL(first[0].SampledClass, Rhi::SampledTextureClass::Uint);
	SWIM_CHECK_EQUAL(first[0].Count, 2u);
	SWIM_CHECK_EQUAL(first[1].SampledClass, Rhi::SampledTextureClass::Float);
	SWIM_CHECK_EQUAL(second[0].Type, Rhi::DescriptorType::StorageBuffer);
	SWIM_CHECK_EQUAL(second[1].SampledClass, Rhi::SampledTextureClass::Sint);
	SWIM_CHECK_EQUAL(second[1].Binding, 5u);
	SWIM_REQUIRE_EQUAL(converted.Interface.PushConstants.size(), 1u);
	SWIM_CHECK_EQUAL(converted.Interface.PushConstants[0].Size, 16u);
}
#endif
