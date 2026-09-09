#include "Tests/Framework/Test.h"
#include "Tools/ShaderCompiler/ShaderRhiInterface.h"

#ifdef SWIM_RHI_DEPTH_SAMPLING_REFLECTION_PATH
SWIM_TEST("ShaderCompiler.DepthSampling", "PinnedComparisonSamplerArraySharesTheReflectedSamplerContract")
{
	using namespace Swim;
	const auto parsed = ShaderCompiler::LoadSlangReflectionJson(SWIM_RHI_DEPTH_SAMPLING_REFLECTION_PATH);
	SWIM_REQUIRE_MESSAGE(parsed, parsed.Error);
	const auto converted = ShaderCompiler::BuildRhiShaderInterface(parsed.Reflection);
	SWIM_REQUIRE_MESSAGE(converted, converted.Error);
	SWIM_REQUIRE_EQUAL(converted.Interface.DescriptorSchemas.size(), 1u);
	const auto& bindings = converted.Interface.DescriptorSchemas[0].Bindings;
	SWIM_REQUIRE_EQUAL(bindings.size(), 5u);
	SWIM_CHECK_EQUAL(bindings[0].SampledClass, Rhi::SampledTextureClass::Float);
	SWIM_CHECK_EQUAL(bindings[1].SampledDimension, Rhi::TextureViewDimension::Texture2D);
	SWIM_CHECK_EQUAL(bindings[2].Type, Rhi::DescriptorType::Sampler);
	SWIM_CHECK_EQUAL(bindings[2].Count, 2u);
	SWIM_CHECK_EQUAL(bindings[3].Type, Rhi::DescriptorType::Sampler);
	SWIM_CHECK_EQUAL(bindings[3].Count, 1u);
	SWIM_CHECK_EQUAL(bindings[4].Type, Rhi::DescriptorType::StorageBuffer);
	// Slang JSON intentionally supplies no comparison-vs-regular sampler flag.
	// The application configures SamplerDesc for the shader operation it uses.
	SWIM_CHECK_EQUAL(parsed.Reflection.EntryPoints[0].Name, std::string("computeMain"));
}
#endif
