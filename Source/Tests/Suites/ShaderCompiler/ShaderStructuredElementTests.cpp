#include "Engine/Systems/Renderer/GpuScene/GpuInstanceRecord.h"
#include "Engine/Systems/Renderer/GpuScene/GpuTransformRecord.h"
#include "Tools/ShaderCompiler/ShaderRhiInterface.h"
#include "Tests/Framework/Test.h"

#include <cstddef>
#include <map>
#include <string>

using namespace Swim;

namespace
{
	const ShaderCompiler::ShaderBindingReflection* FindParameter(const ShaderCompiler::ShaderReflection& reflection, std::string_view name)
	{
		for (const auto& parameter : reflection.GlobalParameters)
		{
			if (parameter.Name == name)
			{
				return &parameter;
			}
		}
		return nullptr;
	}

	std::map<std::string, std::pair<std::uint32_t, std::uint32_t>> Fields(const ShaderCompiler::ShaderBindingReflection& parameter)
	{
		std::map<std::string, std::pair<std::uint32_t, std::uint32_t>> fields;
		for (const auto& field : parameter.ElementFields)
		{
			fields[field.Name] = { field.Offset, field.Size };
		}
		return fields;
	}
} // namespace

SWIM_TEST("ShaderCompiler.StructuredElements", "StructElementsReportFlattenedFieldOffsetsAndStride")
{
	const auto parsed = ShaderCompiler::ParseSlangReflectionJson(R"json({"parameters":[{"name":"Items",
		"binding":{"kind":"descriptorTableSlot","index":0},
		"type":{"kind":"resource","baseShape":"structuredBuffer","resultType":{"kind":"struct","name":"Item","fields":[
			{"name":"Position","type":{"kind":"vector","elementCount":3,"elementType":{"kind":"scalar","scalarType":"float32"}},"binding":{"kind":"uniform","offset":0,"size":12}},
			{"name":"Id","type":{"kind":"scalar","scalarType":"uint32"},"binding":{"kind":"uniform","offset":12,"size":4}},
			{"name":"Inner","type":{"kind":"struct","name":"Inner","fields":[
				{"name":"Rows","type":{"kind":"array","elementCount":2,"elementType":{"kind":"vector","elementCount":4,"elementType":{"kind":"scalar","scalarType":"float32"}}},"binding":{"kind":"uniform","offset":0,"size":32}}]},
				"binding":{"kind":"uniform","offset":16,"size":32}}],
			"sizes":[{"kind":"uniform","value":48,"alignment":16}]}}},
		{"name":"Words","binding":{"kind":"descriptorTableSlot","index":1},
		"type":{"kind":"resource","baseShape":"structuredBuffer","resultType":{"kind":"scalar","scalarType":"uint32"}}}],
		"entryPoints":[{"name":"main","stage":"compute","threadGroupSize":[64,1,1]}]})json");
	SWIM_REQUIRE_MESSAGE(parsed, parsed.Error);
	const auto* items = FindParameter(parsed.Reflection, "Items");
	SWIM_REQUIRE(items != nullptr);
	SWIM_CHECK_EQUAL(items->ElementSize, 48u);
	const auto fields = Fields(*items);
	SWIM_REQUIRE_EQUAL(fields.size(), 3u);
	SWIM_CHECK(fields.at("Id") == std::make_pair(12u, 4u));
	SWIM_CHECK(fields.at("Inner.Rows") == std::make_pair(16u, 32u)); // Nested offsets are absolute.
	// Non-struct elements carry no layout; the binding still converts.
	const auto* words = FindParameter(parsed.Reflection, "Words");
	SWIM_REQUIRE(words != nullptr);
	SWIM_CHECK(words->ElementFields.empty());
	SWIM_CHECK(ShaderCompiler::BuildRhiShaderInterface(parsed.Reflection));
}

#ifdef SWIM_RHI_GPU_SCENE_PROBE_REFLECTION_PATH
// The C++ GPU Scene records and Shaders/Slang/GpuScene/GpuSceneRecords.slang
// must agree byte for byte; this reads the compiled shader's reflection.
SWIM_TEST("ShaderCompiler.GpuSceneLayout", "SlangRecordsMatchTheCppRecords")
{
	using Render::GpuInstanceRecord;
	using Render::GpuTransformRecord;
	const auto parsed = ShaderCompiler::LoadSlangReflectionJson(SWIM_RHI_GPU_SCENE_PROBE_REFLECTION_PATH);
	SWIM_REQUIRE_MESSAGE(parsed, parsed.Error);
	const auto* instances = FindParameter(parsed.Reflection, "Instances");
	const auto* transforms = FindParameter(parsed.Reflection, "Transforms");
	SWIM_REQUIRE(instances != nullptr && transforms != nullptr);
	SWIM_CHECK_EQUAL(instances->ElementSize, std::uint32_t(sizeof(GpuInstanceRecord)));
	SWIM_CHECK_EQUAL(transforms->ElementSize, std::uint32_t(sizeof(GpuTransformRecord)));

	const std::map<std::string, std::pair<std::uint32_t, std::uint32_t>> instanceLayout{
		{ "LocalCenter", { offsetof(GpuInstanceRecord, LocalCenter), 12 } },
		{ "MeshIndex", { offsetof(GpuInstanceRecord, MeshIndex), 4 } },
		{ "LocalExtents", { offsetof(GpuInstanceRecord, LocalExtents), 12 } },
		{ "MeshGeneration", { offsetof(GpuInstanceRecord, MeshGeneration), 4 } },
		{ "TransformIndex", { offsetof(GpuInstanceRecord, TransformIndex), 4 } },
		{ "MaterialSet", { offsetof(GpuInstanceRecord, MaterialSet), 4 } },
		{ "ObjectId", { offsetof(GpuInstanceRecord, ObjectId), 4 } },
		{ "Flags", { offsetof(GpuInstanceRecord, Flags), 4 } },
		{ "SkinIndex", { offsetof(GpuInstanceRecord, SkinIndex), 4 } },
		{ "LodBias", { offsetof(GpuInstanceRecord, LodBias), 4 } },
		{ "Generation", { offsetof(GpuInstanceRecord, Generation), 4 } },
		{ "Reserved", { offsetof(GpuInstanceRecord, Reserved), 4 } },
	};
	SWIM_CHECK(Fields(*instances) == instanceLayout);
	const std::map<std::string, std::pair<std::uint32_t, std::uint32_t>> transformLayout{
		{ "Current", { offsetof(GpuTransformRecord, Current), 48 } },
		{ "Previous", { offsetof(GpuTransformRecord, Previous), 48 } },
	};
	SWIM_CHECK(Fields(*transforms) == transformLayout);
}
#endif
