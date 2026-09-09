#include "Tools/ShaderCompiler/ShaderRhiInterface.h"
#include "Tests/Framework/Test.h"

using namespace Swim;

namespace
{

	ShaderCompiler::ShaderBindingReflection Descriptor(std::uint32_t binding, std::uint32_t space = 0)
	{
		ShaderCompiler::ShaderBindingReflection result;
		result.Name = "Resource";
		result.BindingKind = "descriptorTableSlot";
		result.TypeKind = "samplerState";
		result.Index = binding;
		result.Space = space;
		result.HasIndex = result.HasSpace = true;
		return result;
	}

	ShaderCompiler::ShaderReflection Graphics()
	{
		ShaderCompiler::ShaderReflection result;
		result.EntryPoints.push_back({ "vertexMain", ShaderCompiler::ShaderStage::Vertex });
		result.EntryPoints.push_back({ "fragmentMain", ShaderCompiler::ShaderStage::Fragment });
		return result;
	}

	void RequireFailure(const ShaderCompiler::ShaderReflection& reflection)
	{
		const auto result = ShaderCompiler::BuildRhiShaderInterface(reflection);
		SWIM_REQUIRE(!result);
		SWIM_CHECK(!result.Error.empty());
		SWIM_CHECK(result.Interface.DescriptorSchemas.empty());
		SWIM_CHECK(result.Interface.PushConstants.empty());
		SWIM_CHECK((result.Interface.ComputeThreadGroupSize == std::array<std::uint32_t, 3>{}));
	}

} // namespace

SWIM_TEST("ShaderCompiler.ScopedDescriptors", "GlobalsAndEntryParametersKeepAbsoluteSlotsAndStageVisibility")
{
	auto reflection = Graphics();
	reflection.GlobalParameters.push_back(Descriptor(2));
	reflection.EntryPoints[0].Parameters.push_back(Descriptor(3, 1));
	reflection.EntryPoints[1].Parameters.push_back(Descriptor(7, 1));
	const auto result = ShaderCompiler::BuildRhiShaderInterface(reflection);
	SWIM_REQUIRE_MESSAGE(result, result.Error);
	SWIM_REQUIRE_EQUAL(result.Interface.DescriptorSchemas.size(), 2u);
	const auto& global = result.Interface.DescriptorSchemas[0];
	const auto& local = result.Interface.DescriptorSchemas[1];
	SWIM_CHECK_EQUAL(global.Space, 0u);
	SWIM_CHECK_EQUAL(global.Bindings[0].Binding, 2u);
	SWIM_CHECK_EQUAL(global.Bindings[0].Stages, Rhi::ShaderStageMask::Vertex | Rhi::ShaderStageMask::Fragment);
	SWIM_CHECK_EQUAL(local.Space, 1u);
	SWIM_REQUIRE_EQUAL(local.Bindings.size(), 2u);
	SWIM_CHECK_EQUAL(local.Bindings[0].Binding, 3u);
	SWIM_CHECK_EQUAL(local.Bindings[0].Stages, Rhi::ShaderStageMask::Vertex);
	SWIM_CHECK_EQUAL(local.Bindings[1].Binding, 7u);
	SWIM_CHECK_EQUAL(local.Bindings[1].Stages, Rhi::ShaderStageMask::Fragment);
}

SWIM_TEST("ShaderCompiler.ScopedDescriptors", "EveryDuplicateSlotRejectsEvenAcrossStagesAndWithMatchingTypes")
{
	for (int mode = 0; mode < 3; ++mode)
	{
		auto reflection = Graphics();
		auto binding = Descriptor(3, 1);
		reflection.EntryPoints[0].Parameters.push_back(binding);
		if (mode == 0)
		{
			reflection.GlobalParameters.push_back(binding);
		}
		else
		{
			reflection.EntryPoints[mode - 1].Parameters.push_back(binding);
		}
		RequireFailure(reflection);
	}
	auto reflection = Graphics();
	reflection.EntryPoints[0].Parameters.push_back(Descriptor(3, 1));
	reflection.EntryPoints[1].Parameters.push_back(Descriptor(3, 2));
	SWIM_CHECK(ShaderCompiler::BuildRhiShaderInterface(reflection));
}

