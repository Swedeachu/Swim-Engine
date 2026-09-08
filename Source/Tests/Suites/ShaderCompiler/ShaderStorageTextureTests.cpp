#include "Tools/ShaderCompiler/ShaderRhiInterface.h"
#include "Tests/Framework/Test.h"

using namespace Swim;

namespace
{
	ShaderCompiler::ShaderReflectionResult ParseImage(std::string_view format, std::string_view scalar, std::uint32_t components)
	{
		const auto resultType = components == 1 ?
			std::string("{\"kind\":\"scalar\",\"scalarType\":\"") + std::string(scalar) + "\"}" :
			std::string("{\"kind\":\"vector\",\"elementCount\":") + std::to_string(components) +
			",\"elementType\":{\"kind\":\"scalar\",\"scalarType\":\"" + std::string(scalar) + "\"}}";
		return ShaderCompiler::ParseSlangReflectionJson(std::string(R"json({
			"parameters":[{"name":"Output","binding":{"kind":"descriptorTableSlot","index":7,"space":1},
			"format":")json") + std::string(format) + R"json(","type":{"kind":"resource","baseShape":"texture2D",
			"access":"readWrite","resultType":)json" + resultType + R"json(}}],
			"entryPoints":[{"name":"computeMain","stage":"compute","threadGroupSize":[8,8,1]}]
		})json");
	}
}

SWIM_TEST("ShaderCompiler.StorageTexture", "ExplicitFormatsPreserveNumericClassAndExactViewContract")
{
	struct Case
	{
		const char* Qualifier;
		const char* Scalar;
		std::uint32_t Components;
		Rhi::Format Format;
	};
	const std::array<Case, 13> cases{{
		{ "r32f", "float32", 1, Rhi::Format::R32Float }, { "r32ui", "uint32", 1, Rhi::Format::R32Uint },
		{ "r32i", "int32", 1, Rhi::Format::R32Sint }, { "rgba32f", "float32", 4, Rhi::Format::RGBA32Float },
		{ "rgba32ui", "uint32", 4, Rhi::Format::RGBA32Uint }, { "rgba32i", "int32", 4, Rhi::Format::RGBA32Sint },
		{ "rgba16f", "float32", 4, Rhi::Format::RGBA16Float }, { "rgba16ui", "uint32", 4, Rhi::Format::RGBA16Uint },
		{ "rgba16i", "int32", 4, Rhi::Format::RGBA16Sint }, { "rgba8", "float32", 4, Rhi::Format::RGBA8Unorm },
		{ "rgba8_snorm", "float32", 4, Rhi::Format::RGBA8Snorm }, { "rgba8ui", "uint32", 4, Rhi::Format::RGBA8Uint },
		{ "rgba8i", "int32", 4, Rhi::Format::RGBA8Sint }
	}};
	for (const auto& expected : cases)
	{
		auto parsed = ParseImage(expected.Qualifier, expected.Scalar, expected.Components);
		SWIM_REQUIRE(parsed);
		SWIM_CHECK_EQUAL(parsed.Reflection.GlobalParameters[0].ResourceFormat, std::string(expected.Qualifier));
		SWIM_CHECK_EQUAL(parsed.Reflection.GlobalParameters[0].ResourceComponentCount, expected.Components);
		const auto converted = ShaderCompiler::BuildRhiShaderInterface(parsed.Reflection);
		SWIM_REQUIRE_MESSAGE(converted, converted.Error);
		parsed.Reflection = {};
		SWIM_REQUIRE_EQUAL(converted.Interface.DescriptorSchemas.size(), 1u);
		const auto& schema = converted.Interface.DescriptorSchemas[0];
		SWIM_CHECK_EQUAL(schema.Space, 1u);
		SWIM_REQUIRE_EQUAL(schema.Bindings.size(), 1u);
		SWIM_CHECK_EQUAL(schema.Bindings[0].Binding, 7u);
		SWIM_CHECK_EQUAL(schema.Bindings[0].Type, Rhi::DescriptorType::StorageTexture);
		SWIM_CHECK_EQUAL(schema.Bindings[0].StorageTextureFormat, expected.Format);
		SWIM_CHECK_EQUAL(schema.Bindings[0].Stages, Rhi::ShaderStageMask::Compute);
		SWIM_CHECK(Rhi::IsStorageTextureFormat(expected.Format));
	}
}

