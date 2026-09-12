#include "Tools/ShaderCompiler/ShaderRhiInterface.h"
#include "Tests/Framework/Test.h"

#include <algorithm>
#include <fstream>
#include <map>
#include <set>

using namespace Swim;

namespace
{
	ShaderCompiler::ShaderReflectionResult Parse(const std::string& parameter)
	{
		return ShaderCompiler::ParseSlangReflectionJson(
			"{\"parameters\":[" + parameter + "],\"entryPoints\":[{\"name\":\"computeMain\",\"stage\":\"compute\",\"threadGroupSize\":[8,1,1]}]}");
	}

	void Reject(const ShaderCompiler::ShaderReflection& reflection)
	{
		const auto result = ShaderCompiler::BuildRhiShaderInterface(reflection);
		SWIM_REQUIRE(!result);
		SWIM_CHECK(!result.Error.empty());
		SWIM_CHECK(result.Interface.DescriptorSchemas.empty());
		SWIM_CHECK(result.Interface.PushConstants.empty());
		SWIM_CHECK((result.Interface.ComputeThreadGroupSize == std::array<std::uint32_t, 3>{}));
	}

	std::string Group(const std::string& fields, const std::string& elementBinding =
		R"({"kind":"descriptorTableSlot","index":1,"count":2})")
	{
		return R"({"name":"Root","binding":{"kind":"subElementRegisterSpace","index":2},"type":{"kind":"parameterBlock",
			"containerVarLayout":{"binding":{"kind":"subElementRegisterSpace","index":0}},
			"elementVarLayout":{"binding":)" + elementBinding + R"(,"type":{"kind":"struct","fields":[)" + fields + "]}}}}";
	}

	const std::string Sampler = R"({"name":"Sampler","binding":{"kind":"descriptorTableSlot","index":3},"type":{"kind":"samplerState"}})";
}

SWIM_TEST("ShaderCompiler.NestedLayouts", "RelativeStructAndSetOffsetsResolveOnce")
{
	const auto parsed = Parse(Group(R"({"name":"Pair","binding":{"kind":"descriptorTableSlot","index":2,"count":4},
		"type":{"kind":"struct","fields":[)" + Sampler + "]}}"));
	SWIM_REQUIRE_MESSAGE(parsed, parsed.Error);
	SWIM_REQUIRE_EQUAL(parsed.Reflection.GlobalParameters.size(), 1u);
	const auto& leaf = parsed.Reflection.GlobalParameters[0];
	SWIM_CHECK_EQUAL(leaf.Name, "Root.Pair.Sampler");
	SWIM_CHECK_EQUAL(leaf.Space, 2u);
	SWIM_CHECK_EQUAL(leaf.Index, 6u);
	const auto result = ShaderCompiler::BuildRhiShaderInterface(parsed.Reflection);
	SWIM_REQUIRE_MESSAGE(result, result.Error);
	SWIM_CHECK_EQUAL(result.Interface.DescriptorSchemas[0].Bindings[0].Count, 1u);
}

SWIM_TEST("ShaderCompiler.NestedLayouts", "MixedConstantBufferSeparatesBytesFromDescriptors")
{
	const auto parsed = Parse(R"({"name":"Settings","binding":{"kind":"descriptorTableSlot","index":3,"space":2,"count":2},
		"type":{"kind":"constantBuffer","containerVarLayout":{"binding":{"kind":"descriptorTableSlot","index":0}},
		"elementVarLayout":{"bindings":[{"kind":"descriptorTableSlot","index":1},{"kind":"uniform","offset":0,"size":32}],
		"type":{"kind":"struct","fields":[{"name":"Nested","bindings":[{"kind":"descriptorTableSlot","index":0},{"kind":"uniform","offset":16,"size":16}],
		"type":{"kind":"struct","fields":[{"name":"Value","binding":{"kind":"uniform","offset":4,"size":4},"type":{"kind":"scalar","scalarType":"uint32"}},
		{"name":"Image","binding":{"kind":"descriptorTableSlot","index":0},"type":{"kind":"resource","baseShape":"texture2D","resultType":{"kind":"scalar","scalarType":"uint32"}}}]}}]}}}})");
	SWIM_REQUIRE(parsed);
	const auto result = ShaderCompiler::BuildRhiShaderInterface(parsed.Reflection);
	SWIM_REQUIRE_MESSAGE(result, result.Error);
	SWIM_REQUIRE_EQUAL(parsed.Reflection.GlobalParameters.size(), 2u);
	const auto& uniform = parsed.Reflection.GlobalParameters[0];
	SWIM_CHECK_EQUAL(uniform.Index, 3u);
	SWIM_CHECK_EQUAL(uniform.Space, 2u);
	SWIM_CHECK_EQUAL(uniform.Size, 32u);
	SWIM_REQUIRE_EQUAL(uniform.UniformFields.size(), 1u);
	SWIM_CHECK_EQUAL(uniform.UniformFields[0].Name, "Settings.Nested.Value");
	SWIM_CHECK_EQUAL(uniform.UniformFields[0].Offset, 20u);
	SWIM_CHECK_EQUAL(uniform.UniformFields[0].Size, 4u);
	SWIM_CHECK_EQUAL(parsed.Reflection.GlobalParameters[1].Index, 4u);
	SWIM_CHECK_EQUAL(result.Interface.DescriptorSchemas[0].Bindings[1].SampledClass, Rhi::SampledTextureClass::Uint);
}