SWIM_TEST("ShaderCompiler.ScopedDescriptors", "NestedScopeContainersAndMalformedBindingsCannotDisappear")
{
	for (const auto scope : {
		R"json({"kind":"constantBuffer","binding":{"kind":"pushConstantBuffer","index":0},"parameters":[]})json",
		R"json({"kind":"parameterBlock","parameters":[]})json",
		R"json({"kind":"none","binding":{"kind":"descriptorTableSlot","index":0},"parameters":[]})json",
		R"json({"kind":"none","bindings":[],"parameters":[]})json",
		R"json({"kind":"none","parameters":{}})json",
		R"json({"kind":"none","parameters":[3]})json",
		R"json(3)json" })
	{
		const auto parsed = ShaderCompiler::ParseSlangReflectionJson(std::string(R"json({"entryPoints":[{"name":"main","stage":"fragment","scope":)json") + scope + "}]}");
		SWIM_REQUIRE(parsed);
		RequireFailure(parsed.Reflection);
	}
	for (const auto binding : {
		R"json({"binding":{"kind":"descriptorTableSlot","index":0,"count":"2"}})json",
		R"json({"binding":{"kind":"descriptorTableSlot","index":0,"space":-1}})json",
		R"json({"binding":{"kind":"descriptorTableSlot","index":0,"space":4294967296}})json",
		R"json({"bindings":[{"kind":"descriptorTableSlot","index":0}]})json",
		R"json({"binding":3})json" })
	{
		auto parsed = ShaderCompiler::ParseSlangReflectionJson(std::string(R"json({"entryPoints":[{"name":"main","stage":"fragment","parameters":[)json") + binding + "]}]}");
		SWIM_REQUIRE(parsed);
		SWIM_REQUIRE_EQUAL(parsed.Reflection.EntryPoints[0].Parameters.size(), 1u);
		auto& parameter = parsed.Reflection.EntryPoints[0].Parameters[0];
		SWIM_CHECK(parameter.HasUnsupportedBindingLayout);
		// Make the resource type valid so malformed coordinates/counts cannot
		// hide behind a separate unsupported-type rejection.
		parameter.TypeKind = "samplerState";
		RequireFailure(parsed.Reflection);
	}
}

SWIM_TEST("ShaderCompiler.ScopedDescriptors", "StageIoIsIgnoredButUniformAndUnknownParametersReject")
{
	const auto parsed = ShaderCompiler::ParseSlangReflectionJson(R"json({"entryPoints":[{"name":"main","stage":"vertex","scope":{"kind":"none","parameters":[
		{"name":"id","semanticName":"SV_VERTEXID","type":{"kind":"scalar"}},
		{"name":"input","binding":{"kind":"varyingInput","index":0},"type":{"kind":"struct"}},
		{"name":"output","binding":{"kind":"varyingOutput","index":0},"type":{"kind":"vector"}}
	]}}]})json");
	SWIM_REQUIRE(parsed);
	SWIM_CHECK(ShaderCompiler::BuildRhiShaderInterface(parsed.Reflection));
	for (auto kind : { "uniform", "pushConstantBuffer", "unknown", "" })
	{
		auto reflection = parsed.Reflection;
		auto& parameter = reflection.EntryPoints[0].Parameters[0];
		parameter.SemanticName.clear();
		parameter.BindingKind = kind;
		RequireFailure(reflection);
	}
}

