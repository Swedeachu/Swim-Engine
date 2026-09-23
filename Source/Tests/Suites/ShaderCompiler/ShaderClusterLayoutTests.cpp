#include "Tests/Framework/Test.h"

#if defined(SWIM_CLUSTER_LIGHT_CULL_REFLECTION_PATH) && defined(SWIM_CLUSTER_BOUNDS_REFLECTION_PATH) &&                                    \
	defined(SWIM_CLUSTER_ASSIGN_REFLECTION_PATH) && defined(SWIM_CLUSTER_SCAN_REFLECTION_PATH) &&                                          \
	defined(SWIM_CLUSTER_HEATMAP_REFLECTION_PATH) && defined(SWIM_RHI_CLUSTERED_LIGHT_PROBE_REFLECTION_PATH)
#include "Engine/Systems/Renderer/ClusteredLighting/ClusterBindings.h"
#include "Engine/Systems/Renderer/ClusteredLighting/ClusterGrid.h"
#include "Tools/ShaderCompiler/ShaderRhiInterface.h"

#include <cstddef>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>

using namespace Swim;

namespace
{
	struct Program
	{
		ShaderCompiler::ShaderReflection Reflection;
		Rhi::ShaderProgramInterface Interface;
	};

	Program Load(const char* path)
	{
		auto parsed = ShaderCompiler::LoadSlangReflectionJson(path);
		SWIM_REQUIRE_MESSAGE(parsed, parsed.Error);
		auto converted = ShaderCompiler::BuildRhiShaderInterface(parsed.Reflection);
		SWIM_REQUIRE_MESSAGE(converted, converted.Error);
		return { std::move(parsed.Reflection), std::move(converted.Interface) };
	}

	const ShaderCompiler::ShaderBindingReflection& Parameter(const Program& program, std::string_view name)
	{
		for (const auto& parameter : program.Reflection.GlobalParameters)
		{
			if (parameter.Name == name)
			{
				return parameter;
			}
		}
		SWIM_FAIL("missing shader parameter " + std::string(name));
		throw std::logic_error("unreachable");
	}

	std::map<std::string, std::uint32_t> Offsets(const ShaderCompiler::ShaderBindingReflection& parameter)
	{
		std::map<std::string, std::uint32_t> offsets;
		for (const auto& field : parameter.ElementFields)
		{
			offsets[field.Name] = field.Offset;
		}
		return offsets;
	}

	void CheckBindings(const Program& program, std::uint32_t group, std::uint32_t pushBytes, std::size_t bindingCount)
	{
		SWIM_CHECK((program.Interface.ComputeThreadGroupSize ==
			std::array<std::uint32_t, 3>{ group, group == Render::ClusterHeatmapBindings::ThreadGroupSize ? group : 1u, 1 }));
		SWIM_CHECK_EQUAL(program.Interface.PushConstants.size(), std::size_t(pushBytes ? 1 : 0));
		if (pushBytes)
		{
			SWIM_CHECK_EQUAL(program.Interface.PushConstants[0].Size, pushBytes);
		}
		SWIM_REQUIRE_EQUAL(program.Interface.DescriptorSchemas.size(), 1u);
		SWIM_CHECK_EQUAL(program.Interface.DescriptorSchemas[0].Bindings.size(), bindingCount);
	}
} // namespace

