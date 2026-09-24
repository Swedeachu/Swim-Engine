#include "Tests/Framework/Test.h"

#if defined(SWIM_SHADOW_DEPTH_REFLECTION_PATH) && defined(SWIM_SHADOW_MASKED_REFLECTION_PATH) &&                                           \
	defined(SWIM_FORWARD_OPAQUE_REFLECTION_PATH)
#include "Engine/Systems/Renderer/ForwardPlus/ForwardPlusBindings.h"
#include "Engine/Systems/Renderer/Shadows/ShadowBindings.h"
#include "Engine/Systems/Renderer/Shadows/ShadowRecords.h"
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

namespace
{
	void CheckShadowView(const ShaderCompiler::ShaderBindingReflection& parameter)
	{
		SWIM_CHECK_EQUAL(parameter.ElementSize, std::uint32_t(sizeof(Render::GpuShadowView)));
		const auto offsets = Offsets(parameter);
		SWIM_CHECK_EQUAL(offsets.at("ViewProjection"), std::uint32_t(offsetof(Render::GpuShadowView, ViewProjection)));
		SWIM_CHECK_EQUAL(offsets.at("AtlasRect"), std::uint32_t(offsetof(Render::GpuShadowView, AtlasRect)));
		SWIM_CHECK_EQUAL(offsets.at("TexelWorldSize"), std::uint32_t(offsetof(Render::GpuShadowView, TexelWorldSize)));
		SWIM_CHECK_EQUAL(offsets.at("Perspective"), std::uint32_t(offsetof(Render::GpuShadowView, Perspective)));
	}
} // namespace

// Both depth variants reflect ShadowDepthBindings with a 16-byte push block; the
// masked variant adds the bindless space, defined exactly like Forward+'s so one
// bindless table serves both.
SWIM_TEST("ShaderCompiler.ShadowLayout", "DepthProgramsMatchTheBindingContract")
{
	using B = Render::ShadowDepthBindings;
	const auto opaque = Load(SWIM_SHADOW_DEPTH_REFLECTION_PATH);
	const auto masked = Load(SWIM_SHADOW_MASKED_REFLECTION_PATH);
	SWIM_CHECK_EQUAL(opaque.Interface.DescriptorSchemas.size(), std::size_t(1));
	SWIM_CHECK_EQUAL(masked.Interface.DescriptorSchemas.size(), std::size_t(2));
	for (const auto* program : { &opaque, &masked })
	{
		const auto& schema = Space(*program, 0);
		SWIM_CHECK_EQUAL(schema.Bindings.size(), std::size_t(B::Count));
		for (std::uint32_t binding = 0; binding < B::Count; ++binding)
		{
			SWIM_CHECK(TypeOf(schema, binding) == Rhi::DescriptorType::ReadOnlyStorageBuffer);
		}
		SWIM_REQUIRE_EQUAL(program->Interface.PushConstants.size(), std::size_t(1));
		SWIM_CHECK_EQUAL(program->Interface.PushConstants[0].Size, B::PushConstantBytes);
		CheckShadowView(Parameter(*program, "Views"));
		SWIM_CHECK_EQUAL(Parameter(*program, "Materials").ElementSize, 80u);
	}

	const auto forward = Load(SWIM_FORWARD_OPAQUE_REFLECTION_PATH);
	const auto& shadowBindless = Space(masked, B::BindlessSpace).Bindings;
	const auto& forwardBindless = Space(forward, Render::ForwardPlusDrawBindings::BindlessSpace).Bindings;
	SWIM_REQUIRE_EQUAL(shadowBindless.size(), forwardBindless.size());
	for (std::size_t i = 0; i < shadowBindless.size(); ++i)
	{
		SWIM_CHECK(shadowBindless[i].Binding == forwardBindless[i].Binding && shadowBindless[i].Type == forwardBindless[i].Type);
	}
	SWIM_CHECK(TypeOf(Space(masked, B::BindlessSpace), B::BindlessTextures) == Rhi::DescriptorType::SampledTexture);
	SWIM_CHECK(TypeOf(Space(masked, B::BindlessSpace), B::BindlessSamplers) == Rhi::DescriptorType::Sampler);
}

// Forward+ samples the atlas through GpuShadowRecord / GpuShadowView exactly as C++ lays them out.
SWIM_TEST("ShaderCompiler.ShadowLayout", "ForwardPlusShadowInputsMatchTheRecords")
{
	using F = Render::ForwardPlusDrawBindings;
	const auto forward = Load(SWIM_FORWARD_OPAQUE_REFLECTION_PATH);
	const auto& schema = Space(forward, 0);
	SWIM_CHECK(TypeOf(schema, F::ShadowAtlas) == Rhi::DescriptorType::SampledTexture);
	SWIM_CHECK(TypeOf(schema, F::ShadowRecords) == Rhi::DescriptorType::ReadOnlyStorageBuffer);
	SWIM_CHECK(TypeOf(schema, F::ShadowViews) == Rhi::DescriptorType::ReadOnlyStorageBuffer);
	CheckShadowView(Parameter(forward, "ShadowViews"));
	const auto& records = Parameter(forward, "ShadowRecords");
	SWIM_CHECK_EQUAL(records.ElementSize, std::uint32_t(sizeof(Render::GpuShadowRecord)));
	const auto offsets = Offsets(records);
	SWIM_CHECK_EQUAL(offsets.at("Kind"), std::uint32_t(offsetof(Render::GpuShadowRecord, Kind)));
	SWIM_CHECK_EQUAL(offsets.at("FirstView"), std::uint32_t(offsetof(Render::GpuShadowRecord, FirstView)));
	SWIM_CHECK_EQUAL(offsets.at("ViewCount"), std::uint32_t(offsetof(Render::GpuShadowRecord, ViewCount)));
	SWIM_CHECK_EQUAL(offsets.at("PcfRadius"), std::uint32_t(offsetof(Render::GpuShadowRecord, PcfRadius)));
	SWIM_CHECK_EQUAL(offsets.at("NormalBias"), std::uint32_t(offsetof(Render::GpuShadowRecord, NormalBias)));
	SWIM_CHECK_EQUAL(offsets.at("SlopeBias"), std::uint32_t(offsetof(Render::GpuShadowRecord, SlopeBias)));
	SWIM_CHECK_EQUAL(offsets.at("DepthBias"), std::uint32_t(offsetof(Render::GpuShadowRecord, DepthBias)));
	SWIM_CHECK_EQUAL(offsets.at("CascadeFar"), std::uint32_t(offsetof(Render::GpuShadowRecord, CascadeFar)));
	SWIM_CHECK_EQUAL(offsets.at("LightPosition"), std::uint32_t(offsetof(Render::GpuShadowRecord, LightPosition)));
}
#endif
