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
		{ "LocalCenter", { std::uint32_t(offsetof(GpuInstanceRecord, LocalCenter)), 12u } },
		{ "MeshIndex", { std::uint32_t(offsetof(GpuInstanceRecord, MeshIndex)), 4u } },
		{ "LocalExtents", { std::uint32_t(offsetof(GpuInstanceRecord, LocalExtents)), 12u } },
		{ "MeshGeneration", { std::uint32_t(offsetof(GpuInstanceRecord, MeshGeneration)), 4u } },
		{ "TransformIndex", { std::uint32_t(offsetof(GpuInstanceRecord, TransformIndex)), 4u } },
		{ "MaterialSet", { std::uint32_t(offsetof(GpuInstanceRecord, MaterialSet)), 4u } },
		{ "ObjectId", { std::uint32_t(offsetof(GpuInstanceRecord, ObjectId)), 4u } },
		{ "Flags", { std::uint32_t(offsetof(GpuInstanceRecord, Flags)), 4u } },
		{ "SkinIndex", { std::uint32_t(offsetof(GpuInstanceRecord, SkinIndex)), 4u } },
		{ "LodBias", { std::uint32_t(offsetof(GpuInstanceRecord, LodBias)), 4u } },
		{ "Generation", { std::uint32_t(offsetof(GpuInstanceRecord, Generation)), 4u } },
		{ "Reserved", { std::uint32_t(offsetof(GpuInstanceRecord, Reserved)), 4u } },
	};
	SWIM_CHECK(Fields(*instances) == instanceLayout);
	const std::map<std::string, std::pair<std::uint32_t, std::uint32_t>> transformLayout{
		{ "Current", { std::uint32_t(offsetof(GpuTransformRecord, Current)), 48u } },
		{ "Previous", { std::uint32_t(offsetof(GpuTransformRecord, Previous)), 48u } },
	};
	SWIM_CHECK(Fields(*transforms) == transformLayout);
}
#endif

#ifdef SWIM_GPU_VISIBILITY_REFLECTION_PATH
#include "Engine/Systems/Renderer/Geometry/GpuMeshMetadata.h"
#include "Engine/Systems/Renderer/Visibility/GpuDrawRecord.h"
#include "Engine/Systems/Renderer/Visibility/GpuLodState.h"
#include "Engine/Systems/Renderer/Visibility/GpuViewRecord.h"
#include "Engine/Systems/Renderer/Visibility/GpuVisibilityDesc.h"
#include "Engine/Systems/Renderer/Visibility/VisibilityBinRange.h"

