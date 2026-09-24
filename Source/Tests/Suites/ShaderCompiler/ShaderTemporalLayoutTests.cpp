#include "Tests/Framework/Test.h"

#if defined(SWIM_TEMPORAL_RESOLVE_REFLECTION_PATH)
#include "Engine/Systems/Renderer/Temporal/TemporalBindings.h"
#include "Engine/Systems/Renderer/Temporal/TemporalRecords.h"
#include "Tools/ShaderCompiler/ShaderRhiInterface.h"

#include <array>
#include <cstddef>
#include <string>

using namespace Swim;

// SwimTemporalResolve reflects its TemporalResolveBindings contract: four sampled
// inputs and one storage output in space 0, 32 bytes of push constants, 8 x 8 groups.
SWIM_TEST("ShaderCompiler.TemporalLayout", "ResolveMatchesTheBindingContract")
{
	using B = Render::TemporalResolveBindings;
	using T = Rhi::DescriptorType;
	auto parsed = ShaderCompiler::LoadSlangReflectionJson(SWIM_TEMPORAL_RESOLVE_REFLECTION_PATH);
	SWIM_REQUIRE_MESSAGE(parsed, parsed.Error);
	auto converted = ShaderCompiler::BuildRhiShaderInterface(parsed.Reflection);
	SWIM_REQUIRE_MESSAGE(converted, converted.Error);
	const auto& program = converted.Interface;

	SWIM_REQUIRE_EQUAL(program.DescriptorSchemas.size(), std::size_t(1));
	const auto& schema = program.DescriptorSchemas[0];
	SWIM_CHECK_EQUAL(schema.Space, 0u);
	SWIM_REQUIRE_EQUAL(schema.Bindings.size(), std::size_t(B::Count));
	const std::array<T, B::Count> expected{ T::SampledTexture, T::SampledTexture, T::SampledTexture, T::SampledTexture, T::StorageTexture };
	for (const auto& binding : schema.Bindings)
	{
		SWIM_REQUIRE(binding.Binding < B::Count);
		SWIM_CHECK_MESSAGE(binding.Type == expected[binding.Binding], "binding " + std::to_string(binding.Binding));
		SWIM_CHECK_EQUAL(binding.Count, 1u);
	}
	SWIM_REQUIRE_EQUAL(program.PushConstants.size(), std::size_t(1));
	SWIM_CHECK_EQUAL(program.PushConstants[0].Size, B::PushConstantBytes);
	SWIM_CHECK_EQUAL(B::PushConstantBytes, std::uint32_t(sizeof(Render::TemporalResolveConstants)));
	SWIM_CHECK((program.ComputeThreadGroupSize == std::array<std::uint32_t, 3>{ B::ThreadGroupSize, B::ThreadGroupSize, 1 }));
}
#endif