// The clustering records in ClusterGrid.slang equal the C++ ones, and every program's
// thread group, push constants and binding count match ClusterBindings.h.
SWIM_TEST("ShaderCompiler.ClusterLayout", "ClusterRecordsAndProgramsMatchTheirCppContracts")
{
	const auto assign = Load(SWIM_CLUSTER_ASSIGN_REFLECTION_PATH);
	const auto& grid = Parameter(assign, "Grid");
	SWIM_CHECK_EQUAL(grid.ElementSize, std::uint32_t(sizeof(Render::ClusterGridRecord)));
	const auto gridOffsets = Offsets(grid);
	SWIM_CHECK_EQUAL(gridOffsets.at("ViewRow0"), std::uint32_t(offsetof(Render::ClusterGridRecord, ViewRows)));
	SWIM_CHECK_EQUAL(gridOffsets.at("Projection"), std::uint32_t(offsetof(Render::ClusterGridRecord, Projection)));
	SWIM_CHECK_EQUAL(gridOffsets.at("DepthParams"), std::uint32_t(offsetof(Render::ClusterGridRecord, DepthParams)));
	SWIM_CHECK_EQUAL(gridOffsets.at("SliceParams"), std::uint32_t(offsetof(Render::ClusterGridRecord, SliceParams)));
	SWIM_CHECK_EQUAL(gridOffsets.at("Dimensions"), std::uint32_t(offsetof(Render::ClusterGridRecord, Dimensions)));
	SWIM_CHECK_EQUAL(gridOffsets.at("Limits"), std::uint32_t(offsetof(Render::ClusterGridRecord, Limits)));
	const auto& records = Parameter(assign, "Records");
	SWIM_CHECK_EQUAL(records.ElementSize, std::uint32_t(sizeof(Render::ClusterRecord)));
	const auto recordOffsets = Offsets(records);
	SWIM_CHECK_EQUAL(recordOffsets.at("Count"), std::uint32_t(offsetof(Render::ClusterRecord, Count)));
	SWIM_CHECK_EQUAL(recordOffsets.at("RawCount"), std::uint32_t(offsetof(Render::ClusterRecord, RawCount)));
	SWIM_CHECK_EQUAL(Parameter(assign, "ViewLights").ElementSize, 16u);
	SWIM_CHECK_EQUAL(Parameter(assign, "Bounds").ElementSize, 32u);
	CheckBindings(assign, Render::ClusterAssignBindings::ThreadGroupSize, Render::ClusterAssignBindings::PushConstantBytes, 6);

	const auto scan = Load(SWIM_CLUSTER_SCAN_REFLECTION_PATH);
	const auto& stats = Parameter(scan, "Stats");
	SWIM_CHECK_EQUAL(stats.ElementSize, std::uint32_t(sizeof(Render::ClusterStats)));
	const auto statOffsets = Offsets(stats);
	SWIM_CHECK_EQUAL(statOffsets.at("WrittenIndices"), std::uint32_t(offsetof(Render::ClusterStats, WrittenIndices)));
	SWIM_CHECK_EQUAL(statOffsets.at("DroppedIndices"), std::uint32_t(offsetof(Render::ClusterStats, DroppedIndices)));
	SWIM_CHECK_EQUAL(statOffsets.at("ClusterCount"), std::uint32_t(offsetof(Render::ClusterStats, ClusterCount)));
	CheckBindings(scan, Render::ClusterScanBindings::ThreadGroupSize, 0, 5);

	CheckBindings(Load(SWIM_CLUSTER_LIGHT_CULL_REFLECTION_PATH), Render::ClusterLightCullBindings::ThreadGroupSize, 0, 4);
	CheckBindings(Load(SWIM_CLUSTER_BOUNDS_REFLECTION_PATH), Render::ClusterBoundsBindings::ThreadGroupSize, 0, 2);
	const auto heatmap = Load(SWIM_CLUSTER_HEATMAP_REFLECTION_PATH);
	CheckBindings(heatmap, Render::ClusterHeatmapBindings::ThreadGroupSize, 0, 4);
	for (const auto& binding : heatmap.Interface.DescriptorSchemas[0].Bindings)
	{
		if (binding.Binding == Render::ClusterHeatmapBindings::Output)
		{
			SWIM_CHECK(binding.Type == Rhi::DescriptorType::StorageTexture);
			SWIM_CHECK(binding.StorageTextureFormat == Rhi::Format::RGBA8Unorm);
		}
	}

	const auto probe = Load(SWIM_RHI_CLUSTERED_LIGHT_PROBE_REFLECTION_PATH);
	SWIM_CHECK_EQUAL(Parameter(probe, "Samples").ElementSize, 80u);
	const auto sampleOffsets = Offsets(Parameter(probe, "Samples"));
	SWIM_CHECK_EQUAL(sampleOffsets.at("Pixel"), 64u);
	CheckBindings(probe, 64, 16, 7);
}
#endif
