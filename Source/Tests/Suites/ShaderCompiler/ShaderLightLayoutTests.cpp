#include "Tests/Framework/Test.h"

#ifdef SWIM_RHI_GPU_LIGHT_PROBE_REFLECTION_PATH
#include "Engine/Systems/Renderer/Lights/GpuLightRecord.h"
#include "Tools/ShaderCompiler/ShaderRhiInterface.h"

#include <cstddef>
#include <map>
#include <string>

using namespace Swim;

// GpuLightRecords.slang's records match GpuLightRecord/GpuLightHeader field for
// field, and the probe's bindings match the native light smoke.
SWIM_TEST("ShaderCompiler.LightLayout", "GpuLightRecordsMatchTheCppRecords")
{
	const auto parsed = ShaderCompiler::LoadSlangReflectionJson(SWIM_RHI_GPU_LIGHT_PROBE_REFLECTION_PATH);
	SWIM_REQUIRE_MESSAGE(parsed, parsed.Error);
	const ShaderCompiler::ShaderBindingReflection* lights = nullptr;
	const ShaderCompiler::ShaderBindingReflection* header = nullptr;
	const ShaderCompiler::ShaderBindingReflection* samples = nullptr;
	for (const auto& parameter : parsed.Reflection.GlobalParameters)
	{
		lights = parameter.Name == "Lights" ? &parameter : lights;
		header = parameter.Name == "Header" ? &parameter : header;
		samples = parameter.Name == "Samples" ? &parameter : samples;
	}
	SWIM_REQUIRE(lights != nullptr && header != nullptr && samples != nullptr);
	SWIM_CHECK_EQUAL(lights->ElementSize, std::uint32_t(sizeof(Render::GpuLightRecord)));
	SWIM_CHECK_EQUAL(header->ElementSize, std::uint32_t(sizeof(Render::GpuLightHeader)));
	SWIM_CHECK_EQUAL(samples->ElementSize, 64u);
	std::map<std::string, std::uint32_t> fields;
	for (const auto& field : lights->ElementFields)
	{
		fields[field.Name] = field.Offset;
	}
	SWIM_CHECK_EQUAL(fields.at("Position"), std::uint32_t(offsetof(Render::GpuLightRecord, Position)));
	SWIM_CHECK_EQUAL(fields.at("Range"), std::uint32_t(offsetof(Render::GpuLightRecord, Range)));
	SWIM_CHECK_EQUAL(fields.at("Direction"), std::uint32_t(offsetof(Render::GpuLightRecord, Direction)));
	SWIM_CHECK_EQUAL(fields.at("Type"), std::uint32_t(offsetof(Render::GpuLightRecord, Type)));
	SWIM_CHECK_EQUAL(fields.at("Color"), std::uint32_t(offsetof(Render::GpuLightRecord, Color)));
	SWIM_CHECK_EQUAL(fields.at("InverseRangeSquared"), std::uint32_t(offsetof(Render::GpuLightRecord, InverseRangeSquared)));
	SWIM_CHECK_EQUAL(fields.at("SpotScale"), std::uint32_t(offsetof(Render::GpuLightRecord, SpotScale)));
	SWIM_CHECK_EQUAL(fields.at("SpotOffset"), std::uint32_t(offsetof(Render::GpuLightRecord, SpotOffset)));
	SWIM_CHECK_EQUAL(fields.at("ShadowIndex"), std::uint32_t(offsetof(Render::GpuLightRecord, ShadowIndex)));
	SWIM_CHECK_EQUAL(fields.at("Flags"), std::uint32_t(offsetof(Render::GpuLightRecord, Flags)));
	std::map<std::string, std::uint32_t> headerFields;
	for (const auto& field : header->ElementFields)
	{
		headerFields[field.Name] = field.Offset;
	}
	SWIM_CHECK_EQUAL(headerFields.at("DirectionalCount"), std::uint32_t(offsetof(Render::GpuLightHeader, DirectionalCount)));
	SWIM_CHECK_EQUAL(headerFields.at("LocalCount"), std::uint32_t(offsetof(Render::GpuLightHeader, LocalCount)));
	SWIM_CHECK_EQUAL(headerFields.at("FirstLocalRow"), std::uint32_t(offsetof(Render::GpuLightHeader, FirstLocalRow)));

	const auto converted = ShaderCompiler::BuildRhiShaderInterface(parsed.Reflection);
	SWIM_REQUIRE_MESSAGE(converted, converted.Error);
	SWIM_CHECK((converted.Interface.ComputeThreadGroupSize == std::array<std::uint32_t, 3>{ 64, 1, 1 }));
	SWIM_REQUIRE_EQUAL(converted.Interface.PushConstants.size(), 1u);
	SWIM_CHECK_EQUAL(converted.Interface.PushConstants[0].Size, 16u);
	SWIM_REQUIRE_EQUAL(converted.Interface.DescriptorSchemas.size(), 1u);
	SWIM_CHECK_EQUAL(converted.Interface.DescriptorSchemas[0].Bindings.size(), std::size_t(4));
}
#endif
