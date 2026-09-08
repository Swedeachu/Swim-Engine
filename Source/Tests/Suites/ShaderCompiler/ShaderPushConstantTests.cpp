#include "Tools/ShaderCompiler/ShaderRhiInterface.h"
#include "Tests/Framework/Test.h"

using namespace Swim;

namespace
{
	ShaderCompiler::ShaderReflectionResult ParsePushBlock(std::string_view elementBinding)
	{
		return ShaderCompiler::ParseSlangReflectionJson(std::string(R"json({
			"parameters":[{"name":"Draw","binding":{"kind":"pushConstantBuffer","index":7,"offset":4,"size":4},
			"type":{"kind":"constantBuffer","elementVarLayout":)json") + std::string(elementBinding) + R"json(}}],
			"entryPoints":[{"name":"main","stage":"fragment"}]
		})json");
	}
}

SWIM_TEST("ShaderCompiler.PushConstants", "ByteExtentUsesUniformElementLayoutAndActualProgramStages")
{
	const auto parsed = ParsePushBlock(R"json({"binding":{"kind":"uniform","offset":16,"size":32}})json");
	SWIM_REQUIRE(parsed);
	SWIM_REQUIRE_EQUAL(parsed.Reflection.GlobalParameters.size(), 1u);
	const auto& parameter = parsed.Reflection.GlobalParameters[0];
	SWIM_CHECK_EQUAL(parameter.Index, 7u);
	SWIM_CHECK(parameter.HasOffset && parameter.HasSize);
	SWIM_CHECK_EQUAL(parameter.Offset, 16u);
	SWIM_CHECK_EQUAL(parameter.Size, 32u);
	const auto converted = ShaderCompiler::BuildRhiShaderInterface(parsed.Reflection);
	SWIM_REQUIRE_MESSAGE(converted, converted.Error);
	SWIM_CHECK(converted.Interface.DescriptorSchemas.empty());
	SWIM_REQUIRE_EQUAL(converted.Interface.PushConstants.size(), 1u);
	SWIM_CHECK_EQUAL(converted.Interface.PushConstants[0].Offset, 16u);
	SWIM_CHECK_EQUAL(converted.Interface.PushConstants[0].Size, 32u);
	SWIM_CHECK_EQUAL(converted.Interface.PushConstants[0].Stages, Rhi::ShaderStageMask::Fragment);
}

SWIM_TEST("ShaderCompiler.PushConstants", "MissingMalformedAndUnalignedElementLayoutsFailClosed")
{
	const std::array<std::string_view, 12> invalid{
		R"json({})json",
		R"json({"binding":{"kind":"uniform","size":16}})json",
		R"json({"binding":{"kind":"uniform","offset":0}})json",
		R"json({"binding":{"kind":"descriptorTableSlot","offset":0,"size":16}})json",
		R"json({"binding":{"kind":"uniform","offset":0,"size":16},"bindings":[]})json",
		R"json({"binding":{"kind":"uniform","offset":-4,"size":16}})json",
		R"json({"binding":{"kind":"uniform","offset":0,"size":4294967296}})json",
		R"json({"binding":{"kind":"uniform","offset":0,"size":"16"}})json",
		R"json({"binding":{"kind":"uniform","offset":0,"size":0}})json",
		R"json({"binding":{"kind":"uniform","offset":2,"size":16}})json",
		R"json({"binding":{"kind":"uniform","offset":0,"size":6}})json",
		R"json({"binding":{"kind":"uniform","offset":4294967292,"size":8}})json"
	};
	for (const auto json : invalid)
	{
		const auto parsed = ParsePushBlock(json);
		SWIM_REQUIRE(parsed);
		const auto converted = ShaderCompiler::BuildRhiShaderInterface(parsed.Reflection);
		SWIM_CHECK(!converted);
		SWIM_CHECK(converted.Interface.PushConstants.empty());
		SWIM_CHECK(converted.Interface.DescriptorSchemas.empty());
	}
}

