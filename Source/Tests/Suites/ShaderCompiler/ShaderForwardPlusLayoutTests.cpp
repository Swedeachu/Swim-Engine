#include "Tests/Framework/Test.h"

#if defined(SWIM_FORWARD_OPAQUE_REFLECTION_PATH) && defined(SWIM_FORWARD_TRANSPARENT_REFLECTION_PATH) &&                                   \
	defined(SWIM_FORWARD_TRANSPARENT_SORT_REFLECTION_PATH)
#include "Engine/Systems/Renderer/ForwardPlus/ForwardPlusBindings.h"
#include "Engine/Systems/Renderer/ForwardPlus/ForwardPlusRecords.h"
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

	const Rhi::DescriptorSchemaDesc& Space(const Program& program, std::uint32_t space)
	{
		for (const auto& schema : program.Interface.DescriptorSchemas)
		{
			if (schema.Space == space)
			{
				return schema;
			}
		}
		SWIM_FAIL("missing descriptor space " + std::to_string(space));
		throw std::logic_error("unreachable");
	}

	Rhi::DescriptorType TypeOf(const Rhi::DescriptorSchemaDesc& schema, std::uint32_t binding)
	{
		for (const auto& candidate : schema.Bindings)
		{
			if (candidate.Binding == binding)
			{
				return candidate.Type;
			}
		}
		SWIM_FAIL("missing binding " + std::to_string(binding));
		throw std::logic_error("unreachable");
	}
} // namespace

// Both Forward+ variants reflect the same bindings (so one bindless table and the
// same table-building code serve them), laid out as ForwardPlusDrawBindings, and
// the view record equals ForwardViewRecord.
SWIM_TEST("ShaderCompiler.ForwardPlusLayout", "DrawProgramsMatchTheBindingContract")
{
	using B = Render::ForwardPlusDrawBindings;
	const auto opaque = Load(SWIM_FORWARD_OPAQUE_REFLECTION_PATH);
	const auto transparent = Load(SWIM_FORWARD_TRANSPARENT_REFLECTION_PATH);
	for (const auto* program : { &opaque, &transparent })
	{
		SWIM_REQUIRE_EQUAL(program->Interface.DescriptorSchemas.size(), std::size_t(2));
		const auto& draw = Space(*program, 0);
		SWIM_CHECK_EQUAL(draw.Bindings.size(), std::size_t(B::Count));
		for (std::uint32_t binding = 0; binding < B::Count; ++binding)
		{
			const auto type = TypeOf(draw, binding);
			if (binding == B::EnvironmentSampler)
			{
				SWIM_CHECK(type == Rhi::DescriptorType::Sampler);
			}
			else if (binding == B::EnvironmentPrefiltered || binding == B::EnvironmentBrdfLut || binding == B::ShadowAtlas)
			{
				SWIM_CHECK(type == Rhi::DescriptorType::SampledTexture);
			}
			else
			{
				SWIM_CHECK(type == Rhi::DescriptorType::ReadOnlyStorageBuffer);
			}
		}
		const auto& bindless = Space(*program, B::BindlessSpace);
		SWIM_CHECK(TypeOf(bindless, B::BindlessSamplers) == Rhi::DescriptorType::Sampler);
		SWIM_CHECK(TypeOf(bindless, B::BindlessTextures) == Rhi::DescriptorType::SampledTexture);
		SWIM_CHECK(program->Interface.PushConstants.empty());

		const auto& view = Parameter(*program, "Views");
		SWIM_CHECK_EQUAL(view.ElementSize, std::uint32_t(sizeof(Render::ForwardViewRecord)));
		const auto offsets = Offsets(view);
		SWIM_CHECK_EQUAL(offsets.at("CameraPosition"), std::uint32_t(offsetof(Render::ForwardViewRecord, CameraPosition)));
		SWIM_CHECK_EQUAL(offsets.at("EnvironmentIntensity"), std::uint32_t(offsetof(Render::ForwardViewRecord, EnvironmentIntensity)));
		SWIM_CHECK_EQUAL(offsets.at("CameraForward"), std::uint32_t(offsetof(Render::ForwardViewRecord, CameraForward)));
		SWIM_CHECK_EQUAL(offsets.at("EnvironmentRotation"), std::uint32_t(offsetof(Render::ForwardViewRecord, EnvironmentRotation)));
		SWIM_CHECK_EQUAL(offsets.at("Ambient"), std::uint32_t(offsetof(Render::ForwardViewRecord, Ambient)));
		SWIM_CHECK_EQUAL(offsets.at("MaterialCount"), std::uint32_t(offsetof(Render::ForwardViewRecord, MaterialCount)));
		SWIM_CHECK_EQUAL(offsets.at("PrefilteredMipCount"), std::uint32_t(offsetof(Render::ForwardViewRecord, PrefilteredMipCount)));
		SWIM_CHECK_EQUAL(offsets.at("Flags"), std::uint32_t(offsetof(Render::ForwardViewRecord, Flags)));
		SWIM_CHECK_EQUAL(offsets.at("DebugMode"), std::uint32_t(offsetof(Render::ForwardViewRecord, DebugMode)));
		SWIM_CHECK_EQUAL(Parameter(*program, "Materials").ElementSize, 80u);
		SWIM_CHECK_EQUAL(Parameter(*program, "Grid").ElementSize, 128u);
	}
	// Identical space-0 bindings in both variants.
	const auto& a = Space(opaque, 0).Bindings;
	const auto& b = Space(transparent, 0).Bindings;
	SWIM_REQUIRE_EQUAL(a.size(), b.size());
	for (std::size_t i = 0; i < a.size(); ++i)
	{
		SWIM_CHECK(a[i].Binding == b[i].Binding && a[i].Type == b[i].Type && a[i].Count == b[i].Count);
	}
}

