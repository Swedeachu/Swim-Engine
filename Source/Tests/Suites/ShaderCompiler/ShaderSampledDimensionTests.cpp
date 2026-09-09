#include "Tests/Framework/Test.h"
#include "Tools/ShaderCompiler/ShaderRhiInterface.h"

#include <algorithm>
#include <fstream>

using namespace Swim;

SWIM_TEST("ShaderCompiler.SampledDimensions", "ShapesClassesScopesAndDescriptorArraysRemainIndependent")
{
	struct Shape
	{
		const char* Name;
		bool Array;
		Rhi::TextureViewDimension Dimension;
	};
	using D = Rhi::TextureViewDimension;
	const std::array shapes{ Shape{ "texture1D", false, D::Texture1D }, Shape{ "texture1D", true, D::Texture1DArray },
		Shape{ "texture2D", false, D::Texture2D }, Shape{ "texture2D", true, D::Texture2DArray },
		Shape{ "texture3D", false, D::Texture3D }, Shape{ "textureCube", false, D::TextureCube },
		Shape{ "textureCube", true, D::TextureCubeArray } };
	const std::array scalars{ "float32", "uint32", "int32" };
	const std::array classes{ Rhi::SampledTextureClass::Float, Rhi::SampledTextureClass::Uint, Rhi::SampledTextureClass::Sint };
	for (const auto& shape : shapes)
	{
		for (std::size_t scalar = 0; scalar < scalars.size(); ++scalar)
		{
			for (bool local : { false, true })
			{
				for (bool descriptors : { false, true })
				{
					const std::string resource = "{\"kind\":\"resource\",\"baseShape\":\"" + std::string(shape.Name) +
						"\",\"array\":" + (shape.Array ? "true" : "false") +
						",\"resultType\":{\"kind\":\"scalar\",\"scalarType\":\"" + scalars[scalar] + "\"}}";
					const auto type = descriptors ? "{\"kind\":\"array\",\"elementCount\":3,\"elementType\":" + resource + "}" : resource;
					const auto parameter = "{\"name\":\"image\",\"binding\":{\"kind\":\"descriptorTableSlot\",\"index\":2},\"type\":" + type + "}";
					const auto parsed = ShaderCompiler::ParseSlangReflectionJson("{\"parameters\":[" + (local ? std::string{} : parameter) +
						"],\"entryPoints\":[{\"name\":\"computeMain\",\"stage\":\"compute\",\"threadGroupSize\":[4,1,1],\"parameters\":[" +
						(local ? parameter : std::string{}) + "]}]}");
					SWIM_REQUIRE_MESSAGE(parsed, parsed.Error);
					const auto converted = ShaderCompiler::BuildRhiShaderInterface(parsed.Reflection);
					SWIM_REQUIRE_MESSAGE(converted, converted.Error);
					SWIM_REQUIRE_EQUAL(converted.Interface.DescriptorSchemas.size(), 1u);
					const auto& binding = converted.Interface.DescriptorSchemas[0].Bindings.at(0);
					SWIM_CHECK_EQUAL(binding.SampledDimension, shape.Dimension);
					SWIM_CHECK_EQUAL(binding.SampledClass, classes[scalar]);
					SWIM_CHECK_EQUAL(binding.Count, descriptors ? 3u : 1u);
					SWIM_CHECK_EQUAL(binding.Stages, Rhi::ShaderStageMask::Compute);
					SWIM_CHECK_EQUAL(binding.Type, Rhi::DescriptorType::SampledTexture);
				}
			}
		}
	}
}