SWIM_TEST("ShaderCompiler.NestedLayouts", "MalformedLayoutsAndArithmeticOverflowDiscardTheWholeParameter")
{
	for (const auto& fields : {
		Sampler + ",3",
		Sampler + R"(,{"name":"Bad","binding":{"kind":"uniform","offset":0,"size":4},"type":{"kind":"scalar"}})",
		Sampler + R"(,{"name":"Bad","binding":{"kind":"descriptorTableSlot","index":4294967295},"type":{"kind":"samplerState"}})",
		Sampler + R"(,{"name":"Bad","bindings":[{"kind":"descriptorTableSlot","index":0},{"kind":"descriptorTableSlot","index":1}],"type":{"kind":"struct","fields":[]}})",
		Sampler + R"(,{"name":"Bad","binding":{"kind":"unknown","index":0},"type":{"kind":"struct","fields":[]}})",
		Sampler + R"(,{"name":"Bad","binding":{"kind":"descriptorTableSlot","index":0},"type":{"kind":"struct","fields":{}}})" })
	{
		const auto parsed = Parse(Group(fields));
		SWIM_REQUIRE(parsed);
		SWIM_REQUIRE_EQUAL(parsed.Reflection.GlobalParameters.size(), 1u);
		SWIM_CHECK(parsed.Reflection.GlobalParameters[0].HasUnsupportedBindingLayout);
		Reject(parsed.Reflection);
	}
	for (const auto binding : {
		R"({"kind":"descriptorTableSlot","index":0,"count":0})",
		R"({"kind":"descriptorTableSlot","index":0,"space":-1})",
		R"({"kind":"subElementRegisterSpace","index":4294967295})" })
	{
		const auto parsed = Parse(Group(Sampler, binding));
		SWIM_REQUIRE(parsed);
		Reject(parsed.Reflection);
	}
}

SWIM_TEST("ShaderCompiler.NestedLayouts", "ResolvedCollisionsRejectAcrossGlobalAndEntryDeclarations")
{
	auto parsed = Parse(Group(Sampler));
	SWIM_REQUIRE(parsed);
	const auto leaf = parsed.Reflection.GlobalParameters[0];
	parsed.Reflection.EntryPoints[0].Parameters.push_back(leaf);
	Reject(parsed.Reflection);
	parsed.Reflection.EntryPoints[0].Parameters[0].Space += 1;
	SWIM_CHECK(ShaderCompiler::BuildRhiShaderInterface(parsed.Reflection));
}

SWIM_TEST("ShaderCompiler.NestedLayouts", "UniformExtentAndResourceArraysCannotHideNestedResources")
{
	for (const auto field : {
		R"({"name":"Bad","binding":{"kind":"uniform","offset":12,"size":8},"type":{"kind":"scalar"}})",
		R"({"name":"Bad","binding":{"kind":"uniform","offset":0,"size":16},"type":{"kind":"array","elementCount":2,"elementType":{"kind":"samplerState"}}})",
		R"({"name":"Bad","binding":{"kind":"descriptorTableSlot","index":0},"type":{"kind":"array","elementCount":2,"elementType":{"kind":"parameterBlock"}}})" })
	{
		const auto parsed = Parse(R"({"name":"Root","binding":{"kind":"subElementRegisterSpace","index":0},"type":{"kind":"parameterBlock",
			"containerVarLayout":{"bindings":[{"kind":"descriptorTableSlot","index":0},{"kind":"subElementRegisterSpace","index":0}]},
			"elementVarLayout":{"bindings":[{"kind":"descriptorTableSlot","index":1},{"kind":"uniform","offset":0,"size":16}],
			"type":{"kind":"struct","fields":[)" + std::string(field) + "]}}}}");
		SWIM_REQUIRE(parsed);
		Reject(parsed.Reflection);
	}
}

