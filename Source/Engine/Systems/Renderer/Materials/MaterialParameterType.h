#pragma once
#include <cstdint>
#include <string_view>

namespace Swim::Render
{
	// Types a material parameter record may contain (one std430 struct per material).
	// Texture and sampler parameters are uint fields holding BindlessResourceTable
	// indices; reflection marks them by name (a "Texture" or "Sampler" suffix).
	enum class MaterialParameterType : std::uint8_t
	{
		Float,
		Float2,
		Float3,
		Float4,
		Uint,
		Int,
		TextureIndex,
		SamplerIndex,
	};

	constexpr std::uint32_t MaterialParameterComponents(MaterialParameterType type)
	{
		switch (type)
		{
		case MaterialParameterType::Float2:
			return 2;
		case MaterialParameterType::Float3:
			return 3;
		case MaterialParameterType::Float4:
			return 4;
		default:
			return 1;
		}
	}

	constexpr std::uint32_t MaterialParameterSize(MaterialParameterType type)
	{
		return MaterialParameterComponents(type) * 4;
	}

	// std430 alignment: scalars 4, two-component vectors 8, three/four-component vectors 16.
	constexpr std::uint32_t MaterialParameterAlignment(MaterialParameterType type)
	{
		switch (type)
		{
		case MaterialParameterType::Float2:
			return 8;
		case MaterialParameterType::Float3:
		case MaterialParameterType::Float4:
			return 16;
		default:
			return 4;
		}
	}

	constexpr bool IsFloatParameter(MaterialParameterType type)
	{
		return type == MaterialParameterType::Float || type == MaterialParameterType::Float2 || type == MaterialParameterType::Float3 ||
			type == MaterialParameterType::Float4;
	}

	std::string_view MaterialParameterTypeName(MaterialParameterType type);
} // namespace Swim::Render
