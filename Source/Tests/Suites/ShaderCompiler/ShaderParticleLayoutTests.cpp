#include "Tests/Framework/Test.h"

#if defined(SWIM_PARTICLE_SIMULATE_REFLECTION_PATH) && defined(SWIM_PARTICLE_EMIT_REFLECTION_PATH) &&                                      \
	defined(SWIM_PARTICLE_COMPACT_REFLECTION_PATH) && defined(SWIM_PARTICLE_FINALIZE_REFLECTION_PATH) &&                                   \
	defined(SWIM_PARTICLE_RENDER_REFLECTION_PATH)
#include "Engine/Systems/Renderer/Particles/ParticleBindings.h"
#include "Engine/Systems/Renderer/Particles/ParticleRecords.h"
#include "Tools/ShaderCompiler/ShaderRhiInterface.h"

#include <cstddef>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

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

	const ShaderCompiler::ShaderBindingReflection* Parameter(const Program& program, std::string_view name)
	{
		for (const auto& parameter : program.Reflection.GlobalParameters)
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

	// Space 0 holds exactly `expected` (binding -> type).
	void CheckSpace(const Program& program, const std::map<std::uint32_t, Rhi::DescriptorType>& expected)
	{
		const Rhi::DescriptorSchemaDesc* schema = nullptr;
		for (const auto& candidate : program.Interface.DescriptorSchemas)
		{
			schema = candidate.Space == 0 ? &candidate : schema;
		}
		SWIM_REQUIRE(schema != nullptr);
		SWIM_REQUIRE_EQUAL(schema->Bindings.size(), expected.size());
		for (const auto& binding : schema->Bindings)
		{
			SWIM_REQUIRE_MESSAGE(expected.contains(binding.Binding), "unexpected binding " + std::to_string(binding.Binding));
			SWIM_CHECK_MESSAGE(binding.Type == expected.at(binding.Binding), "binding " + std::to_string(binding.Binding));
			SWIM_CHECK_EQUAL(binding.Count, 1u);
		}
	}

	// Every record a program reads equals its C++ mirror.
	void CheckRecords(const Program& program)
	{
		using namespace Render;
		if (const auto* particles = Parameter(program, "Particles"))
		{
			SWIM_CHECK_EQUAL(particles->ElementSize, std::uint32_t(sizeof(GpuParticle)));
			const auto offsets = Offsets(*particles);
			SWIM_CHECK_EQUAL(offsets.at("Velocity"), std::uint32_t(offsetof(GpuParticle, Velocity)));
			SWIM_CHECK_EQUAL(offsets.at("Lifetime"), std::uint32_t(offsetof(GpuParticle, Lifetime)));
			SWIM_CHECK_EQUAL(offsets.at("Id"), std::uint32_t(offsetof(GpuParticle, Id)));
		}
		if (const auto* emitters = Parameter(program, "Emitters"))
		{
			SWIM_CHECK_EQUAL(emitters->ElementSize, std::uint32_t(sizeof(GpuParticleEmitter)));
			const auto offsets = Offsets(*emitters);
			using E = GpuParticleEmitter;
			SWIM_CHECK_EQUAL(offsets.at("Gravity"), std::uint32_t(offsetof(E, Gravity)));
			SWIM_CHECK_EQUAL(offsets.at("Shape"), std::uint32_t(offsetof(E, Shape)));
			SWIM_CHECK_EQUAL(offsets.at("CosConeAngle"), std::uint32_t(offsetof(E, CosConeAngle)));
			SWIM_CHECK_EQUAL(offsets.at("Friction"), std::uint32_t(offsetof(E, Friction)));
			SWIM_CHECK_EQUAL(offsets.at("SpawnCount"), std::uint32_t(offsetof(E, SpawnCount)));
			SWIM_CHECK_EQUAL(offsets.at("FlipbookFrameRate"), std::uint32_t(offsetof(E, FlipbookFrameRate)));
			SWIM_CHECK_EQUAL(offsets.at("Row"), std::uint32_t(offsetof(E, Row)));
			SWIM_CHECK_EQUAL(offsets.at("SizeTimes"), std::uint32_t(offsetof(E, SizeTimes)));
			SWIM_CHECK_EQUAL(offsets.at("ColorValues"), std::uint32_t(offsetof(E, ColorValues)));
		}
		if (const auto* frame = Parameter(program, "Frame"))
		{
			SWIM_CHECK_EQUAL(frame->ElementSize, std::uint32_t(sizeof(GpuParticleFrame)));
			const auto offsets = Offsets(*frame);
			SWIM_CHECK_EQUAL(offsets.at("CameraPosition"), std::uint32_t(offsetof(GpuParticleFrame, CameraPosition)));
			SWIM_CHECK_EQUAL(offsets.at("DeltaTime"), std::uint32_t(offsetof(GpuParticleFrame, DeltaTime)));
			SWIM_CHECK_EQUAL(offsets.at("CameraForward"), std::uint32_t(offsetof(GpuParticleFrame, CameraForward)));
		}
		if (const auto* counters = Parameter(program, "Counters"))
		{
			SWIM_CHECK_EQUAL(counters->ElementSize, std::uint32_t(sizeof(GpuParticleCounters)));
		}
	}
} // namespace

