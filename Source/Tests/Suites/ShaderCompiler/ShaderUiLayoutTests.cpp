#include "Tests/Framework/Test.h"

#if defined(SWIM_UI_QUAD_REFLECTION_PATH)
#include "Engine/Systems/Renderer/UiRendering/UiRenderBindings.h"
#include "Engine/Systems/Renderer/UiRendering/UiRenderRecords.h"
#include "Tools/ShaderCompiler/ShaderRhiInterface.h"

#include <cstddef>
#include <map>
#include <string>

using namespace Swim;

// SwimUiQuad (item 79) declares the quad buffer at UiRenderBindings::Quads with the
// GpuUiQuad layout, the 16-byte draw constants for both stages, and the shared bindless
// space as runtime-sized arrays.
SWIM_TEST("ShaderCompiler.UiLayout", "QuadProgramMatchesTheBindingContractAndRecords")
{
	auto parsed = ShaderCompiler::LoadSlangReflectionJson(SWIM_UI_QUAD_REFLECTION_PATH);
	SWIM_REQUIRE_MESSAGE(parsed, parsed.Error);
	auto converted = ShaderCompiler::BuildRhiShaderInterface(parsed.Reflection);
	SWIM_REQUIRE_MESSAGE(converted, converted.Error);
	const auto& program = converted.Interface;
	using R = Render::UiRenderBindings;

	const Rhi::DescriptorSchemaDesc* space0 = nullptr;
	const Rhi::DescriptorSchemaDesc* bindless = nullptr;
	for (const auto& schema : program.DescriptorSchemas)
	{
		space0 = schema.Space == 0 ? &schema : space0;
		bindless = schema.Space == R::BindlessSpace ? &schema : bindless;
	}
	SWIM_REQUIRE(space0 != nullptr && bindless != nullptr);
	SWIM_REQUIRE_EQUAL(space0->Bindings.size(), std::size_t(1));
	SWIM_CHECK_EQUAL(space0->Bindings[0].Binding, R::Quads);
	SWIM_CHECK(space0->Bindings[0].Type == Rhi::DescriptorType::ReadOnlyStorageBuffer);
	SWIM_REQUIRE_EQUAL(bindless->Bindings.size(), std::size_t(2));
	for (const auto& binding : bindless->Bindings)
	{
		SWIM_CHECK(binding.Count == 0u); // Runtime-sized.
		SWIM_CHECK((binding.Binding == R::BindlessSamplers && binding.Type == Rhi::DescriptorType::Sampler) ||
			(binding.Binding == R::BindlessTextures && binding.Type == Rhi::DescriptorType::SampledTexture));
	}
	SWIM_REQUIRE_EQUAL(program.PushConstants.size(), std::size_t(1));
	SWIM_CHECK_EQUAL(program.PushConstants[0].Size, R::PushConstantBytes);
	SWIM_CHECK(program.PushConstants[0].Stages == (Rhi::ShaderStageMask::Vertex | Rhi::ShaderStageMask::Fragment));

	const ShaderCompiler::ShaderBindingReflection* quads = nullptr;
	for (const auto& parameter : parsed.Reflection.GlobalParameters)
	{
		quads = parameter.Name == "Quads" ? &parameter : quads;
	}
	SWIM_REQUIRE(quads != nullptr);
	SWIM_CHECK_EQUAL(quads->ElementSize, std::uint32_t(sizeof(Render::GpuUiQuad)));
	std::map<std::string, std::uint32_t> offsets;
	for (const auto& field : quads->ElementFields)
	{
		offsets[field.Name] = field.Offset;
	}
	using Q = Render::GpuUiQuad;
	SWIM_CHECK_EQUAL(offsets.at("Clip"), std::uint32_t(offsetof(Q, Clip)));
	SWIM_CHECK_EQUAL(offsets.at("Uv"), std::uint32_t(offsetof(Q, Uv)));
	SWIM_CHECK_EQUAL(offsets.at("Color"), std::uint32_t(offsetof(Q, Color)));
	SWIM_CHECK_EQUAL(offsets.at("BorderColor"), std::uint32_t(offsetof(Q, BorderColor)));
	SWIM_CHECK_EQUAL(offsets.at("Radius"), std::uint32_t(offsetof(Q, Radius)));
	SWIM_CHECK_EQUAL(offsets.at("PixelRange"), std::uint32_t(offsetof(Q, PixelRange)));
	SWIM_CHECK_EQUAL(offsets.at("Kind"), std::uint32_t(offsetof(Q, Kind)));
	SWIM_CHECK_EQUAL(offsets.at("Texture"), std::uint32_t(offsetof(Q, Texture)));
	SWIM_CHECK_EQUAL(offsets.at("Sampler"), std::uint32_t(offsetof(Q, Sampler)));
}
#endif