// The culling program's bindings, element strides and record fields must match
// GpuVisibilityBindings and the C++ records GpuVisibility uploads and reads.
SWIM_TEST("ShaderCompiler.GpuSceneLayout", "VisibilityProgramMatchesItsCppContract")
{
	using namespace Render;
	using B = GpuVisibilityBindings;
	const auto parsed = ShaderCompiler::LoadSlangReflectionJson(SWIM_GPU_VISIBILITY_REFLECTION_PATH);
	SWIM_REQUIRE_MESSAGE(parsed, parsed.Error);

	struct Expected
	{
		const char* Name;
		std::uint32_t Binding;
		std::uint32_t ElementSize;
	};

	const Expected expected[] = {
		{ "Instances", B::Instances, sizeof(GpuInstanceRecord) },
		{ "Transforms", B::Transforms, sizeof(GpuTransformRecord) },
		{ "Meshes", B::Meshes, sizeof(GpuMeshMetadata) },
		{ "Submeshes", B::Submeshes, sizeof(GpuSubmeshRecord) },
		{ "Views", B::View, sizeof(GpuViewRecord) },
		{ "MaterialBins", B::MaterialBins, 0 },
		{ "BinRanges", B::BinRanges, sizeof(VisibilityBinRange) },
		{ "IndexPages", B::IndexPages, 0 },
		{ "LodStates", B::LodState, sizeof(GpuLodState) },
		{ "Commands", B::Commands, sizeof(Rhi::DrawIndexedIndirectCommand) },
		{ "DrawRecords", B::DrawRecords, sizeof(GpuDrawRecord) },
		{ "Counts", B::Counts, 0 },
		{ "Stats", B::Stats, 0 },
		{ "OcclusionHistory", B::OcclusionHistory, 0 },
	};
	for (const auto& item : expected)
	{
		const auto* parameter = FindParameter(parsed.Reflection, item.Name);
		SWIM_REQUIRE_MESSAGE(parameter != nullptr, item.Name);
		SWIM_CHECK_EQUAL(parameter->Index, item.Binding);
		SWIM_CHECK_EQUAL(parameter->Space, 0u);
		SWIM_CHECK_EQUAL(parameter->ElementSize, item.ElementSize); // Zero: scalar elements.
	}

	const auto view = Fields(*FindParameter(parsed.Reflection, "Views"));
	SWIM_CHECK(view.at("ViewProjection") == std::make_pair(std::uint32_t(offsetof(GpuViewRecord, ViewProjection)), 64u));
	SWIM_CHECK(view.at("FrustumPlanes") == std::make_pair(std::uint32_t(offsetof(GpuViewRecord, FrustumPlanes)), 96u));
	SWIM_CHECK(view.at("CameraPosition").first == offsetof(GpuViewRecord, CameraPosition));
	SWIM_CHECK(view.at("LodScale").first == offsetof(GpuViewRecord, LodScale));
	SWIM_CHECK(view.at("LodPixelError").first == offsetof(GpuViewRecord, LodPixelError));
	SWIM_CHECK(view.at("LodHysteresis").first == offsetof(GpuViewRecord, LodHysteresis));
	SWIM_CHECK(view.at("Flags").first == offsetof(GpuViewRecord, Flags));

	const auto mesh = Fields(*FindParameter(parsed.Reflection, "Meshes"));
	SWIM_CHECK(mesh.at("IndexPage").first == offsetof(GpuMeshMetadata, IndexPage));
	SWIM_CHECK(mesh.at("LodCount").first == offsetof(GpuMeshMetadata, LodCount));
	SWIM_CHECK(mesh.at("Generation").first == offsetof(GpuMeshMetadata, Generation));
	SWIM_CHECK(mesh.at("FirstSubmesh").first == offsetof(GpuMeshMetadata, FirstSubmesh));
	SWIM_CHECK(mesh.at("SubmeshCount").first == offsetof(GpuMeshMetadata, SubmeshCount));
	SWIM_CHECK(mesh.at("Lods") == std::make_pair(std::uint32_t(offsetof(GpuMeshMetadata, Lods)), std::uint32_t(sizeof(GpuMeshLod) * 8)));
	const auto submesh = Fields(*FindParameter(parsed.Reflection, "Submeshes"));
	SWIM_CHECK(submesh.at("VertexOffset").first == offsetof(GpuSubmeshRecord, VertexOffset));
	SWIM_CHECK(submesh.at("MaterialSlot").first == offsetof(GpuSubmeshRecord, MaterialSlot));
	const auto command = Fields(*FindParameter(parsed.Reflection, "Commands"));
	SWIM_CHECK(command.at("VertexOffset").first == offsetof(Rhi::DrawIndexedIndirectCommand, VertexOffset));
	SWIM_CHECK(command.at("FirstInstance").first == offsetof(Rhi::DrawIndexedIndirectCommand, FirstInstance));

	const auto converted = ShaderCompiler::BuildRhiShaderInterface(parsed.Reflection);
	SWIM_REQUIRE_MESSAGE(converted, converted.Error);
	SWIM_CHECK((converted.Interface.ComputeThreadGroupSize == std::array<std::uint32_t, 3>{ B::ThreadGroupSize, 1, 1 }));
	SWIM_REQUIRE_EQUAL(converted.Interface.PushConstants.size(), 1u);
	SWIM_CHECK_EQUAL(converted.Interface.PushConstants[0].Size, B::PushConstantBytes);
	const auto* hzb = FindParameter(parsed.Reflection, "Hzb");
	SWIM_REQUIRE(hzb != nullptr);
	SWIM_CHECK_EQUAL(hzb->Index, B::Hzb);
	SWIM_REQUIRE_EQUAL(converted.Interface.DescriptorSchemas.size(), 1u);
	std::uint32_t found = 0;
	for (const auto& binding : converted.Interface.DescriptorSchemas[0].Bindings)
	{
		found += binding.Binding == B::Hzb && binding.Type == Rhi::DescriptorType::SampledTexture;
		found += binding.Binding == B::OcclusionHistory && binding.Type == Rhi::DescriptorType::StorageBuffer;
	}
	SWIM_CHECK_EQUAL(found, 2u);
	SWIM_CHECK_EQUAL(converted.Interface.DescriptorSchemas[0].Bindings.size(), std::size_t(B::Count));
}
#endif

#ifdef SWIM_HZB_REDUCE_REFLECTION_PATH
#include "Engine/Systems/Renderer/Visibility/HzbBuilder.h"

// The HZB reduction program's bindings, thread group and push constants must match HzbBindings.
SWIM_TEST("ShaderCompiler.GpuSceneLayout", "HzbReduceProgramMatchesItsCppContract")
{
	using B = Render::HzbBindings;
	const auto parsed = ShaderCompiler::LoadSlangReflectionJson(SWIM_HZB_REDUCE_REFLECTION_PATH);
	SWIM_REQUIRE_MESSAGE(parsed, parsed.Error);
	const auto converted = ShaderCompiler::BuildRhiShaderInterface(parsed.Reflection);
	SWIM_REQUIRE_MESSAGE(converted, converted.Error);
	SWIM_CHECK((converted.Interface.ComputeThreadGroupSize == std::array<std::uint32_t, 3>{ B::ThreadGroupSize, B::ThreadGroupSize, 1 }));
	SWIM_REQUIRE_EQUAL(converted.Interface.PushConstants.size(), 1u);
	SWIM_CHECK_EQUAL(converted.Interface.PushConstants[0].Size, B::PushConstantBytes);
	SWIM_REQUIRE_EQUAL(converted.Interface.DescriptorSchemas.size(), 1u);
	const auto& bindings = converted.Interface.DescriptorSchemas[0].Bindings;
	SWIM_REQUIRE_EQUAL(bindings.size(), 2u);
	for (const auto& binding : bindings)
	{
		if (binding.Binding == B::Source)
		{
			SWIM_CHECK(binding.Type == Rhi::DescriptorType::SampledTexture);
		}
		else
		{
			SWIM_CHECK_EQUAL(binding.Binding, B::Destination);
			SWIM_CHECK(binding.Type == Rhi::DescriptorType::StorageTexture);
			SWIM_CHECK(binding.StorageTextureFormat == Rhi::Format::R32Float);
		}
	}
}
#endif