#ifdef SWIM_RHI_NESTED_PARAMETERS_REFLECTION_PATH
SWIM_TEST("ShaderCompiler.NestedLayouts", "PinnedSlangNestedBlocksPreserveNamesUniformBytesAndArrays")
{
	const auto parsed = ShaderCompiler::LoadSlangReflectionJson(SWIM_RHI_NESTED_PARAMETERS_REFLECTION_PATH);
	SWIM_REQUIRE_MESSAGE(parsed, parsed.Error);
	const auto result = ShaderCompiler::BuildRhiShaderInterface(parsed.Reflection);
	SWIM_REQUIRE_MESSAGE(result, result.Error);
	SWIM_REQUIRE_EQUAL(result.Interface.DescriptorSchemas.size(), 3u);
	SWIM_REQUIRE_EQUAL(parsed.Reflection.GlobalParameters.size(), 7u);
	const auto& root = parsed.Reflection.GlobalParameters[0];
	SWIM_CHECK_EQUAL(root.Name, "Settings");
	SWIM_CHECK_EQUAL(root.Size, 16u);
	SWIM_REQUIRE_EQUAL(root.UniformFields.size(), 2u);
	SWIM_CHECK_EQUAL(root.UniformFields[1].Name, "Settings.Scale");
	SWIM_CHECK_EQUAL(root.UniformFields[1].Offset, 4u);
	SWIM_CHECK_EQUAL(root.UniformFields[1].Size, 4u);
	const auto& child = parsed.Reflection.GlobalParameters[3];
	SWIM_CHECK_EQUAL(child.Name, "Settings.Child");
	SWIM_CHECK_EQUAL(child.Size, 16u);
	SWIM_REQUIRE_EQUAL(child.UniformFields.size(), 1u);
	SWIM_CHECK_EQUAL(child.UniformFields[0].Name, "Settings.Child.Bias");
	for (std::size_t i = 0; i < 2; ++i)
	{
		const auto& schema = result.Interface.DescriptorSchemas[i];
		SWIM_CHECK_EQUAL(schema.Space, i + 2);
		SWIM_REQUIRE_EQUAL(schema.Bindings.size(), 3u);
		SWIM_CHECK_EQUAL(schema.Bindings[0].Type, Rhi::DescriptorType::UniformBuffer);
		SWIM_CHECK_EQUAL(schema.Bindings[0].Count, 1u);
		SWIM_CHECK_EQUAL(schema.Bindings[1].Type, Rhi::DescriptorType::ReadOnlyStorageBuffer);
		SWIM_CHECK_EQUAL(schema.Bindings[1].Count, 2u);
		SWIM_CHECK_EQUAL(schema.Bindings[2].SampledClass, Rhi::SampledTextureClass::Uint);
	}
	SWIM_CHECK_EQUAL(result.Interface.DescriptorSchemas[2].Space, 5u);
	SWIM_CHECK_EQUAL(result.Interface.DescriptorSchemas[2].Bindings[0].Type, Rhi::DescriptorType::StorageBuffer);
}
#endif

#ifdef SWIM_RHI_NESTED_PARAMETERS_SPIRV_PATH
SWIM_TEST("ShaderCompiler.NestedLayouts", "ResolvedCoordinatesMatchActualSpirvDecorations")
{
	const auto parsed = ShaderCompiler::LoadSlangReflectionJson(SWIM_RHI_NESTED_PARAMETERS_REFLECTION_PATH);
	SWIM_REQUIRE(parsed);
	const auto result = ShaderCompiler::BuildRhiShaderInterface(parsed.Reflection);
	SWIM_REQUIRE(result);
	std::ifstream file(SWIM_RHI_NESTED_PARAMETERS_SPIRV_PATH, std::ios::binary | std::ios::ate);
	SWIM_REQUIRE(file);
	const auto bytes = static_cast<std::size_t>(file.tellg());
	SWIM_REQUIRE(bytes >= 20 && bytes % 4 == 0);
	std::vector<std::uint32_t> words(bytes / 4);
	file.seekg(0);
	file.read(reinterpret_cast<char*>(words.data()), static_cast<std::streamsize>(bytes));
	SWIM_REQUIRE(file);
	SWIM_REQUIRE_EQUAL(words[0], 0x07230203u);
	std::map<std::uint32_t, std::uint32_t> sets, bindings;
	for (std::size_t i = 5; i < words.size();)
	{
		const auto count = words[i] >> 16;
		SWIM_REQUIRE(count > 0 && count <= words.size() - i);
		if ((words[i] & 0xffffu) == 71 && count == 4)
		{
			if (words[i + 2] == 33)
			{
				bindings[words[i + 1]] = words[i + 3];
			}
			else if (words[i + 2] == 34)
			{
				sets[words[i + 1]] = words[i + 3];
			}
		}
		i += count;
	}
	std::set<std::pair<std::uint32_t, std::uint32_t>> native, reflected;
	for (const auto& [id, binding] : bindings)
	{
		SWIM_REQUIRE(sets.contains(id));
		native.emplace(sets.at(id), binding);
	}
	for (const auto& schema : result.Interface.DescriptorSchemas)
	{
		for (const auto& binding : schema.Bindings)
		{
			reflected.emplace(schema.Space, binding.Binding);
		}
	}
	SWIM_CHECK(native == reflected);
}
#endif

