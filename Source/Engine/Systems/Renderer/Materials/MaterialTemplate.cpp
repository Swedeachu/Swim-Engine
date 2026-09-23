#include "Engine/Systems/Renderer/Materials/MaterialTemplate.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace Swim::Render
{
	std::string_view MaterialParameterTypeName(MaterialParameterType type)
	{
		switch (type)
		{
		case MaterialParameterType::Float:
			return "float";
		case MaterialParameterType::Float2:
			return "float2";
		case MaterialParameterType::Float3:
			return "float3";
		case MaterialParameterType::Float4:
			return "float4";
		case MaterialParameterType::Uint:
			return "uint";
		case MaterialParameterType::Int:
			return "int";
		case MaterialParameterType::TextureIndex:
			return "texture index";
		case MaterialParameterType::SamplerIndex:
			return "sampler index";
		}
		return "unknown";
	}

	MaterialTemplate::MaterialTemplate(MaterialTemplateDesc desc)
		: name(std::move(desc.Name)), recordSize(desc.RecordSize), parameters(std::move(desc.Parameters))
	{
		if (name.empty())
		{
			throw std::invalid_argument("Material template needs a name");
		}
		if (recordSize == 0 || recordSize % 16 != 0)
		{
			throw std::invalid_argument("Material template " + name + " record size must be a nonzero multiple of 16");
		}
		auto sorted = parameters;
		std::sort(sorted.begin(), sorted.end(),
			[](const auto& a, const auto& b)
			{
				return a.Offset < b.Offset;
			});
		for (std::size_t i = 0; i < sorted.size(); ++i)
		{
			const auto& parameter = sorted[i];
			const auto size = MaterialParameterSize(parameter.Type);
			if (parameter.Name.empty() || parameter.Offset % MaterialParameterAlignment(parameter.Type) != 0 ||
				parameter.Offset > recordSize || size > recordSize - parameter.Offset)
			{
				throw std::invalid_argument(
					"Material template " + name + " parameter '" + parameter.Name + "' is unnamed, misaligned or outside the record");
			}
			if (i > 0 && sorted[i - 1].Offset + MaterialParameterSize(sorted[i - 1].Type) > parameter.Offset)
			{
				throw std::invalid_argument(
					"Material template " + name + " parameters '" + sorted[i - 1].Name + "' and '" + parameter.Name + "' overlap");
			}
			for (std::size_t j = 0; j < i; ++j)
			{
				if (sorted[j].Name == parameter.Name)
				{
					throw std::invalid_argument("Material template " + name + " declares parameter '" + parameter.Name + "' twice");
				}
			}
		}
		defaults.assign(recordSize, std::byte{ 0 });
	}

	const MaterialParameterDesc* MaterialTemplate::FindParameter(std::string_view parameter) const
	{
		for (const auto& candidate : parameters)
		{
			if (candidate.Name == parameter)
			{
				return &candidate;
			}
		}
		return nullptr;
	}

	const MaterialParameterDesc& MaterialTemplate::Require(std::string_view parameter) const
	{
		const auto* found = FindParameter(parameter);
		if (!found)
		{
			throw std::invalid_argument("Material template " + name + " has no parameter '" + std::string(parameter) + "'");
		}
		return *found;
	}

	void MaterialTemplate::Write(std::span<std::byte> record, std::string_view parameter, std::span<const float> values) const
	{
		const auto& target = Require(parameter);
		if (record.size() != recordSize || !IsFloatParameter(target.Type) || values.size() != MaterialParameterComponents(target.Type))
		{
			throw std::invalid_argument("Material parameter '" + target.Name + "' is a " +
				std::string(MaterialParameterTypeName(target.Type)) + "; " + std::to_string(values.size()) + " float(s) given");
		}
		std::memcpy(record.data() + target.Offset, values.data(), values.size_bytes());
	}

	void MaterialTemplate::Write(std::span<std::byte> record, std::string_view parameter, std::uint32_t value) const
	{
		const auto& target = Require(parameter);
		if (record.size() != recordSize ||
			(target.Type != MaterialParameterType::Uint && target.Type != MaterialParameterType::TextureIndex &&
				target.Type != MaterialParameterType::SamplerIndex))
		{
			throw std::invalid_argument("Material parameter '" + target.Name + "' is a " +
				std::string(MaterialParameterTypeName(target.Type)) + "; an unsigned value was given");
		}
		std::memcpy(record.data() + target.Offset, &value, sizeof(value));
	}

	void MaterialTemplate::Write(std::span<std::byte> record, std::string_view parameter, std::int32_t value) const
	{
		const auto& target = Require(parameter);
		if (record.size() != recordSize || target.Type != MaterialParameterType::Int)
		{
			throw std::invalid_argument("Material parameter '" + target.Name + "' is a " +
				std::string(MaterialParameterTypeName(target.Type)) + "; a signed value was given");
		}
		std::memcpy(record.data() + target.Offset, &value, sizeof(value));
	}

	void MaterialTemplate::SetDefault(std::string_view parameter, std::span<const float> values)
	{
		Write(defaults, parameter, values);
	}

	void MaterialTemplate::SetDefault(std::string_view parameter, std::uint32_t value)
	{
		Write(defaults, parameter, value);
	}

	void MaterialTemplate::SetDefault(std::string_view parameter, std::int32_t value)
	{
		Write(defaults, parameter, value);
	}
} // namespace Swim::Render
