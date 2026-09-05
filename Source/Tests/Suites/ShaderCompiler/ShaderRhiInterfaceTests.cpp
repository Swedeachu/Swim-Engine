#include "Tools/ShaderCompiler/ShaderRhiInterface.h"
#include "Tests/Framework/Test.h"

using namespace Swim;

SWIM_TEST("ShaderCompiler.RhiInterface", "FlatDescriptorsDeriveSlotsTypesAndStageVisibility")
{
	const auto parsed = ShaderCompiler::ParseSlangReflectionJson(R"json({
		"version":"1.1",
		"parameters":[
			{"name":"image","binding":{"kind":"descriptorTableSlot","space":2,"index":3},"type":{"kind":"resource","baseShape":"texture2D","resultType":{"kind":"scalar","scalarType":"float32"}}},
			{"name":"sampler","binding":{"kind":"descriptorTableSlot","space":2,"index":7},"type":{"kind":"samplerState"}}
		],
		"entryPoints":[{"name":"vertexMain","stage":"vertex"},{"name":"fragmentMain","stage":"fragment"}]
	})json");
	SWIM_REQUIRE(parsed);
	const auto converted = ShaderCompiler::BuildRhiShaderInterface(parsed.Reflection);
	SWIM_REQUIRE_MESSAGE(converted, converted.Error);
	SWIM_REQUIRE_EQUAL(converted.Interface.DescriptorSchemas.size(), 1u);
	const auto& schema = converted.Interface.DescriptorSchemas[0];
	SWIM_CHECK_EQUAL(schema.Space, 2u);
	SWIM_REQUIRE_EQUAL(schema.Bindings.size(), 2u);
	SWIM_CHECK_EQUAL(schema.Bindings[0].Binding, 3u);
	SWIM_CHECK_EQUAL(schema.Bindings[0].Type, Rhi::DescriptorType::SampledTexture);
	SWIM_CHECK_EQUAL(schema.Bindings[1].Binding, 7u);
	SWIM_CHECK_EQUAL(schema.Bindings[1].Type, Rhi::DescriptorType::Sampler);
	SWIM_CHECK_EQUAL(schema.Bindings[1].Stages, Rhi::ShaderStageMask::Vertex | Rhi::ShaderStageMask::Fragment);
}

SWIM_TEST("ShaderCompiler.RhiInterface", "UnsupportedInterfacesAndDuplicateSlotsFailWithoutPartialOutput")
{
	ShaderCompiler::ShaderReflection reflection;
	reflection.EntryPoints.push_back({ "main", ShaderCompiler::ShaderStage::Fragment, {}, {} });
	ShaderCompiler::ShaderBindingReflection binding;
	binding.Name = "sampler";
	binding.BindingKind = "descriptorTableSlot";
	binding.TypeKind = "samplerState";
	binding.HasIndex = true;
	reflection.GlobalParameters.push_back(binding);
	reflection.GlobalParameters.push_back(binding);
	auto result = ShaderCompiler::BuildRhiShaderInterface(reflection);
	SWIM_CHECK(!result);
	SWIM_CHECK(result.Interface.DescriptorSchemas.empty());
	reflection.GlobalParameters.resize(1);
	reflection.GlobalParameters[0].TypeKind = "parameterBlock";
	SWIM_CHECK(!ShaderCompiler::BuildRhiShaderInterface(reflection));
	reflection.GlobalParameters[0].TypeKind = "resource";
	reflection.GlobalParameters[0].ResourceShape = "texture2D";
	reflection.GlobalParameters[0].ResourceAccess = "readWrite";
	SWIM_CHECK(!ShaderCompiler::BuildRhiShaderInterface(reflection));
	reflection.GlobalParameters[0].ResourceAccess.clear();
	reflection.GlobalParameters[0].ResourceScalarType = "uint32";
	SWIM_CHECK(!ShaderCompiler::BuildRhiShaderInterface(reflection));
	reflection.GlobalParameters[0].ResourceScalarType = "float32";
	reflection.GlobalParameters[0].ResourceMultisample = true;
	SWIM_CHECK(!ShaderCompiler::BuildRhiShaderInterface(reflection));
	reflection.GlobalParameters[0].ResourceMultisample = false;
	reflection.GlobalParameters[0].ResourceArray = true;
	SWIM_CHECK(!ShaderCompiler::BuildRhiShaderInterface(reflection));

	reflection.GlobalParameters[0].TypeKind = "array";
	SWIM_CHECK(!ShaderCompiler::BuildRhiShaderInterface(reflection));
}

#ifdef SWIM_RHI_TEXTURE_REFLECTION_PATH
SWIM_TEST("ShaderCompiler.RhiInterface", "CompiledTextureReflectionConvertsWithoutHandwrittenLayout")
{
	const auto reflection = ShaderCompiler::LoadSlangReflectionJson(SWIM_RHI_TEXTURE_REFLECTION_PATH);
	SWIM_REQUIRE_MESSAGE(reflection, reflection.Error);
	const auto converted = ShaderCompiler::BuildRhiShaderInterface(reflection.Reflection);
	SWIM_REQUIRE_MESSAGE(converted, converted.Error);
	SWIM_REQUIRE_EQUAL(converted.Interface.DescriptorSchemas.size(), 1u);
	SWIM_CHECK_EQUAL(converted.Interface.DescriptorSchemas[0].Space, 2u);
	SWIM_CHECK_EQUAL(converted.Interface.DescriptorSchemas[0].Bindings.size(), 2u);
}
#endif