SWIM_TEST("ShaderCompiler.ScopedDescriptors", "ComputeDescriptorsReuseAllSupportedFlatResourceContracts")
{
	ShaderCompiler::ShaderReflection reflection;
	reflection.EntryPoints.push_back({ "computeMain", ShaderCompiler::ShaderStage::Compute, { 8, 1, 1 } });
	auto& parameters = reflection.EntryPoints[0].Parameters;
	parameters.push_back(Descriptor(0));
	auto buffer = Descriptor(1);
	buffer.TypeKind = "constantBuffer";
	parameters.push_back(buffer);
	buffer = Descriptor(2);
	buffer.TypeKind = "resource";
	buffer.ResourceShape = "byteAddressBuffer";
	parameters.push_back(buffer);
	buffer.Index = 3;
	buffer.ResourceAccess = "readWrite";
	parameters.push_back(buffer);
	auto image = Descriptor(4);
	image.TypeKind = "resource";
	image.ResourceShape = "texture2D";
	image.ResourceScalarType = "float32";
	image.ResourceComponentCount = 4;
	parameters.push_back(image);
	image.Index = 5;
	image.ResourceAccess = "readWrite";
	image.ResourceFormat = "rgba32f";
	image.ResourceComponentCount = 4;
	parameters.push_back(image);
	const auto result = ShaderCompiler::BuildRhiShaderInterface(reflection);
	SWIM_REQUIRE_MESSAGE(result, result.Error);
	const std::array types{ Rhi::DescriptorType::Sampler, Rhi::DescriptorType::UniformBuffer,
		Rhi::DescriptorType::ReadOnlyStorageBuffer, Rhi::DescriptorType::StorageBuffer,
		Rhi::DescriptorType::SampledTexture, Rhi::DescriptorType::StorageTexture };
	SWIM_REQUIRE_EQUAL(result.Interface.DescriptorSchemas.size(), 1u);
	const auto& bindings = result.Interface.DescriptorSchemas[0].Bindings;
	SWIM_REQUIRE_EQUAL(bindings.size(), types.size());
	for (std::size_t index = 0; index < types.size(); ++index)
	{
		SWIM_CHECK_EQUAL(bindings[index].Type, types[index]);
		SWIM_CHECK_EQUAL(bindings[index].Binding, index);
		SWIM_CHECK_EQUAL(bindings[index].Stages, Rhi::ShaderStageMask::Compute);
	}
	SWIM_CHECK_EQUAL(bindings.back().StorageTextureFormat, Rhi::Format::RGBA32Float);
	reflection.EntryPoints[0].Stage = ShaderCompiler::ShaderStage::Fragment;
	RequireFailure(reflection);
}

SWIM_TEST("ShaderCompiler.ScopedDescriptors", "UnsupportedResourcesDiscardGlobalPushConstantsAndEarlierDescriptors")
{
	for (const auto type : { "parameterBlock", "array", "unknown" })
	{
		auto reflection = Graphics();
		auto push = Descriptor(0);
		push.BindingKind = "pushConstantBuffer";
		push.TypeKind = "constantBuffer";
		push.HasOffset = push.HasSize = true;
		push.Size = 16;
		reflection.GlobalParameters.push_back(push);
		reflection.GlobalParameters.push_back(Descriptor(1));
		auto resource = Descriptor(2);
		resource.TypeKind = type;
		reflection.EntryPoints[1].Parameters.push_back(resource);
		RequireFailure(reflection);
	}
}

SWIM_TEST("ShaderCompiler.ScopedDescriptors", "DuplicateGraphicsStagesRejectBeforePublishingInterface")
{
	auto reflection = Graphics();
	reflection.EntryPoints.push_back(reflection.EntryPoints[0]);
	RequireFailure(reflection);
	reflection.EntryPoints.back() = reflection.EntryPoints[1];
	RequireFailure(reflection);
}

