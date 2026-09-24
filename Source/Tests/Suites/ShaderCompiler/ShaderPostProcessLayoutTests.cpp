#include "Tests/Framework/Test.h"

#if defined(SWIM_POST_HISTOGRAM_REFLECTION_PATH) && defined(SWIM_POST_EXPOSURE_REFLECTION_PATH) &&                                         \
	defined(SWIM_POST_BLOOM_DOWNSAMPLE_REFLECTION_PATH) && defined(SWIM_POST_BLOOM_UPSAMPLE_REFLECTION_PATH) &&                            \
	defined(SWIM_POST_COMPOSITE_REFLECTION_PATH) && defined(SWIM_POST_COMPOSITE_HDR_REFLECTION_PATH)
#include "Engine/Systems/Renderer/PostProcess/PostProcessBindings.h"
#include "Engine/Systems/Renderer/PostProcess/PostProcessRecords.h"
#include "Tools/ShaderCompiler/ShaderRhiInterface.h"

#include <array>
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
	void CheckProgram(const Program& program, std::uint32_t pushBytes, std::uint32_t group,
		std::initializer_list<std::pair<std::uint32_t, Rhi::DescriptorType>> bindings)
	{
		SWIM_REQUIRE_EQUAL(program.Interface.DescriptorSchemas.size(), std::size_t(1));
		const auto& schema = Space(program, 0);
		SWIM_CHECK_EQUAL(schema.Bindings.size(), bindings.size());
		for (const auto& [binding, type] : bindings)
		{
			SWIM_CHECK(TypeOf(schema, binding) == type);
		}
		SWIM_REQUIRE_EQUAL(program.Interface.PushConstants.size(), std::size_t(1));
		SWIM_CHECK_EQUAL(program.Interface.PushConstants[0].Size, pushBytes);
		SWIM_CHECK((program.Interface.ComputeThreadGroupSize == std::array<std::uint32_t, 3>{ group, group, 1 }));
	}

	void CheckExposureState(const ShaderCompiler::ShaderBindingReflection& parameter)
	{
		SWIM_CHECK_EQUAL(parameter.ElementSize, std::uint32_t(sizeof(Render::GpuExposureState)));
		const auto offsets = Offsets(parameter);
		SWIM_CHECK_EQUAL(offsets.at("Ev100"), std::uint32_t(offsetof(Render::GpuExposureState, Ev100)));
		SWIM_CHECK_EQUAL(offsets.at("AverageLog2Luminance"), std::uint32_t(offsetof(Render::GpuExposureState, AverageLog2Luminance)));
		SWIM_CHECK_EQUAL(offsets.at("Exposure"), std::uint32_t(offsetof(Render::GpuExposureState, Exposure)));
		SWIM_CHECK_EQUAL(offsets.at("Valid"), std::uint32_t(offsetof(Render::GpuExposureState, Valid)));
	}
} // namespace

