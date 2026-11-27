#include "Tests/Framework/Test.h"
#include "Tools/ShaderCompiler/ShaderNativeBindings.h"
#include "Tools/ShaderCompiler/ShaderReflection.h"

#include <string>
#include <string_view>
#include <vector>

namespace
{

	using namespace Swim;

	// Register-target reflection of a vertex stage: a struct input whose field
	// locations are relative to it, a loose input after it, and a system value.
	constexpr std::string_view VertexReflection =
		R"json({"parameters":[],"entryPoints":[
		{"name":"vertexMain","stage":"vertex","parameters":[
			{"name":"input","stage":"vertex","binding":{"kind":"varyingInput","index":1,"count":2},
				"type":{"kind":"struct","name":"VertexInput","fields":[
					{"name":"Position","type":{"kind":"vector","elementCount":2},"binding":{"kind":"varyingInput","index":0},
						"semanticName":"POSITION"},
					{"name":"Color","type":{"kind":"vector","elementCount":4},"binding":{"kind":"varyingInput","index":1},
						"semanticName":"COLOR","semanticIndex":1}]}},
			{"name":"extra","stage":"vertex","binding":{"kind":"varyingInput","index":0},"type":{"kind":"vector"},
				"semanticName":"TEXCOORD","semanticIndex":3},
			{"name":"vertexId","stage":"vertex","binding":{"kind":"varyingInput","index":3},"type":{"kind":"scalar"},
				"semanticName":"SV_VERTEXID"}]},
		{"name":"fragmentMain","stage":"fragment","parameters":[
			{"name":"input","stage":"fragment","binding":{"kind":"varyingInput","index":0},"type":{"kind":"vector"},
				"semanticName":"COLOR"}]}]})json";

} // namespace

SWIM_TEST("ShaderCompiler.NativeBindings", "VertexInputsMapLocationsToSemantics")
{
	const auto parsed = ShaderCompiler::ParseNativeVertexInputs(VertexReflection);
	SWIM_REQUIRE_MESSAGE(parsed, parsed.Error);
	SWIM_REQUIRE_EQUAL(parsed.Inputs.size(),
		std::size_t(3)); // No system value, no fragment input.
	SWIM_CHECK(parsed.Inputs[0].Location == 0u && parsed.Inputs[0].SemanticName == "TEXCOORD" && parsed.Inputs[0].SemanticIndex == 3u);
	SWIM_CHECK(parsed.Inputs[1].Location == 1u && parsed.Inputs[1].SemanticName == "POSITION" && parsed.Inputs[1].SemanticIndex == 0u);
	SWIM_CHECK(parsed.Inputs[2].Location == 2u && parsed.Inputs[2].SemanticName == "COLOR" && parsed.Inputs[2].SemanticIndex == 1u);
}

SWIM_TEST("ShaderCompiler.NativeBindings", "VertexInputWithoutSemanticIsAnError")
{
	const auto parsed = ShaderCompiler::ParseNativeVertexInputs(
		R"json({"entryPoints":[{"name":"vertexMain","stage":"vertex",
		"parameters":[{"name":"position","binding":{"kind":"varyingInput","index":0},"type":{"kind":"vector"}}]}]})json");
	SWIM_CHECK(!parsed);
	SWIM_CHECK(parsed.Error.find("position") != std::string::npos);
}