SWIM_TEST("ShaderCompiler.StorageTexture", "MissingUnsupportedOrMismatchedFormatsRejectWithoutPartialOutput")
{
	for (const auto format : { "", "unknown", "rgba8_srgb", "bgra8", "rg16f" })
	{
		const auto parsed = ParseImage(format, "float32", 4);
		SWIM_REQUIRE(parsed);
		const auto converted = ShaderCompiler::BuildRhiShaderInterface(parsed.Reflection);
		SWIM_CHECK(!converted);
		SWIM_CHECK(converted.Interface.DescriptorSchemas.empty());
		SWIM_CHECK((converted.Interface.ComputeThreadGroupSize == std::array<std::uint32_t, 3>{}));
	}
	for (const auto scalar : { "uint32", "int32", "float16", "float64", "" })
	{
		const auto parsed = ParseImage("rgba32f", scalar, 4);
		SWIM_REQUIRE(parsed);
		SWIM_CHECK(!ShaderCompiler::BuildRhiShaderInterface(parsed.Reflection));
	}
	for (auto components : { 0u, 1u, 2u, 3u, 5u, UINT32_MAX })
	{
		const auto parsed = ParseImage("rgba32f", "float32", components);
		SWIM_REQUIRE(parsed);
		SWIM_CHECK(!ShaderCompiler::BuildRhiShaderInterface(parsed.Reflection));
	}
}

SWIM_TEST("ShaderCompiler.StorageTexture", "UnsupportedShapesArraysAndGraphicsStoresReject")
{
	const auto parsed = ParseImage("rgba32f", "float32", 4);
	SWIM_REQUIRE(parsed);
	for (std::uint32_t invalid = 0; invalid < 7; ++invalid)
	{
		auto reflection = parsed.Reflection;
		auto& parameter = reflection.GlobalParameters[0];
		if (invalid == 0)
		{
			parameter.ResourceShape = "texture3D";
		}
		if (invalid == 1)
		{
			parameter.ResourceArray = true;
		}
		if (invalid == 2)
		{
			parameter.ResourceMultisample = true;
		}
		if (invalid == 3)
		{
			parameter.TypeKind = "array";
		}
		if (invalid == 4)
		{
			reflection.EntryPoints[0].Stage = ShaderCompiler::ShaderStage::Fragment;
		}
		if (invalid == 5)
		{
			parameter.ResourceAccess = "write";
		}
		if (invalid == 6)
		{
			reflection.EntryPoints[0].Parameters.push_back(parameter);
		}
		SWIM_CHECK(!ShaderCompiler::BuildRhiShaderInterface(reflection));
	}
}

#ifdef SWIM_RHI_STORAGE_TEXTURE_REFLECTION_PATH
SWIM_TEST("ShaderCompiler.StorageTexture", "PinnedSlangArtifactReflectsFloatUnsignedAndSignedImages")
{
	const auto parsed = ShaderCompiler::LoadSlangReflectionJson(SWIM_RHI_STORAGE_TEXTURE_REFLECTION_PATH);
	SWIM_REQUIRE_MESSAGE(parsed, parsed.Error);
	const auto converted = ShaderCompiler::BuildRhiShaderInterface(parsed.Reflection);
	SWIM_REQUIRE_MESSAGE(converted, converted.Error);
	SWIM_REQUIRE_EQUAL(converted.Interface.DescriptorSchemas.size(), 1u);
	const auto& schema = converted.Interface.DescriptorSchemas[0];
	SWIM_CHECK_EQUAL(schema.Space, 1u);
	SWIM_REQUIRE_EQUAL(schema.Bindings.size(), 3u);
	for (const auto& binding : schema.Bindings)
	{
		SWIM_CHECK_EQUAL(binding.Type, Rhi::DescriptorType::StorageTexture);
		SWIM_CHECK_EQUAL(binding.Stages, Rhi::ShaderStageMask::Compute);
		SWIM_CHECK_EQUAL(binding.StorageTextureFormat, binding.Binding == 3 ? Rhi::Format::RGBA32Float :
			binding.Binding == 7 ? Rhi::Format::R32Uint : Rhi::Format::R32Sint);
	}
	SWIM_CHECK((converted.Interface.ComputeThreadGroupSize == std::array<std::uint32_t, 3>{ 8, 8, 1 }));
	SWIM_REQUIRE_EQUAL(converted.Interface.PushConstants.size(), 1u);
	SWIM_CHECK_EQUAL(converted.Interface.PushConstants[0].Size, 24u);
}
#endif
