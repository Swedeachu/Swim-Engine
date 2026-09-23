#include "Engine/Systems/Renderer/Materials/MaterialInstance.h"

#include <cstring>
#include <stdexcept>

namespace Swim::Render
{
	namespace
	{
		bool IsUint(MaterialParameterType type)
		{
			return type == MaterialParameterType::Uint;
		}

		bool IsTexture(MaterialParameterType type)
		{
			return type == MaterialParameterType::TextureIndex;
		}

		bool IsSampler(MaterialParameterType type)
		{
			return type == MaterialParameterType::SamplerIndex;
		}

		bool IsIndexOrUint(MaterialParameterType type)
		{
			return IsUint(type) || IsTexture(type) || IsSampler(type);
		}

		bool IsInt(MaterialParameterType type)
		{
			return type == MaterialParameterType::Int;
		}

		bool IsScalarFloat(MaterialParameterType type)
		{
			return type == MaterialParameterType::Float;
		}
	} // namespace

	MaterialInstance::MaterialInstance(std::shared_ptr<const MaterialTemplate> materialTemplate)
		: materialTemplate(std::move(materialTemplate))
	{
		if (!this->materialTemplate)
		{
			throw std::invalid_argument("Material instance needs a template");
		}
		const auto defaults = this->materialTemplate->GetDefaultRecord();
		record.assign(defaults.begin(), defaults.end());
	}

	const MaterialParameterDesc& MaterialInstance::Require(std::string_view parameter, bool (*accepts)(MaterialParameterType)) const
	{
		const auto* found = materialTemplate->FindParameter(parameter);
		if (!found)
		{
			throw std::invalid_argument(
				"Material template " + materialTemplate->GetName() + " has no parameter '" + std::string(parameter) + "'");
		}
		if (!accepts(found->Type))
		{
			throw std::invalid_argument("Material parameter '" + found->Name + "' is a " +
				std::string(MaterialParameterTypeName(found->Type)) + "; wrong accessor");
		}
		return *found;
	}

	void MaterialInstance::Changed(bool changed)
	{
		version += changed ? 1 : 0;
	}

	void MaterialInstance::SetFloat(std::string_view parameter, float value)
	{
		SetVector(parameter, { &value, 1 });
	}

	void MaterialInstance::SetVector(std::string_view parameter, std::span<const float> values)
	{
		const auto before = record;
		materialTemplate->Write(record, parameter, values);
		Changed(before != record);
	}

	void MaterialInstance::SetUint(std::string_view parameter, std::uint32_t value)
	{
		Require(parameter, &IsUint);
		const auto before = record;
		materialTemplate->Write(record, parameter, value);
		Changed(before != record);
	}

	void MaterialInstance::SetInt(std::string_view parameter, std::int32_t value)
	{
		const auto before = record;
		materialTemplate->Write(record, parameter, value);
		Changed(before != record);
	}

	void MaterialInstance::SetTexture(std::string_view parameter, std::uint32_t bindlessIndex)
	{
		Require(parameter, &IsTexture);
		const auto before = record;
		materialTemplate->Write(record, parameter, bindlessIndex);
		Changed(before != record);
	}

	void MaterialInstance::SetSampler(std::string_view parameter, std::uint32_t bindlessIndex)
	{
		Require(parameter, &IsSampler);
		const auto before = record;
		materialTemplate->Write(record, parameter, bindlessIndex);
		Changed(before != record);
	}

	float MaterialInstance::GetFloat(std::string_view parameter) const
	{
		const auto& target = Require(parameter, &IsScalarFloat);
		float value = 0.0f;
		std::memcpy(&value, record.data() + target.Offset, sizeof(value));
		return value;
	}

	std::array<float, 4> MaterialInstance::GetVector(std::string_view parameter) const
	{
		const auto& target = Require(parameter, &IsFloatParameter);
		std::array<float, 4> values{};
		std::memcpy(values.data(), record.data() + target.Offset, MaterialParameterSize(target.Type));
		return values;
	}

	std::uint32_t MaterialInstance::GetUint(std::string_view parameter) const
	{
		const auto& target = Require(parameter, &IsIndexOrUint);
		std::uint32_t value = 0;
		std::memcpy(&value, record.data() + target.Offset, sizeof(value));
		return value;
	}

	std::int32_t MaterialInstance::GetInt(std::string_view parameter) const
	{
		const auto& target = Require(parameter, &IsInt);
		std::int32_t value = 0;
		std::memcpy(&value, record.data() + target.Offset, sizeof(value));
		return value;
	}
} // namespace Swim::Render