SWIM_TEST("ShaderCompiler.PushConstants", "MultipleBlocksAndUnsupportedTypesDiscardAllOutput")
{
	auto parsed = ParsePushBlock(R"json({"binding":{"kind":"uniform","offset":0,"size":16}})json");
	SWIM_REQUIRE(parsed);
	ShaderCompiler::ShaderBindingReflection sampler;
	sampler.Name = "Sampler";
	sampler.BindingKind = "descriptorTableSlot";
	sampler.TypeKind = "samplerState";
	sampler.HasIndex = true;
	parsed.Reflection.GlobalParameters.insert(parsed.Reflection.GlobalParameters.begin(), sampler);
	const auto valid = parsed.Reflection.GlobalParameters.back();
	parsed.Reflection.GlobalParameters.push_back(valid);
	auto converted = ShaderCompiler::BuildRhiShaderInterface(parsed.Reflection);
	SWIM_CHECK(!converted);
	SWIM_CHECK(converted.Interface.DescriptorSchemas.empty());
	SWIM_CHECK(converted.Interface.PushConstants.empty());
	parsed.Reflection.GlobalParameters.pop_back();
	for (const auto kind : { "parameterBlock", "array", "resource" })
	{
		parsed.Reflection.GlobalParameters.back().TypeKind = kind;
		converted = ShaderCompiler::BuildRhiShaderInterface(parsed.Reflection);
		SWIM_CHECK(!converted);
		SWIM_CHECK(converted.Interface.DescriptorSchemas.empty());
		SWIM_CHECK(converted.Interface.PushConstants.empty());
	}
	parsed.Reflection.GlobalParameters.back() = valid;
	parsed.Reflection.GlobalParameters.back().Count = 2;
	SWIM_CHECK(!ShaderCompiler::BuildRhiShaderInterface(parsed.Reflection));
	parsed.Reflection.GlobalParameters.back() = valid;
	converted = ShaderCompiler::BuildRhiShaderInterface(parsed.Reflection);
	SWIM_REQUIRE(converted);
	SWIM_CHECK_EQUAL(converted.Interface.DescriptorSchemas.size(), 1u);
	SWIM_CHECK_EQUAL(converted.Interface.PushConstants.size(), 1u);
}

SWIM_TEST("ShaderCompiler.PushConstants", "EntryPointResourceScopeAndMissingComputeLocalSizeReject")
{
	auto parsed = ParsePushBlock(R"json({"binding":{"kind":"uniform","offset":0,"size":16}})json");
	SWIM_REQUIRE(parsed);
	parsed.Reflection.EntryPoints[0].Parameters = parsed.Reflection.GlobalParameters;
	parsed.Reflection.GlobalParameters.clear();
	SWIM_CHECK(!ShaderCompiler::BuildRhiShaderInterface(parsed.Reflection));
	parsed.Reflection.GlobalParameters = parsed.Reflection.EntryPoints[0].Parameters;
	parsed.Reflection.EntryPoints[0].Parameters.clear();
	parsed.Reflection.EntryPoints[0].Stage = ShaderCompiler::ShaderStage::Compute;
	SWIM_CHECK(!ShaderCompiler::BuildRhiShaderInterface(parsed.Reflection));
}

#ifdef SWIM_RHI_PUSH_CONSTANTS_REFLECTION_PATH
SWIM_TEST("ShaderCompiler.PushConstants", "CompiledSlangBlockConvertsWithoutHandwrittenRanges")
{
	const auto parsed = ShaderCompiler::LoadSlangReflectionJson(SWIM_RHI_PUSH_CONSTANTS_REFLECTION_PATH);
	SWIM_REQUIRE_MESSAGE(parsed, parsed.Error);
	const auto converted = ShaderCompiler::BuildRhiShaderInterface(parsed.Reflection);
	SWIM_REQUIRE_MESSAGE(converted, converted.Error);
	SWIM_CHECK(converted.Interface.DescriptorSchemas.empty());
	SWIM_REQUIRE_EQUAL(converted.Interface.PushConstants.size(), 1u);
	const auto& range = converted.Interface.PushConstants[0];
	SWIM_CHECK_EQUAL(range.Offset, 0u);
	SWIM_CHECK_EQUAL(range.Size, 32u);
	SWIM_CHECK_EQUAL(range.Stages, Rhi::ShaderStageMask::Vertex | Rhi::ShaderStageMask::Fragment);
}
#endif
