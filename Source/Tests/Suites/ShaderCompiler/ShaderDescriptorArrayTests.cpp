#include "Tools/ShaderCompiler/ShaderRhiInterface.h"
#include "Tests/Framework/Test.h"

using namespace Swim;

namespace
{

	ShaderCompiler::ShaderReflectionResult ParseArray(std::string_view element, std::string_view count = "2",
		std::string_view binding = R"json({"kind":"descriptorTableSlot","space":1,"index":7})json", bool scoped = false,
		std::string_view format = "")
	{
		const auto parameter = std::string(R"json({"name":"Resources","format":")json") + std::string(format) + R"json(","binding":)json" +
			std::string(binding) + R"json(,"type":{"kind":"array","elementCount":)json" + std::string(count) +
			R"json(,"elementType":)json" + std::string(element) + "}}";
		const auto globals = scoped ? "[]" : "[" + parameter + "]";
		const auto locals = scoped ? "[" + parameter + "]" : "[]";
		return ShaderCompiler::ParseSlangReflectionJson(std::string(R"json({"parameters":)json") + globals +
			R"json(,"entryPoints":[{"name":"main","stage":"compute","threadGroupSize":[8,1,1],"parameters":)json" + locals + "}]}");
	}

	void Reject(const ShaderCompiler::ShaderReflection& reflection)
	{
		const auto result = ShaderCompiler::BuildRhiShaderInterface(reflection);
		SWIM_REQUIRE(!result);
		SWIM_CHECK(result.Interface.DescriptorSchemas.empty());
		SWIM_CHECK(result.Interface.PushConstants.empty());
		SWIM_CHECK((result.Interface.ComputeThreadGroupSize == std::array<std::uint32_t, 3>{}));
	}

} // namespace

SWIM_TEST("ShaderCompiler.DescriptorArrays", "ElementCountsAndTypesConvertForGlobalsAndEntryParameters")
{
	struct Case
	{
		std::string_view Json;
		Rhi::DescriptorType Type;
		std::string_view Format{};
	};
	const std::array cases{
		Case{ R"json({"kind":"samplerState"})json", Rhi::DescriptorType::Sampler },
		Case{ R"json({"kind":"constantBuffer"})json", Rhi::DescriptorType::UniformBuffer },
		Case{ R"json({"kind":"resource","baseShape":"structuredBuffer"})json", Rhi::DescriptorType::ReadOnlyStorageBuffer },
		Case{ R"json({"kind":"resource","baseShape":"byteAddressBuffer","access":"readWrite"})json", Rhi::DescriptorType::StorageBuffer },
		Case{ R"json({"kind":"resource","baseShape":"texture2D","resultType":{"kind":"vector","elementCount":4,"elementType":{"kind":"scalar","scalarType":"float32"}}})json", Rhi::DescriptorType::SampledTexture },
		Case{ R"json({"kind":"resource","baseShape":"texture2D","access":"readWrite","resultType":{"kind":"scalar","scalarType":"uint32"}})json", Rhi::DescriptorType::StorageTexture, "r32ui" }
	};
	for (const auto& item : cases)
	{
		for (const bool scoped : { false, true })
		{
			const auto parsed = ParseArray(item.Json, "3", R"json({"kind":"descriptorTableSlot","space":1,"index":7})json", scoped, item.Format);
			SWIM_REQUIRE(parsed);
			const auto& parameter = scoped ? parsed.Reflection.EntryPoints[0].Parameters[0] : parsed.Reflection.GlobalParameters[0];
			SWIM_CHECK_EQUAL(parameter.TypeKind, std::string("array"));
			SWIM_CHECK_EQUAL(parameter.Count, 1u);
			SWIM_CHECK_EQUAL(parameter.DescriptorArrayCount, 3u);
			const auto result = ShaderCompiler::BuildRhiShaderInterface(parsed.Reflection);
			SWIM_REQUIRE_MESSAGE(result, result.Error);
			SWIM_REQUIRE_EQUAL(result.Interface.DescriptorSchemas.size(), 1u);
			SWIM_CHECK_EQUAL(result.Interface.DescriptorSchemas[0].Space, 1u);
			const auto& binding = result.Interface.DescriptorSchemas[0].Bindings[0];
			SWIM_CHECK_EQUAL(binding.Binding, 7u);
			SWIM_CHECK_EQUAL(binding.Count, 3u);
			SWIM_CHECK_EQUAL(binding.Type, item.Type);
			SWIM_CHECK_EQUAL(binding.Stages, Rhi::ShaderStageMask::Compute);
			SWIM_CHECK(!binding.VariableCount && !binding.PartiallyBound);
			SWIM_CHECK_EQUAL(binding.StorageTextureFormat, item.Type == Rhi::DescriptorType::StorageTexture ? Rhi::Format::R32Uint : Rhi::Format::Undefined);
		}
	}
}

SWIM_TEST("ShaderCompiler.DescriptorArrays", "RuntimeZeroMalformedAndOverflowCountsReject")
{
	for (const auto count : { "0", "-1", "4294967296", "\"unbounded\"", "null", "1.5", "true" })
	{
		const auto parsed = ParseArray(R"json({"kind":"samplerState"})json", count);
		SWIM_REQUIRE(parsed);
		Reject(parsed.Reflection);
	}
	const auto missing = ShaderCompiler::ParseSlangReflectionJson(R"json({"parameters":[{"name":"Unbounded","binding":{"kind":"descriptorTableSlot","index":0},"type":{"kind":"array","elementType":{"kind":"samplerState"}}}],"entryPoints":[{"stage":"fragment"}]})json");
	SWIM_REQUIRE(missing);
	Reject(missing.Reflection);
	for (auto count : { "1", "4294967295" })
	{
		const auto parsed = ParseArray(R"json({"kind":"samplerState"})json", count);
		SWIM_REQUIRE(parsed);
		const auto result = ShaderCompiler::BuildRhiShaderInterface(parsed.Reflection);
		SWIM_REQUIRE(result);
		SWIM_CHECK_EQUAL(result.Interface.DescriptorSchemas[0].Bindings[0].Count, count[0] == '1' ? 1u : UINT32_MAX);
	}
}

SWIM_TEST("ShaderCompiler.DescriptorArrays", "OneBindingSlotIsDistinctFromDescriptorElementCount")
{
	for (const auto binding : {
		R"json({"kind":"descriptorTableSlot","index":0,"count":2})json",
		R"json({"kind":"descriptorTableSlot","index":0,"count":0})json",
		R"json({"kind":"descriptorTableSlot","index":0,"count":"1"})json",
		R"json({"kind":"uniform","offset":0,"size":8})json" })
	{
		const auto parsed = ParseArray(R"json({"kind":"samplerState"})json", "2", binding);
		SWIM_REQUIRE(parsed);
		Reject(parsed.Reflection);
	}
	const auto parsed = ParseArray(R"json({"kind":"samplerState"})json", "2", R"json({"kind":"descriptorTableSlot","index":7,"count":1})json");
	SWIM_REQUIRE(parsed);
	auto reflection = parsed.Reflection;
	auto adjacent = reflection.GlobalParameters[0];
	adjacent.Index = 8;
	reflection.GlobalParameters.push_back(adjacent);
	const auto result = ShaderCompiler::BuildRhiShaderInterface(reflection);
	SWIM_REQUIRE(result);
	SWIM_CHECK_EQUAL(result.Interface.DescriptorSchemas[0].Bindings.size(), 2u);
	reflection.GlobalParameters[1].Index = 7;
	Reject(reflection);
}

SWIM_TEST("ShaderCompiler.DescriptorArrays", "NestedArraysValuesBlocksAndUnsupportedImageShapesReject")
{
	for (const auto element : {
		R"json({"kind":"array","elementCount":2,"elementType":{"kind":"samplerState"}})json",
		R"json({"kind":"scalar","scalarType":"uint32"})json",
		R"json({"kind":"struct"})json", R"json({"kind":"parameterBlock"})json", "null",
		R"json({"kind":"resource","baseShape":"texture3D","array":true,"resultType":{"kind":"scalar","scalarType":"float32"}})json",
		R"json({"kind":"resource","baseShape":"texture2D","multisample":true,"resultType":{"kind":"scalar","scalarType":"float32"}})json" })
	{
		const auto parsed = ParseArray(element);
		SWIM_REQUIRE(parsed);
		Reject(parsed.Reflection);
	}
}

SWIM_TEST("ShaderCompiler.DescriptorArrays", "ArrayMetadataDoesNotBypassStorageFormatOrStageRules")
{
	const auto parsed = ParseArray(R"json({"kind":"resource","baseShape":"texture2D","access":"readWrite","resultType":{"kind":"scalar","scalarType":"uint32"}})json",
		"2", R"json({"kind":"descriptorTableSlot","space":1,"index":7})json", false, "r32ui");
	SWIM_REQUIRE(parsed);
	auto reflection = parsed.Reflection;
	SWIM_REQUIRE(ShaderCompiler::BuildRhiShaderInterface(reflection));
	reflection.GlobalParameters[0].ResourceFormat.clear();
	Reject(reflection);
	reflection = parsed.Reflection;
	reflection.GlobalParameters[0].ResourceScalarType = "int32";
	Reject(reflection);
	reflection = parsed.Reflection;
	reflection.EntryPoints[0].Stage = ShaderCompiler::ShaderStage::Fragment;
	Reject(reflection);
}

SWIM_TEST("ShaderCompiler.DescriptorArrays", "ArrayDataInsideBufferDoesNotBecomeADescriptorArray")
{
	const auto parsed = ShaderCompiler::ParseSlangReflectionJson(R"json({
		"parameters":[{"name":"Data","binding":{"kind":"descriptorTableSlot","index":2},
		"type":{"kind":"resource","baseShape":"structuredBuffer","resultType":{"kind":"array","elementCount":2,"elementType":{"kind":"scalar","scalarType":"uint32"}}}}],
		"entryPoints":[{"name":"main","stage":"fragment"}]
	})json");
	SWIM_REQUIRE(parsed);
	SWIM_CHECK_EQUAL(parsed.Reflection.GlobalParameters[0].DescriptorArrayCount, 0u);
	const auto result = ShaderCompiler::BuildRhiShaderInterface(parsed.Reflection);
	SWIM_REQUIRE_MESSAGE(result, result.Error);
	SWIM_CHECK_EQUAL(result.Interface.DescriptorSchemas[0].Bindings[0].Count, 1u);
	SWIM_CHECK_EQUAL(result.Interface.DescriptorSchemas[0].Bindings[0].Type, Rhi::DescriptorType::ReadOnlyStorageBuffer);
}