#ifdef SWIM_RHI_SCOPED_COMPUTE_REFLECTION_PATH
SWIM_TEST("ShaderCompiler.ScopedDescriptors", "PinnedSlangComputeCombinesGlobalsAndEntryDescriptorsOnce")
{
	const auto parsed = ShaderCompiler::LoadSlangReflectionJson(SWIM_RHI_SCOPED_COMPUTE_REFLECTION_PATH);
	SWIM_REQUIRE_MESSAGE(parsed, parsed.Error);
	SWIM_REQUIRE_EQUAL(parsed.Reflection.EntryPoints.size(), 1u);
	SWIM_CHECK_EQUAL(parsed.Reflection.EntryPoints[0].ScopeKind, std::string("none"));
	SWIM_REQUIRE_EQUAL(parsed.Reflection.EntryPoints[0].Parameters.size(), 3u);
	const auto result = ShaderCompiler::BuildRhiShaderInterface(parsed.Reflection);
	SWIM_REQUIRE_MESSAGE(result, result.Error);
	SWIM_REQUIRE_EQUAL(result.Interface.DescriptorSchemas.size(), 2u);
	SWIM_REQUIRE_EQUAL(result.Interface.PushConstants.size(), 1u);
	SWIM_CHECK_EQUAL(result.Interface.PushConstants[0].Size, 16u);
	SWIM_CHECK((result.Interface.ComputeThreadGroupSize == std::array<std::uint32_t, 3>{ 8, 1, 1 }));
	const auto& local = result.Interface.DescriptorSchemas[1];
	SWIM_CHECK_EQUAL(local.Space, 1u);
	SWIM_REQUIRE_EQUAL(local.Bindings.size(), 2u);
	SWIM_CHECK_EQUAL(local.Bindings[0].Binding, 5u);
	SWIM_CHECK_EQUAL(local.Bindings[0].Type, Rhi::DescriptorType::StorageBuffer);
	SWIM_CHECK_EQUAL(local.Bindings[1].Binding, 9u);
	SWIM_CHECK_EQUAL(local.Bindings[1].Type, Rhi::DescriptorType::UniformBuffer);
}
#endif

#ifdef SWIM_RHI_SCOPED_GRAPHICS_REFLECTION_PATH
SWIM_TEST("ShaderCompiler.ScopedDescriptors", "PinnedSlangGraphicsKeepsSharedVertexAndFragmentVisibility")
{
	const auto parsed = ShaderCompiler::LoadSlangReflectionJson(SWIM_RHI_SCOPED_GRAPHICS_REFLECTION_PATH);
	SWIM_REQUIRE_MESSAGE(parsed, parsed.Error);
	const auto result = ShaderCompiler::BuildRhiShaderInterface(parsed.Reflection);
	SWIM_REQUIRE_MESSAGE(result, result.Error);
	SWIM_REQUIRE_EQUAL(result.Interface.DescriptorSchemas.size(), 2u);
	SWIM_CHECK_EQUAL(result.Interface.DescriptorSchemas[0].Bindings[0].Stages,
		Rhi::ShaderStageMask::Vertex | Rhi::ShaderStageMask::Fragment);
	const auto& local = result.Interface.DescriptorSchemas[1];
	SWIM_CHECK_EQUAL(local.Space, 1u);
	SWIM_REQUIRE_EQUAL(local.Bindings.size(), 3u);
	SWIM_CHECK_EQUAL(local.Bindings[0].Binding, 3u);
	SWIM_CHECK_EQUAL(local.Bindings[0].Stages, Rhi::ShaderStageMask::Vertex);
	SWIM_CHECK_EQUAL(local.Bindings[1].Binding, 7u);
	SWIM_CHECK_EQUAL(local.Bindings[1].Type, Rhi::DescriptorType::SampledTexture);
	SWIM_CHECK_EQUAL(local.Bindings[2].Binding, 9u);
	SWIM_CHECK_EQUAL(local.Bindings[2].Type, Rhi::DescriptorType::Sampler);
	SWIM_CHECK_EQUAL(local.Bindings[1].Stages, Rhi::ShaderStageMask::Fragment);
	SWIM_CHECK_EQUAL(local.Bindings[2].Stages, Rhi::ShaderStageMask::Fragment);
}
#endif
