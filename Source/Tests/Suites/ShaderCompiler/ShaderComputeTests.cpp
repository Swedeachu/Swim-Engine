#include "Tools/ShaderCompiler/ShaderRhiInterface.h"
#include "Tests/Framework/Test.h"

using namespace Swim;

namespace
{
	ShaderCompiler::ShaderReflection ComputeReflection()
	{
		ShaderCompiler::ShaderReflection reflection;
		reflection.EntryPoints.push_back({ "computeMain", ShaderCompiler::ShaderStage::Compute, { 8, 4, 1 }, {} });
		ShaderCompiler::ShaderBindingReflection buffer;
		buffer.Name = "Output";
		buffer.BindingKind = "descriptorTableSlot";
		buffer.TypeKind = "resource";
		buffer.ResourceShape = "structuredBuffer";
		buffer.ResourceAccess = "readWrite";
		buffer.Index = 7;
		buffer.Space = 1;
		buffer.HasIndex = buffer.HasSpace = true;
		reflection.GlobalParameters.push_back(buffer);
		return reflection;
	}
}

SWIM_TEST("ShaderCompiler.Compute", "ReadWriteBuffersAndLocalSizeAreOwnedByConvertedInterface")
{
	auto reflection = ComputeReflection();
	for (const auto shape : { "structuredBuffer", "byteAddressBuffer" })
	{
		reflection.GlobalParameters[0].ResourceShape = shape;
		const auto converted = ShaderCompiler::BuildRhiShaderInterface(reflection);
		SWIM_REQUIRE_MESSAGE(converted, converted.Error);
		SWIM_CHECK((converted.Interface.ComputeThreadGroupSize == std::array<std::uint32_t, 3>{ 8, 4, 1 }));
		SWIM_REQUIRE_EQUAL(converted.Interface.DescriptorSchemas.size(), 1u);
		const auto& schema = converted.Interface.DescriptorSchemas[0];
		SWIM_CHECK_EQUAL(schema.Space, 1u);
		SWIM_REQUIRE_EQUAL(schema.Bindings.size(), 1u);
		SWIM_CHECK_EQUAL(schema.Bindings[0].Binding, 7u);
		SWIM_CHECK_EQUAL(schema.Bindings[0].Type, Rhi::DescriptorType::StorageBuffer);
		SWIM_CHECK_EQUAL(schema.Bindings[0].Stages, Rhi::ShaderStageMask::Compute);
	}
	reflection.GlobalParameters[0].ResourceAccess = "read";
	const auto converted = ShaderCompiler::BuildRhiShaderInterface(reflection);
	SWIM_REQUIRE(converted);
	SWIM_CHECK_EQUAL(converted.Interface.DescriptorSchemas[0].Bindings[0].Type, Rhi::DescriptorType::ReadOnlyStorageBuffer);
}

SWIM_TEST("ShaderCompiler.Compute", "MixedEntriesAndMissingFixedLocalSizeFailWithoutPartialOutput")
{
	for (std::size_t axis = 0; axis < 3; ++axis)
	{
		auto reflection = ComputeReflection();
		reflection.EntryPoints[0].ThreadGroupSize[axis] = 0;
		const auto converted = ShaderCompiler::BuildRhiShaderInterface(reflection);
		SWIM_CHECK(!converted);
		SWIM_CHECK(converted.Interface.DescriptorSchemas.empty());
		SWIM_CHECK((converted.Interface.ComputeThreadGroupSize == std::array<std::uint32_t, 3>{}));
	}
	for (auto stage : { ShaderCompiler::ShaderStage::Compute, ShaderCompiler::ShaderStage::Vertex })
	{
		auto reflection = ComputeReflection();
		reflection.EntryPoints.push_back({ "other", stage, { 1, 1, 1 }, {} });
		SWIM_CHECK(!ShaderCompiler::BuildRhiShaderInterface(reflection));
	}
}