#ifdef SWIM_RHI_DESCRIPTOR_ARRAYS_REFLECTION_PATH
SWIM_TEST("ShaderCompiler.DescriptorArrays", "PinnedSlangReflectsSixArrayClassesAcrossTwoSpaces")
{
	const auto parsed = ShaderCompiler::LoadSlangReflectionJson(SWIM_RHI_DESCRIPTOR_ARRAYS_REFLECTION_PATH);
	SWIM_REQUIRE_MESSAGE(parsed, parsed.Error);
	const auto result = ShaderCompiler::BuildRhiShaderInterface(parsed.Reflection);
	SWIM_REQUIRE_MESSAGE(result, result.Error);
	SWIM_REQUIRE_EQUAL(result.Interface.DescriptorSchemas.size(), 2u);
	SWIM_REQUIRE_EQUAL(result.Interface.PushConstants.size(), 1u);
	SWIM_CHECK_EQUAL(result.Interface.PushConstants[0].Size, 16u);
	std::uint32_t total = 0;
	for (const auto& schema : result.Interface.DescriptorSchemas)
	{
		for (const auto& binding : schema.Bindings)
		{
			SWIM_CHECK_EQUAL(binding.Count, 2u);
			SWIM_CHECK_EQUAL(binding.Stages, Rhi::ShaderStageMask::Compute);
			total += binding.Count;
		}
	}
	SWIM_CHECK_EQUAL(total, 12u);
	const auto& storage = result.Interface.DescriptorSchemas[1].Bindings;
	SWIM_REQUIRE_EQUAL(storage.size(), 2u);
	SWIM_CHECK_EQUAL(storage[0].Binding, 7u);
	SWIM_CHECK_EQUAL(storage[0].StorageTextureFormat, Rhi::Format::R32Uint);
	SWIM_CHECK_EQUAL(storage[1].Binding, 9u);
	SWIM_CHECK_EQUAL(storage[1].Type, Rhi::DescriptorType::StorageBuffer);
}
#endif
