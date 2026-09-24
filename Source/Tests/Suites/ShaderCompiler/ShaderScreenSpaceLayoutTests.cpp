#include "Tests/Framework/Test.h"

#if defined(SWIM_SCREEN_SPACE_AO_REFLECTION_PATH) && defined(SWIM_SCREEN_SPACE_BLUR_REFLECTION_PATH) &&                                    \
	defined(SWIM_SCREEN_SPACE_COMPOSITE_REFLECTION_PATH) && defined(SWIM_SCREEN_SPACE_REFLECTION_REFLECTION_PATH)
#include "Engine/Systems/Renderer/ScreenSpace/ScreenSpaceBindings.h"
#include "Engine/Systems/Renderer/ScreenSpace/ScreenSpaceRecords.h"
#include "Tools/ShaderCompiler/ShaderRhiInterface.h"

#include <array>
#include <cstddef>
#include <map>
#include <string>
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

	void CheckProgram(const Program& program, const std::vector<Rhi::DescriptorType>& expected, std::uint32_t paramsBinding)
	{
		SWIM_REQUIRE_EQUAL(program.Interface.DescriptorSchemas.size(), std::size_t(1));
		const auto& schema = program.Interface.DescriptorSchemas[0];
		SWIM_CHECK_EQUAL(schema.Space, 0u);
		SWIM_REQUIRE_EQUAL(schema.Bindings.size(), expected.size());
		for (const auto& binding : schema.Bindings)
		{
			SWIM_REQUIRE(binding.Binding < expected.size());
			SWIM_CHECK_MESSAGE(binding.Type == expected[binding.Binding], "binding " + std::to_string(binding.Binding));
		}
		SWIM_CHECK(program.Interface.PushConstants.empty());
		SWIM_CHECK((program.Interface.ComputeThreadGroupSize ==
			std::array<std::uint32_t, 3>{ Render::ScreenSpaceThreadGroupSize, Render::ScreenSpaceThreadGroupSize, 1 }));
		// The parameter record equals GpuScreenSpaceParams.
		const ShaderCompiler::ShaderBindingReflection* params = nullptr;
		for (const auto& parameter : program.Reflection.GlobalParameters)
		{
			params = parameter.Name == "Params" ? &parameter : params;
		}
		SWIM_REQUIRE(params != nullptr);
		SWIM_CHECK_EQUAL(params->ElementSize, std::uint32_t(sizeof(Render::GpuScreenSpaceParams)));
		std::map<std::string, std::uint32_t> offsets;
		for (const auto& field : params->ElementFields)
		{
			offsets[field.Name] = field.Offset;
		}
		using P = Render::GpuScreenSpaceParams;
		SWIM_CHECK_EQUAL(offsets.at("ViewRows"), std::uint32_t(offsetof(P, ViewRows)));
		SWIM_CHECK_EQUAL(offsets.at("InverseViewRows"), std::uint32_t(offsetof(P, InverseViewRows)));
		SWIM_CHECK_EQUAL(offsets.at("Jitter"), std::uint32_t(offsetof(P, Jitter)));
		SWIM_CHECK_EQUAL(offsets.at("AoRadius"), std::uint32_t(offsetof(P, AoRadius)));
		SWIM_CHECK_EQUAL(offsets.at("AoSliceCount"), std::uint32_t(offsetof(P, AoSliceCount)));
		SWIM_CHECK_EQUAL(offsets.at("FogEnabled"), std::uint32_t(offsetof(P, FogEnabled)));
		SWIM_CHECK_EQUAL(offsets.at("FogColor"), std::uint32_t(offsetof(P, FogColor)));
		SWIM_CHECK_EQUAL(offsets.at("FogDensity"), std::uint32_t(offsetof(P, FogDensity)));
		SWIM_CHECK_EQUAL(offsets.at("FogSunDirection"), std::uint32_t(offsetof(P, FogSunDirection)));
		SWIM_CHECK_EQUAL(offsets.at("FogMaxDistance"), std::uint32_t(offsetof(P, FogMaxDistance)));
		SWIM_CHECK_EQUAL(offsets.at("SsrEnabled"), std::uint32_t(offsetof(P, SsrEnabled)));
		SWIM_CHECK_EQUAL(offsets.at("Projection"), std::uint32_t(offsetof(P, Projection)));
		SWIM_CHECK_EQUAL(offsets.at("SsrMaxDistance"), std::uint32_t(offsetof(P, SsrMaxDistance)));
		SWIM_CHECK_EQUAL(offsets.at("SsrNearZ"), std::uint32_t(offsetof(P, SsrNearZ)));
		SWIM_CHECK_EQUAL(offsets.at("SsrMaxSteps"), std::uint32_t(offsetof(P, SsrMaxSteps)));
		SWIM_CHECK_EQUAL(offsets.at("SsrRefineSteps"), std::uint32_t(offsetof(P, SsrRefineSteps)));
		(void)paramsBinding;
	}
} // namespace

// Each screen-space program reflects its ScreenSpace*Bindings contract, runs 8 x 8 groups
// without push constants, and reads a parameter record equal to GpuScreenSpaceParams.
SWIM_TEST("ShaderCompiler.ScreenSpaceLayout", "ProgramsMatchTheBindingContract")
{
	using T = Rhi::DescriptorType;
	CheckProgram(Load(SWIM_SCREEN_SPACE_AO_REFLECTION_PATH),
		{ T::SampledTexture, T::SampledTexture, T::ReadOnlyStorageBuffer, T::StorageTexture }, Render::ScreenSpaceAoBindings::Params);
	CheckProgram(Load(SWIM_SCREEN_SPACE_BLUR_REFLECTION_PATH),
		{ T::SampledTexture, T::SampledTexture, T::ReadOnlyStorageBuffer, T::StorageTexture }, Render::ScreenSpaceBlurBindings::Params);
	CheckProgram(Load(SWIM_SCREEN_SPACE_COMPOSITE_REFLECTION_PATH),
		{ T::SampledTexture, T::SampledTexture, T::SampledTexture, T::SampledTexture, T::ReadOnlyStorageBuffer, T::StorageTexture,
			T::SampledTexture, T::SampledTexture, T::SampledTexture },
		Render::ScreenSpaceCompositeBindings::Params);
	CheckProgram(Load(SWIM_SCREEN_SPACE_REFLECTION_REFLECTION_PATH),
		{ T::SampledTexture, T::SampledTexture, T::SampledTexture, T::SampledTexture, T::SampledTexture, T::ReadOnlyStorageBuffer,
			T::StorageTexture },
		Render::ScreenSpaceReflectionBindings::Params);
	static_assert(Render::ScreenSpaceAoBindings::Count == 4 && Render::ScreenSpaceBlurBindings::Count == 4 &&
		Render::ScreenSpaceCompositeBindings::Count == 9 && Render::ScreenSpaceReflectionBindings::Count == 7);
}
#endif