#ifdef SWIM_RHI_SAMPLED_DIMENSIONS_REFLECTION_PATH
SWIM_TEST("ShaderCompiler.SampledDimensions", "PinnedSlangVariantsPreserveViewShapesAndLocalParameter")
{
	using D = Rhi::TextureViewDimension;
	const std::array dimensions{ D::Texture1D, D::Texture1DArray, D::Texture2DArray, D::Texture3D,
		D::TextureCube, D::Texture2D, D::TextureCubeArray };
	for (bool cubes : { false, true })
	{
		const auto parsed = ShaderCompiler::LoadSlangReflectionJson(cubes ?
			SWIM_RHI_SAMPLED_CUBE_ARRAY_REFLECTION_PATH : SWIM_RHI_SAMPLED_DIMENSIONS_REFLECTION_PATH);
		SWIM_REQUIRE_MESSAGE(parsed, parsed.Error);
		SWIM_REQUIRE_EQUAL(parsed.Reflection.EntryPoints.size(), 1u);
		SWIM_CHECK_EQUAL(parsed.Reflection.EntryPoints[0].Name, std::string("computeMain"));
		SWIM_REQUIRE_EQUAL(parsed.Reflection.EntryPoints[0].Parameters.size(), 2u);
		const auto converted = ShaderCompiler::BuildRhiShaderInterface(parsed.Reflection);
		SWIM_REQUIRE_MESSAGE(converted, converted.Error);
		SWIM_REQUIRE_EQUAL(converted.Interface.DescriptorSchemas.size(), 1u);
		const auto& bindings = converted.Interface.DescriptorSchemas[0].Bindings;
		SWIM_REQUIRE_EQUAL(bindings.size(), cubes ? 9u : 8u);
		for (std::size_t index = 0; index < (cubes ? 7u : 6u); ++index)
		{
			const auto binding = std::find_if(bindings.begin(), bindings.end(), [index](const auto& item) { return item.Binding == index; });
			SWIM_REQUIRE(binding != bindings.end());
			SWIM_CHECK_EQUAL(binding->SampledDimension, dimensions[index]);
			SWIM_CHECK_EQUAL(binding->Count, 1u);
			SWIM_CHECK_EQUAL(binding->SampledClass, index == 4 || index == 6 ? Rhi::SampledTextureClass::Float : Rhi::SampledTextureClass::Uint);
		}
	}
}
SWIM_TEST("ShaderCompiler.SampledDimensions", "SpirvDeclaresCubeArrayCapabilityOnlyInOptionalVariant")
{
	for (bool cubes : { false, true })
	{
		std::ifstream file(cubes ? SWIM_RHI_SAMPLED_CUBE_ARRAY_SPIRV_PATH : SWIM_RHI_SAMPLED_DIMENSIONS_SPIRV_PATH,
			std::ios::binary | std::ios::ate);
		SWIM_REQUIRE(file);
		const auto size = file.tellg();
		SWIM_REQUIRE(size >= 20 && size % 4 == 0);
		std::vector<std::uint32_t> words(static_cast<std::size_t>(size) / 4);
		file.seekg(0);
		file.read(reinterpret_cast<char*>(words.data()), static_cast<std::streamsize>(size));
		SWIM_REQUIRE(file);
		SWIM_REQUIRE_EQUAL(words[0], 0x07230203u);
		bool valid = true;
		bool cubeCapability = false;
		bool cubeType = false;
		bool lineCapability = false;
		bool entryName = false;
		for (std::size_t offset = 5; offset < words.size();)
		{
			const auto count = words[offset] >> 16;
			const auto opcode = words[offset] & 0xFFFFu;
			if (count == 0 || count > words.size() - offset)
			{
				valid = false;
				break;
			}
			if (opcode == 17 && count == 2) // OpCapability
			{
				cubeCapability |= words[offset + 1] == 45; // SampledCubeArray
				lineCapability |= words[offset + 1] == 43; // Sampled1D
			}
			if (opcode == 25 && count >= 9) // OpTypeImage: Dim=Cube, Arrayed=1, Sampled=1
			{
				cubeType |= words[offset + 3] == 3 && words[offset + 5] == 1 && words[offset + 7] == 1;
			}
			if (opcode == 15 && count >= 4) // OpEntryPoint
			{
				const auto* begin = reinterpret_cast<const char*>(words.data() + offset + 3);
				const auto* end = begin + (count - 3) * 4;
				entryName |= std::string_view(begin, static_cast<std::size_t>(std::find(begin, end, '\0') - begin)) == "computeMain";
			}
			offset += count;
		}
		SWIM_CHECK(valid);
		SWIM_CHECK(entryName);
		SWIM_CHECK(lineCapability);
		SWIM_CHECK_EQUAL(cubeCapability, cubes);
		SWIM_CHECK_EQUAL(cubeType, cubes);
	}
}
#endif
