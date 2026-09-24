#include "Tests/Framework/Test.h"

#if defined(SWIM_SKINNING_REFLECTION_PATH)
#include "Engine/Systems/Renderer/Skinning/SkinningBindings.h"
#include "Engine/Systems/Renderer/Skinning/SkinningRecords.h"
#include "Tools/ShaderCompiler/ShaderRhiInterface.h"

#include <cstddef>
#include <map>
#include <string>
#include <string_view>

using namespace Swim;

namespace
{
	const ShaderCompiler::ShaderBindingReflection* Parameter(const ShaderCompiler::ShaderReflection& reflection, std::string_view name)
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

	std::map<std::string, std::uint32_t> Offsets(const ShaderCompiler::ShaderBindingReflection& parameter)
	{
		std::map<std::string, std::uint32_t> offsets;
		for (const auto& field : parameter.ElementFields)
		{
			offsets[field.Name] = field.Offset;
		}
		return offsets;
	}
} // namespace

// SwimSkinning declares exactly SkinningBindings with the records of SkinningRecords.h,
// 64-thread groups and one push-constant word (the first dispatch row).
SWIM_TEST("ShaderCompiler.SkinningLayout", "ProgramMatchesTheBindingContractAndRecords")
{
	using T = Rhi::DescriptorType;
	using B = Render::SkinningBindings;
	auto parsed = ShaderCompiler::LoadSlangReflectionJson(SWIM_SKINNING_REFLECTION_PATH);
	SWIM_REQUIRE_MESSAGE(parsed, parsed.Error);
	auto converted = ShaderCompiler::BuildRhiShaderInterface(parsed.Reflection);
	SWIM_REQUIRE_MESSAGE(converted, converted.Error);
	const auto& program = converted.Interface;

	const std::map<std::uint32_t, T> expected{ { B::Dispatches, T::ReadOnlyStorageBuffer }, { B::SourceVertices, T::ReadOnlyStorageBuffer },
		{ B::SkinVertices, T::ReadOnlyStorageBuffer }, { B::MorphDeltas, T::ReadOnlyStorageBuffer },
		{ B::Palettes, T::ReadOnlyStorageBuffer }, { B::MorphWeights, T::ReadOnlyStorageBuffer }, { B::Output, T::StorageBuffer } };
	const Rhi::DescriptorSchemaDesc* schema = nullptr;
	for (const auto& candidate : program.DescriptorSchemas)
	{
		schema = candidate.Space == 0 ? &candidate : schema;
	}
	SWIM_REQUIRE(schema != nullptr);
	SWIM_REQUIRE_EQUAL(schema->Bindings.size(), expected.size());
	for (const auto& binding : schema->Bindings)
	{
		SWIM_REQUIRE_MESSAGE(expected.contains(binding.Binding), "unexpected binding " + std::to_string(binding.Binding));
		SWIM_CHECK_MESSAGE(binding.Type == expected.at(binding.Binding), "binding " + std::to_string(binding.Binding));
	}
	SWIM_CHECK((program.ComputeThreadGroupSize == std::array<std::uint32_t, 3>{ Render::SkinningThreadGroupSize, 1, 1 }));
	SWIM_REQUIRE_EQUAL(program.PushConstants.size(), std::size_t(1));
	SWIM_CHECK_EQUAL(program.PushConstants[0].Size, B::PushConstantBytes);

	const auto* dispatches = Parameter(parsed.Reflection, "Dispatches");
	const auto* skins = Parameter(parsed.Reflection, "SkinVertices");
	const auto* deltas = Parameter(parsed.Reflection, "MorphDeltas");
	SWIM_REQUIRE(dispatches && skins && deltas);
	SWIM_CHECK_EQUAL(dispatches->ElementSize, std::uint32_t(sizeof(Render::GpuSkinDispatch)));
	SWIM_CHECK_EQUAL(skins->ElementSize, std::uint32_t(sizeof(Render::GpuSkinVertex)));
	SWIM_CHECK_EQUAL(deltas->ElementSize, std::uint32_t(sizeof(Render::GpuMorphDelta)));
	using D = Render::GpuSkinDispatch;
	const auto dispatch = Offsets(*dispatches);
	SWIM_CHECK_EQUAL(dispatch.at("OutputVertex"), std::uint32_t(offsetof(D, OutputVertex)));
	SWIM_CHECK_EQUAL(dispatch.at("PreviousOffset"), std::uint32_t(offsetof(D, PreviousOffset)));
	SWIM_CHECK_EQUAL(dispatch.at("Palette"), std::uint32_t(offsetof(D, Palette)));
	SWIM_CHECK_EQUAL(dispatch.at("MorphTargetCount"), std::uint32_t(offsetof(D, MorphTargetCount)));
	SWIM_CHECK_EQUAL(dispatch.at("SourceMorph"), std::uint32_t(offsetof(D, SourceMorph)));
	const auto skin = Offsets(*skins);
	SWIM_CHECK_EQUAL(skin.at("Weights"), std::uint32_t(offsetof(Render::GpuSkinVertex, Weights)));
	SWIM_CHECK_EQUAL(skin.at("MorphFirst"), std::uint32_t(offsetof(Render::GpuSkinVertex, MorphFirst)));
	SWIM_CHECK_EQUAL(skin.at("MorphCount"), std::uint32_t(offsetof(Render::GpuSkinVertex, MorphCount)));
	const auto delta = Offsets(*deltas);
	SWIM_CHECK_EQUAL(delta.at("Position"), std::uint32_t(offsetof(Render::GpuMorphDelta, Position)));
	SWIM_CHECK_EQUAL(delta.at("Normal"), std::uint32_t(offsetof(Render::GpuMorphDelta, Normal)));
	SWIM_CHECK_EQUAL(delta.at("Tangent"), std::uint32_t(offsetof(Render::GpuMorphDelta, Tangent)));
}
#endif
