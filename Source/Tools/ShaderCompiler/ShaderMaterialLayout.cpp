#include "Tools/ShaderCompiler/ShaderMaterialLayout.h"

namespace Swim::ShaderCompiler
{
	namespace
	{
		const ShaderBindingReflection* FindBinding(const ShaderReflection& reflection, std::string_view name)
		{
			for (const auto& parameter : reflection.GlobalParameters)
			{
				if (parameter.Name == name)
				{
					return &parameter;
				}
			}
			for (const auto& entry : reflection.EntryPoints)
			{
				for (const auto& parameter : entry.Parameters)
				{
					if (parameter.Name == name)
					{
						return &parameter;
					}
				}
			}
			return nullptr;
		}

		bool EndsWith(std::string_view text, std::string_view suffix)
		{
			return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
		}

		bool ToParameterType(const ShaderUniformReflection& field, Render::MaterialParameterType& type)
		{
			using T = Render::MaterialParameterType;
			if (field.ScalarType == "float32" && field.ComponentCount >= 1 && field.ComponentCount <= 4)
			{
				constexpr T floats[] = { T::Float, T::Float2, T::Float3, T::Float4 };
				type = floats[field.ComponentCount - 1];
				return true;
			}
			if (field.ComponentCount != 1)
			{
				return false;
			}
			if (field.ScalarType == "int32")
			{
				type = T::Int;
				return true;
			}
			if (field.ScalarType == "uint32")
			{
				type = EndsWith(field.Name, "Texture") ? T::TextureIndex : EndsWith(field.Name, "Sampler") ? T::SamplerIndex : T::Uint;
				return true;
			}
			return false;
		}
	} // namespace

	ShaderMaterialLayoutResult BuildMaterialTemplateDesc(
		const ShaderReflection& reflection, std::string_view bindingName, std::string templateName)
	{
		ShaderMaterialLayoutResult result;
		const auto* binding = FindBinding(reflection, bindingName);
		if (!binding)
		{
			result.Error = "Material binding '" + std::string(bindingName) + "' is not in the reflection";
			return result;
		}
		if (binding->ElementFields.empty() || binding->ElementSize == 0)
		{
			result.Error = "Material binding '" + std::string(bindingName) + "' is not a structured buffer of a struct";
			return result;
		}
		result.Desc.Name = std::move(templateName);
		result.Desc.RecordSize = binding->ElementSize;
		for (const auto& field : binding->ElementFields)
		{
			Render::MaterialParameterDesc parameter;
			parameter.Name = field.Name;
			parameter.Offset = field.Offset;
			if (!ToParameterType(field, parameter.Type) || Render::MaterialParameterSize(parameter.Type) != field.Size)
			{
				result.Error = "Material field '" + field.Name + "' has an unsupported type (" +
					(field.ScalarType.empty() ? std::string("matrix/array") : field.ScalarType) + " x" +
					std::to_string(field.ComponentCount) + ")";
				result.Desc = {};
				return result;
			}
			result.Desc.Parameters.push_back(std::move(parameter));
		}
		return result;
	}
} // namespace Swim::ShaderCompiler
