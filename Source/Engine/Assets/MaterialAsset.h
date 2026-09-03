#pragma once

#include "Engine/Assets/AssetHandle.h"
#include "Engine/Assets/TextureAsset.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Swim::Assets
{

	enum class MaterialParameterType : std::uint8_t
	{
		Float,
		Float2,
		Float3,
		Float4
	};

	struct MaterialParameterDesc
	{
		std::string Name;
		MaterialParameterType Type = MaterialParameterType::Float;
		std::array<float, 4> DefaultValue{};
	};

	struct MaterialTemplateAsset
	{
		std::string ShaderFamily;
		std::uint64_t FeatureMask = 0;
		std::vector<MaterialParameterDesc> Parameters;
	};

	struct MaterialParameterValue
	{
		std::string Name;
		std::array<float, 4> Value{};
	};

	struct MaterialTextureBinding
	{
		std::string Name;
		AssetHandle<TextureAsset> Texture;
		AssetHandle<SamplerAsset> Sampler;
	};

	struct MaterialInstanceAsset
	{
		AssetHandle<MaterialTemplateAsset> Template;
		std::vector<MaterialParameterValue> Parameters;
		std::vector<MaterialTextureBinding> Textures;
	};

}