SWIM_TEST("ShaderCompiler.ForwardPlusLayout", "TransparentSortMatchesItsBindingContract")
{
	using B = Render::ForwardTransparentSortBindings;
	const auto sort = Load(SWIM_FORWARD_TRANSPARENT_SORT_REFLECTION_PATH);
	SWIM_CHECK((sort.Interface.ComputeThreadGroupSize == std::array<std::uint32_t, 3>{ B::ThreadGroupSize, 1, 1 }));
	SWIM_REQUIRE_EQUAL(sort.Interface.PushConstants.size(), std::size_t(1));
	SWIM_CHECK_EQUAL(sort.Interface.PushConstants[0].Size, B::PushConstantBytes);
	SWIM_REQUIRE_EQUAL(sort.Interface.DescriptorSchemas.size(), std::size_t(1));
	const auto& schema = sort.Interface.DescriptorSchemas[0];
	SWIM_CHECK_EQUAL(schema.Bindings.size(), std::size_t(B::Count));
	for (const auto binding : { B::Scratch, B::SortedCommands, B::SortedCounts })
	{
		SWIM_CHECK(TypeOf(schema, binding) == Rhi::DescriptorType::StorageBuffer);
	}
	for (const auto binding : { B::Commands, B::DrawRecords, B::Counts, B::Instances, B::Transforms, B::View })
	{
		SWIM_CHECK(TypeOf(schema, binding) == Rhi::DescriptorType::ReadOnlyStorageBuffer);
	}
	const auto& scratch = Parameter(sort, "Scratch");
	SWIM_CHECK_EQUAL(scratch.ElementSize, std::uint32_t(sizeof(Render::ForwardSortEntry)));
	const auto offsets = Offsets(scratch);
	SWIM_CHECK_EQUAL(offsets.at("InstanceRow"), std::uint32_t(offsetof(Render::ForwardSortEntry, InstanceRow)));
	SWIM_CHECK_EQUAL(offsets.at("Slot"), std::uint32_t(offsetof(Render::ForwardSortEntry, Slot)));
	SWIM_CHECK_EQUAL(Parameter(sort, "SortedCommands").ElementSize, 20u);
	SWIM_CHECK_EQUAL(Parameter(sort, "Views").ElementSize, std::uint32_t(sizeof(Render::ForwardViewRecord)));
}
#endif