SWIM_TEST("ShaderCompiler.NativeBindings", "DescriptorsMatchRegistersByName")
{
	// Binding-model reflection: two descriptors and a push-constant block.
	const auto bindings = ShaderCompiler::ParseSlangReflectionJson(R"json({"parameters":[
		{"name":"Input","binding":{"kind":"descriptorTableSlot","index":3,"space":1},
			"type":{"kind":"resource","baseShape":"structuredBuffer","resultType":{"kind":"scalar","scalarType":"uint32"}}},
		{"name":"Output","binding":{"kind":"descriptorTableSlot","index":7,"space":1},
			"type":{"kind":"resource","baseShape":"structuredBuffer","access":"readWrite","resultType":{"kind":"scalar","scalarType":"uint32"}}},
		{"name":"Constants","binding":{"kind":"pushConstantBuffer","index":0},
			"type":{"kind":"constantBuffer","elementType":{"kind":"struct","name":"C","fields":[]}}}],"entryPoints":[{"name":"computeMain","stage":"compute","parameters":[]}]})json");
	// Register reflection of the same program: per-class registers, a reserved
	// extra.
	const auto registers = ShaderCompiler::ParseSlangReflectionJson(R"json({"parameters":[
		{"name":"Input","binding":{"kind":"shaderResource","index":0},
			"type":{"kind":"resource","baseShape":"structuredBuffer","resultType":{"kind":"scalar","scalarType":"uint32"}}},
		{"name":"Output","binding":{"kind":"unorderedAccess","index":0},
			"type":{"kind":"resource","baseShape":"structuredBuffer","access":"readWrite","resultType":{"kind":"scalar","scalarType":"uint32"}}},
		{"name":"Constants","binding":{"kind":"constantBuffer","index":0},
			"type":{"kind":"constantBuffer","elementType":{"kind":"struct","name":"C","fields":[]}}},
		{"name":"DrawBase","binding":{"kind":"constantBuffer","index":0,"space":1000},
			"type":{"kind":"constantBuffer","elementType":{"kind":"struct","name":"D","fields":[]}}}],"entryPoints":[{"name":"computeMain","stage":"compute","parameters":[]}]})json");
	SWIM_REQUIRE_MESSAGE(bindings, bindings.Error);
	SWIM_REQUIRE_MESSAGE(registers, registers.Error);
	const auto natives = ShaderCompiler::BuildNativeBindings(bindings.Reflection, registers.Reflection);
	SWIM_REQUIRE_MESSAGE(natives, natives.Error);
	SWIM_REQUIRE_EQUAL(natives.Bindings.size(), std::size_t(3));
	const auto find = [&](std::uint32_t space, std::uint32_t binding) -> const Rhi::NativeBinding*
	{
		for (const auto& native : natives.Bindings)
		{
			if (native.Space == space && native.Binding == binding)
			{
				return &native;
			}
		}

		return nullptr;
	};
	const auto* input = find(1, 3);
	const auto* output = find(1, 7);
	const auto* push = find(Rhi::NativeBinding::PushConstantSpace, 0);
	SWIM_REQUIRE(input && output && push);
	SWIM_CHECK(input->Class == Rhi::NativeRegisterClass::ShaderResource && input->Register == 0u && input->StructureStride == 4u);
	SWIM_CHECK(output->Class == Rhi::NativeRegisterClass::UnorderedAccess && output->Register == 0u);
	SWIM_CHECK(push->Class == Rhi::NativeRegisterClass::ConstantBuffer);
}

