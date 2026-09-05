#include "Tests/Framework/Test.h"
#include "Tools/ShaderCompiler/ShaderReflection.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <string>

namespace
{

	bool ContainsGlobalParameter(
		const Swim::ShaderCompiler::ShaderReflection& reflection,
		const std::string& name)
	{
		return std::any_of(
			reflection.GlobalParameters.begin(),
			reflection.GlobalParameters.end(),
			[&name](const Swim::ShaderCompiler::ShaderBindingReflection& parameter)
			{
				return parameter.Name == name;
			});
	}

	bool ContainsEntryPoint(
		const Swim::ShaderCompiler::ShaderReflection& reflection,
		const std::string& name,
		Swim::ShaderCompiler::ShaderStage stage)
	{
		return std::any_of(
			reflection.EntryPoints.begin(),
			reflection.EntryPoints.end(),
			[&name, stage](const Swim::ShaderCompiler::ShaderEntryPointReflection& entryPoint)
			{
				return entryPoint.Name == name && entryPoint.Stage == stage;
			});
	}

}

SWIM_TEST("ShaderCompiler.Reflection", "ParsesStableSwimOwnedMetadata")
{
	constexpr std::string_view reflectionJson = R"json(
	{
		"version": "1.1",
		"globalScope": {
			"kind": "none",
			"parameters": [
				{
					"name": "Frame",
					"binding": { "kind": "subElementRegisterSpace", "index": 0, "space": 2 },
					"type": { "kind": "parameterBlock" }
				}
			]
		},
		"entryPoints": [
			{
				"name": "vertexMain",
				"stage": "vertex",
				"scope": { "kind": "none", "parameters": [] }
			},
			{
				"name": "computeMain",
				"stage": "compute",
				"threadGroupSize": [8, 4, 1],
				"scope": { "kind": "none", "parameters": [] }
			}
		]
	}
	)json";

	const Swim::ShaderCompiler::ShaderReflectionResult result =
		Swim::ShaderCompiler::ParseSlangReflectionJson(reflectionJson);
	SWIM_REQUIRE(result);
	SWIM_CHECK_EQUAL(result.Reflection.SchemaVersion, std::string("1.1"));
	SWIM_CHECK_EQUAL(result.Reflection.GlobalParameters.size(), std::size_t(1));
	SWIM_CHECK_EQUAL(result.Reflection.GlobalParameters[0].Name, std::string("Frame"));
	SWIM_CHECK(result.Reflection.GlobalParameters[0].HasIndex);
	SWIM_CHECK_EQUAL(result.Reflection.GlobalParameters[0].Index, std::uint32_t(0));
	SWIM_CHECK(result.Reflection.GlobalParameters[0].HasSpace);
	SWIM_CHECK_EQUAL(result.Reflection.GlobalParameters[0].Space, std::uint32_t(2));
	SWIM_CHECK_EQUAL(result.Reflection.EntryPoints.size(), std::size_t(2));
	SWIM_CHECK_EQUAL(result.Reflection.EntryPoints[1].ThreadGroupSize[0], std::uint32_t(8));
	SWIM_CHECK_EQUAL(result.Reflection.EntryPoints[1].ThreadGroupSize[1], std::uint32_t(4));
	SWIM_CHECK_EQUAL(result.Reflection.EntryPoints[1].ThreadGroupSize[2], std::uint32_t(1));
}

#if defined(SWIM_SLANG_REFLECTION_SAMPLE_PATH) && defined(SWIM_SLANG_SPIRV_SAMPLE_PATH)

SWIM_TEST("ShaderCompiler.Slang", "BasicRasterCompilesAndReflects")
{
	const Swim::ShaderCompiler::ShaderReflectionResult reflection =
		Swim::ShaderCompiler::LoadSlangReflectionJson(SWIM_SLANG_REFLECTION_SAMPLE_PATH);
	SWIM_REQUIRE(reflection);

	SWIM_CHECK(ContainsGlobalParameter(reflection.Reflection, "Frame"));
	SWIM_CHECK(ContainsGlobalParameter(reflection.Reflection, "Object"));
	SWIM_CHECK(ContainsGlobalParameter(reflection.Reflection, "Material"));
	SWIM_CHECK(ContainsEntryPoint(
		reflection.Reflection,
		"vertexMain",
		Swim::ShaderCompiler::ShaderStage::Vertex));
	SWIM_CHECK(ContainsEntryPoint(
		reflection.Reflection,
		"fragmentMain",
		Swim::ShaderCompiler::ShaderStage::Fragment));

	std::ifstream spirv(SWIM_SLANG_SPIRV_SAMPLE_PATH, std::ios::binary);
	SWIM_REQUIRE(spirv.good());

	std::uint32_t magic = 0;
	spirv.read(reinterpret_cast<char*>(&magic), sizeof(magic));
	SWIM_REQUIRE(spirv.good());
	SWIM_CHECK_EQUAL(magic, std::uint32_t(0x07230203));
}

#endif