SWIM_TEST("ShaderCompiler.Compute", "UnsupportedWritesAndGraphicsStoresStillReject")
{
	auto reflection = ComputeReflection();
	const auto valid = reflection;
	for (const auto access : { "append", "write", "unknown" })
	{
		reflection.GlobalParameters[0].ResourceAccess = access;
		SWIM_CHECK(!ShaderCompiler::BuildRhiShaderInterface(reflection));
	}
	reflection = valid;
	reflection.GlobalParameters[0].ResourceShape = "texture2D";
	SWIM_CHECK(!ShaderCompiler::BuildRhiShaderInterface(reflection));
	reflection = valid;
	reflection.GlobalParameters[0].ResourceArray = true;
	SWIM_CHECK(!ShaderCompiler::BuildRhiShaderInterface(reflection));
	reflection = valid;
	reflection.GlobalParameters[0].ResourceMultisample = true;
	SWIM_CHECK(!ShaderCompiler::BuildRhiShaderInterface(reflection));
	reflection = valid;
	reflection.EntryPoints[0].Stage = ShaderCompiler::ShaderStage::Fragment;
	SWIM_CHECK(!ShaderCompiler::BuildRhiShaderInterface(reflection));
	reflection = valid;
	reflection.EntryPoints[0].Parameters.push_back(reflection.GlobalParameters[0]);
	SWIM_CHECK(!ShaderCompiler::BuildRhiShaderInterface(reflection));
	reflection = valid;
	reflection.GlobalParameters.push_back(reflection.GlobalParameters[0]);
	const auto failed = ShaderCompiler::BuildRhiShaderInterface(reflection);
	SWIM_CHECK(!failed);
	SWIM_CHECK(failed.Interface.DescriptorSchemas.empty());
	SWIM_CHECK((failed.Interface.ComputeThreadGroupSize == std::array<std::uint32_t, 3>{}));
}

#ifdef SWIM_RHI_COMPUTE_REFLECTION_PATH
SWIM_TEST("ShaderCompiler.Compute", "CompiledSlangReflectionSuppliesBindingsConstantsAndFixedLocalSize")
{
	const auto parsed = ShaderCompiler::LoadSlangReflectionJson(SWIM_RHI_COMPUTE_REFLECTION_PATH);
	SWIM_REQUIRE_MESSAGE(parsed, parsed.Error);
	const auto converted = ShaderCompiler::BuildRhiShaderInterface(parsed.Reflection);
	SWIM_REQUIRE_MESSAGE(converted, converted.Error);
	SWIM_CHECK((converted.Interface.ComputeThreadGroupSize == std::array<std::uint32_t, 3>{ 8, 4, 1 }));
	SWIM_REQUIRE_EQUAL(converted.Interface.DescriptorSchemas.size(), 1u);
	const auto& schema = converted.Interface.DescriptorSchemas[0];
	SWIM_CHECK_EQUAL(schema.Space, 1u);
	SWIM_REQUIRE_EQUAL(schema.Bindings.size(), 2u);
	for (const auto& binding : schema.Bindings)
	{
		SWIM_CHECK_EQUAL(binding.Stages, Rhi::ShaderStageMask::Compute);
		SWIM_CHECK_EQUAL(binding.Type, binding.Binding == 3 ? Rhi::DescriptorType::ReadOnlyStorageBuffer : Rhi::DescriptorType::StorageBuffer);
	}
	SWIM_REQUIRE_EQUAL(converted.Interface.PushConstants.size(), 1u);
	SWIM_CHECK_EQUAL(converted.Interface.PushConstants[0].Offset, 0u);
	SWIM_CHECK_EQUAL(converted.Interface.PushConstants[0].Size, 24u);
	SWIM_CHECK_EQUAL(converted.Interface.PushConstants[0].Stages, Rhi::ShaderStageMask::Compute);
}
#endif

SWIM_TEST("ShaderCompiler.Compute", "MalformedLocalSizeIsNeverPartiallyAccepted")
{
	for (const auto size : { "[]", "[8,4]", "[8,4,1,1]", "[8,0,1]", "[8,-1,1]", "[8,4294967296,1]", "[8,\"4\",1]", "[8,1.5,1]", "null" })
	{
		const auto parsed = ShaderCompiler::ParseSlangReflectionJson(std::string("{\"entryPoints\":[{\"name\":\"computeMain\",\"stage\":\"compute\",\"threadGroupSize\":") + size + "}]}");
		SWIM_REQUIRE(parsed);
		SWIM_CHECK((parsed.Reflection.EntryPoints[0].ThreadGroupSize == std::array<std::uint32_t, 3>{}));
		SWIM_CHECK(!ShaderCompiler::BuildRhiShaderInterface(parsed.Reflection));
	}
}