SWIM_TEST("ShaderCompiler.NativeBindings", "EntryPointRegistersFollowTheGlobalRegisters")
{
	const auto bindings = ShaderCompiler::ParseSlangReflectionJson(R"json({"parameters":[
		{"name":"Dispatch","binding":{"kind":"pushConstantBuffer","index":0},
			"type":{"kind":"constantBuffer","elementType":{"kind":"struct","name":"C","fields":[]}}},
		{"name":"Inputs","binding":{"kind":"descriptorTableSlot","index":2},
			"type":{"kind":"resource","baseShape":"structuredBuffer","resultType":{"kind":"scalar","scalarType":"uint32"}}}],
		"entryPoints":[{"name":"computeMain","stage":"compute","parameters":[
			{"name":"Output","binding":{"kind":"descriptorTableSlot","index":5,"space":1},
				"type":{"kind":"resource","baseShape":"structuredBuffer","access":"readWrite","resultType":{"kind":"scalar","scalarType":"uint32"}}},
			{"name":"Parameters","binding":{"kind":"descriptorTableSlot","index":9,"space":1},
				"type":{"kind":"constantBuffer","elementType":{"kind":"struct","name":"C","fields":[]}}}]}]})json");
	// Entry-point registers are relative: in the compiled stage they follow the
	// globals' b0 and t0..t2.
	const auto registers = ShaderCompiler::ParseSlangReflectionJson(R"json({"parameters":[
		{"name":"Dispatch","binding":{"kind":"constantBuffer","index":0},
			"type":{"kind":"constantBuffer","elementType":{"kind":"struct","name":"C","fields":[]}}},
		{"name":"Inputs","binding":{"kind":"shaderResource","index":0,"count":3},
			"type":{"kind":"resource","baseShape":"structuredBuffer","resultType":{"kind":"scalar","scalarType":"uint32"}}}],
		"entryPoints":[{"name":"computeMain","stage":"compute","parameters":[
			{"name":"Output","binding":{"kind":"unorderedAccess","index":0},
				"type":{"kind":"resource","baseShape":"structuredBuffer","access":"readWrite","resultType":{"kind":"scalar","scalarType":"uint32"}}},
			{"name":"Parameters","binding":{"kind":"constantBuffer","index":0},
				"type":{"kind":"constantBuffer","elementType":{"kind":"struct","name":"C","fields":[]}}}]}]})json");
	SWIM_REQUIRE_MESSAGE(bindings, bindings.Error);
	SWIM_REQUIRE_MESSAGE(registers, registers.Error);
	const auto natives = ShaderCompiler::BuildNativeBindings(bindings.Reflection, registers.Reflection);
	SWIM_REQUIRE_MESSAGE(natives, natives.Error);
	SWIM_REQUIRE_EQUAL(natives.Bindings.size(), std::size_t(4));

	for (const auto& native : natives.Bindings)
	{
		if (native.Space == 1 && native.Binding == 5)
		{
			SWIM_CHECK(native.Class == Rhi::NativeRegisterClass::UnorderedAccess && native.Register == 0u);
		}
		else if (native.Space == 1 && native.Binding == 9)
		{
			SWIM_CHECK(native.Class == Rhi::NativeRegisterClass::ConstantBuffer && native.Register == 1u);
		}
	}
}

SWIM_TEST("ShaderCompiler.NativeBindings", "SizeQueriesMarkTheirRegisters")
{
	std::vector<Rhi::NativeBinding> natives(3);
	natives[0].Register = 2;
	natives[1].Register = 2;
	natives[1].Class = Rhi::NativeRegisterClass::UnorderedAccess;
	natives[2].Register = 2;
	natives[2].RegisterSpace = 1;
	const auto error = ShaderCompiler::ParseNativeSizeQueries(
		R"json({"nativeSizeQueries":[{"class":"shaderResource","index":2,"space":0}]})json", natives);
	SWIM_REQUIRE_MESSAGE(error.empty(), error);
	SWIM_CHECK(natives[0].SizeQueried);
	SWIM_CHECK(!natives[1].SizeQueried);
	SWIM_CHECK(!natives[2].SizeQueried);
	SWIM_CHECK(ShaderCompiler::ParseNativeSizeQueries(R"json({"parameters":[]})json", natives).empty());
}

SWIM_TEST("ShaderCompiler.NativeBindings", "SizeQueriesComeFromGetDimensionsCalls")
{
	// Records is queried directly; Views through a function parameter of its element type.
	constexpr std::string_view hlsl = R"hlsl(
StructuredBuffer<Record_0 > Records_0 : register(t2);
RWStructuredBuffer<View_0 > Views_0 : register(u1, space3);
StructuredBuffer<uint > Other_0 : register(t4);
uint Count_0(StructuredBuffer<View_0 > views_0, uint index_0)
{
    uint n_0; uint s_0;
    views_0.GetDimensions(n_0, s_0);
    Records_0.GetDimensions(n_0, s_0);
    return n_0;
}
)hlsl";
	SWIM_CHECK_EQUAL(ShaderCompiler::FindNativeSizeQueries(hlsl),
		std::string(R"([{"class":"shaderResource","index":2,"space":0},{"class":"unorderedAccess","index":1,"space":3}])"));
	SWIM_CHECK(ShaderCompiler::FindNativeSizeQueries("StructuredBuffer<uint > A_0 : register(t0);").empty());
}