// Every post-processing program reflects its PostProcessBindings contract: binding
// types, push-constant size and thread-group size, and the records equal the C++ structs.
SWIM_TEST("ShaderCompiler.PostProcessLayout", "ProgramsMatchTheBindingContract")
{
	using T = Rhi::DescriptorType;
	const auto histogram = Load(SWIM_POST_HISTOGRAM_REFLECTION_PATH);
	CheckProgram(histogram, Render::PostHistogramBindings::PushConstantBytes, Render::PostHistogramBindings::ThreadGroupSize,
		{ { Render::PostHistogramBindings::Source, T::SampledTexture }, { Render::PostHistogramBindings::Histogram, T::StorageBuffer } });

	const auto exposure = Load(SWIM_POST_EXPOSURE_REFLECTION_PATH);
	CheckProgram(exposure, Render::PostExposureBindings::PushConstantBytes, 1,
		{ { Render::PostExposureBindings::Histogram, T::ReadOnlyStorageBuffer },
			{ Render::PostExposureBindings::State, T::StorageBuffer } });
	CheckExposureState(Parameter(exposure, "State"));

	const auto down = Load(SWIM_POST_BLOOM_DOWNSAMPLE_REFLECTION_PATH);
	CheckProgram(down, Render::PostBloomDownsampleBindings::PushConstantBytes, Render::PostBloomDownsampleBindings::ThreadGroupSize,
		{ { Render::PostBloomDownsampleBindings::Source, T::SampledTexture },
			{ Render::PostBloomDownsampleBindings::Destination, T::StorageTexture },
			{ Render::PostBloomDownsampleBindings::State, T::ReadOnlyStorageBuffer } });

	const auto up = Load(SWIM_POST_BLOOM_UPSAMPLE_REFLECTION_PATH);
	CheckProgram(up, Render::PostBloomUpsampleBindings::PushConstantBytes, Render::PostBloomUpsampleBindings::ThreadGroupSize,
		{ { Render::PostBloomUpsampleBindings::Low, T::SampledTexture }, { Render::PostBloomUpsampleBindings::High, T::SampledTexture },
			{ Render::PostBloomUpsampleBindings::Destination, T::StorageTexture } });

	for (const char* path : { SWIM_POST_COMPOSITE_REFLECTION_PATH, SWIM_POST_COMPOSITE_HDR_REFLECTION_PATH })
	{
		const auto composite = Load(path);
		using B = Render::PostCompositeBindings;
		CheckProgram(composite, B::PushConstantBytes, B::ThreadGroupSize,
			{ { B::Source, T::SampledTexture }, { B::Bloom, T::SampledTexture }, { B::State, T::ReadOnlyStorageBuffer },
				{ B::Params, T::ReadOnlyStorageBuffer }, { B::Output, T::StorageTexture } });
		CheckExposureState(Parameter(composite, "State"));
		const auto& params = Parameter(composite, "Params");
		SWIM_CHECK_EQUAL(params.ElementSize, std::uint32_t(sizeof(Render::GpuPostParams)));
		const auto offsets = Offsets(params);
		SWIM_CHECK_EQUAL(offsets.at("WhiteBalance"), std::uint32_t(offsetof(Render::GpuPostParams, WhiteBalance)));
		SWIM_CHECK_EQUAL(offsets.at("SlopeContrast"), std::uint32_t(offsetof(Render::GpuPostParams, SlopeContrast)));
		SWIM_CHECK_EQUAL(offsets.at("OffsetSaturation"), std::uint32_t(offsetof(Render::GpuPostParams, OffsetSaturation)));
		SWIM_CHECK_EQUAL(offsets.at("PowerBloom"), std::uint32_t(offsetof(Render::GpuPostParams, PowerBloom)));
		SWIM_CHECK_EQUAL(offsets.at("ToneMapper"), std::uint32_t(offsetof(Render::GpuPostParams, ToneMapper)));
		SWIM_CHECK_EQUAL(offsets.at("Encoding"), std::uint32_t(offsetof(Render::GpuPostParams, Encoding)));
		SWIM_CHECK_EQUAL(offsets.at("Dither"), std::uint32_t(offsetof(Render::GpuPostParams, Dither)));
		SWIM_CHECK_EQUAL(offsets.at("BloomEnabled"), std::uint32_t(offsetof(Render::GpuPostParams, BloomEnabled)));
		SWIM_CHECK_EQUAL(offsets.at("WhitePoint"), std::uint32_t(offsetof(Render::GpuPostParams, WhitePoint)));
		SWIM_CHECK_EQUAL(offsets.at("PaperWhiteNits"), std::uint32_t(offsetof(Render::GpuPostParams, PaperWhiteNits)));
		SWIM_CHECK_EQUAL(offsets.at("PeakNits"), std::uint32_t(offsetof(Render::GpuPostParams, PeakNits)));
		SWIM_CHECK_EQUAL(offsets.at("GradingEnabled"), std::uint32_t(offsetof(Render::GpuPostParams, GradingEnabled)));
	}
}
#endif