#ifdef SWIM_RHI_NESTED_GRAPHICS_REFLECTION_PATH
SWIM_TEST("ShaderCompiler.NestedLayouts", "PinnedSlangEntryBlockKeepsFragmentVisibilityAndAbsoluteSet")
{
	const auto parsed = ShaderCompiler::LoadSlangReflectionJson(SWIM_RHI_NESTED_GRAPHICS_REFLECTION_PATH);
	SWIM_REQUIRE_MESSAGE(parsed, parsed.Error);
	const auto result = ShaderCompiler::BuildRhiShaderInterface(parsed.Reflection);
	SWIM_REQUIRE_MESSAGE(result, result.Error);
	SWIM_REQUIRE_EQUAL(result.Interface.DescriptorSchemas.size(), 2u);
	const auto& shared = result.Interface.DescriptorSchemas[0];
	SWIM_CHECK_EQUAL(shared.Space, 1u);
	SWIM_REQUIRE_EQUAL(shared.Bindings.size(), 2u);
	SWIM_CHECK_EQUAL(shared.Bindings[0].Binding, 3u);
	SWIM_CHECK_EQUAL(shared.Bindings[1].Binding, 4u);
	SWIM_CHECK_EQUAL(shared.Bindings[0].Stages, Rhi::ShaderStageMask::Vertex | Rhi::ShaderStageMask::Fragment);
	const auto& material = result.Interface.DescriptorSchemas[1];
	SWIM_CHECK_EQUAL(material.Space, 4u);
	SWIM_REQUIRE_EQUAL(material.Bindings.size(), 3u);
	SWIM_CHECK_EQUAL(material.Bindings[0].Type, Rhi::DescriptorType::UniformBuffer);
	for (const auto& binding : material.Bindings)
	{
		SWIM_CHECK_EQUAL(binding.Stages, Rhi::ShaderStageMask::Fragment);
	}
}
#endif

SWIM_TEST("ShaderCompiler.NestedLayouts", "ExcessiveNestingAndMalformedGlobalScopesCannotDisappear")
{
	std::string nested = Sampler;
	for (int depth = 0; depth < 66; ++depth)
	{
		nested = R"({"name":"Nested","binding":{"kind":"descriptorTableSlot","index":0},"type":{"kind":"struct","fields":[)" + nested + "]}}";
	}
	const auto parsed = Parse(nested);
	SWIM_REQUIRE(parsed);
	Reject(parsed.Reflection);
	for (const auto scope : {
		R"({"kind":"none","parameters":[3]})",
		R"({"kind":"none","parameters":{}})",
		R"({"kind":"constantBuffer","parameters":[]})",
		R"({"kind":"none","binding":{"kind":"descriptorTableSlot","index":0},"parameters":[]})",
		"null" })
	{
		const auto bad = ShaderCompiler::ParseSlangReflectionJson(std::string("{\"globalScope\":") + scope +
			R"(,"entryPoints":[{"name":"main","stage":"fragment"}]})");
		SWIM_REQUIRE(bad);
		Reject(bad.Reflection);
	}
}

SWIM_TEST("ShaderCompiler.NestedLayouts", "ConstantBufferArraysRejectResourceBearingElements")
{
	const auto parsed = Parse(R"({"name":"Buffers","binding":{"kind":"descriptorTableSlot","index":0},
		"type":{"kind":"array","elementCount":2,"elementType":{"kind":"constantBuffer",
		"elementType":{"kind":"struct","fields":[)" + Sampler + "]}}}}");
	SWIM_REQUIRE(parsed);
	Reject(parsed.Reflection);
}