// The four compute programs declare exactly their ParticleProgramBindings subset with the
// records of ParticleRecords.h; the render program adds the bindless space and one push
// constant.
SWIM_TEST("ShaderCompiler.ParticleLayout", "ProgramsMatchTheBindingContractAndRecords")
{
	using T = Rhi::DescriptorType;
	using B = Render::ParticleSimulationBindings;
	const auto simulate = Load(SWIM_PARTICLE_SIMULATE_REFLECTION_PATH);
	CheckSpace(simulate,
		{ { B::Frame, T::ReadOnlyStorageBuffer }, { B::Emitters, T::ReadOnlyStorageBuffer }, { B::Particles, T::StorageBuffer },
			{ B::FreeList, T::StorageBuffer }, { B::Counters, T::StorageBuffer } });
	const auto emit = Load(SWIM_PARTICLE_EMIT_REFLECTION_PATH);
	CheckSpace(emit,
		{ { B::Emitters, T::ReadOnlyStorageBuffer }, { B::Particles, T::StorageBuffer }, { B::FreeList, T::StorageBuffer },
			{ B::Counters, T::StorageBuffer } });
	const auto compact = Load(SWIM_PARTICLE_COMPACT_REFLECTION_PATH);
	CheckSpace(compact,
		{ { B::Emitters, T::ReadOnlyStorageBuffer }, { B::Particles, T::ReadOnlyStorageBuffer }, { B::DrawList, T::StorageBuffer },
			{ B::Counters, T::StorageBuffer } });
	const auto finalize = Load(SWIM_PARTICLE_FINALIZE_REFLECTION_PATH);
	CheckSpace(finalize,
		{ { B::Frame, T::ReadOnlyStorageBuffer }, { B::Emitters, T::ReadOnlyStorageBuffer }, { B::Particles, T::ReadOnlyStorageBuffer },
			{ B::DrawList, T::StorageBuffer }, { B::Counters, T::StorageBuffer }, { B::DrawArgs, T::StorageBuffer } });
	for (const auto* program : { &simulate, &emit, &compact })
	{
		SWIM_CHECK((program->Interface.ComputeThreadGroupSize == std::array<std::uint32_t, 3>{ Render::ParticleThreadGroupSize, 1, 1 }));
		SWIM_CHECK(program->Interface.PushConstants.empty());
		CheckRecords(*program);
	}
	SWIM_CHECK((finalize.Interface.ComputeThreadGroupSize == std::array<std::uint32_t, 3>{ Render::ParticleFinalizeGroupSize, 1, 1 }));
	CheckRecords(finalize);
	// The C++ binding subsets name the same bindings.
	SWIM_CHECK(Render::ParticleProgramBindings::Simulate.size() == 5u && Render::ParticleProgramBindings::Finalize.size() == 6u);

	using R = Render::ParticleRenderBindings;
	const auto render = Load(SWIM_PARTICLE_RENDER_REFLECTION_PATH);
	CheckSpace(render,
		{ { R::Frame, T::ReadOnlyStorageBuffer }, { R::Emitters, T::ReadOnlyStorageBuffer }, { R::Particles, T::ReadOnlyStorageBuffer },
			{ R::DrawList, T::ReadOnlyStorageBuffer } });
	CheckRecords(render);
	SWIM_REQUIRE_EQUAL(render.Interface.PushConstants.size(), std::size_t(1));
	SWIM_CHECK_EQUAL(render.Interface.PushConstants[0].Size, R::PushConstantBytes);
	const Rhi::DescriptorSchemaDesc* bindless = nullptr;
	for (const auto& schema : render.Interface.DescriptorSchemas)
	{
		bindless = schema.Space == R::BindlessSpace ? &schema : bindless;
	}
	SWIM_REQUIRE(bindless != nullptr);
	SWIM_REQUIRE_EQUAL(bindless->Bindings.size(), std::size_t(2));
	for (const auto& binding : bindless->Bindings)
	{
		SWIM_CHECK(binding.Count == 0u); // Runtime-sized.
		SWIM_CHECK((binding.Binding == R::BindlessSamplers && binding.Type == T::Sampler) ||
			(binding.Binding == R::BindlessTextures && binding.Type == T::SampledTexture));
	}
}
#endif
