#include "Tools/ShaderCompiler/ShaderBindingReflection.h"
#include "Tools/ShaderCompiler/ShaderReflectionJson.h"

namespace Swim::ShaderCompiler::Detail
{

	ShaderBindingReflection ParseSlangBindingParameter(simdjson::dom::object parameter)
	{
		ShaderBindingReflection reflection;
		reflection.Name = ReadString(parameter, "name");
		reflection.SemanticName = ReadString(parameter, "semanticName");
		reflection.HasUnsupportedBindingLayout = FindField(parameter, "bindings").has_value();
		reflection.ResourceFormat = ReadString(parameter, "format");

		if (const auto bindingField = FindField(parameter, "binding"))
		{
			simdjson::dom::object binding;
			if (!bindingField->get_object().get(binding))
			{
				reflection.BindingKind = ReadString(binding, "kind");
				reflection.HasIndex = ReadU32(binding, "index", reflection.Index);
				reflection.HasSpace = ReadU32(binding, "space", reflection.Space);
				reflection.HasOffset = ReadU32(binding, "offset", reflection.Offset);
				reflection.HasSize = ReadU32(binding, "size", reflection.Size);
				if ((FindField(binding, "count") && !ReadU32(binding, "count", reflection.Count)) ||
					(FindField(binding, "space") && !reflection.HasSpace))
				{
					reflection.HasUnsupportedBindingLayout = true;
				}
			}
			else
			{
				reflection.HasUnsupportedBindingLayout = true;
			}
		}

		if (const auto typeField = FindField(parameter, "type"))
		{
			simdjson::dom::object type;
			if (!typeField->get_object().get(type))
			{
				reflection.TypeKind = ReadString(type, "kind");
				if (reflection.TypeKind == "array")
				{
					// A SPIR-V descriptor array occupies one binding. Its element
					// count lives on the array type, not the binding's slot count.
					if (reflection.BindingKind != "descriptorTableSlot" || reflection.Count != 1 ||
						!ReadU32(type, "elementCount", reflection.DescriptorArrayCount) || reflection.DescriptorArrayCount == 0)
					{
						reflection.HasUnsupportedBindingLayout = true;
					}
					const auto element = FindField(type, "elementType");
					simdjson::dom::object elementType;
					if (!element || element->get_object().get(elementType))
					{
						reflection.HasUnsupportedBindingLayout = true;
						return reflection;
					}
					reflection.DescriptorElementTypeKind = ReadString(elementType, "kind");
					if (reflection.DescriptorElementTypeKind == "array")
					{
						reflection.HasUnsupportedBindingLayout = true;
					}
					type = elementType;
				}
				if (reflection.BindingKind == "pushConstantBuffer")
				{
					reflection.HasOffset = reflection.HasSize = false;
					if (const auto element = FindField(type, "elementVarLayout"))
					{
						simdjson::dom::object elementLayout;
						if (!element->get_object().get(elementLayout) && !FindField(elementLayout, "bindings"))
						{
							if (const auto field = FindField(elementLayout, "binding"))
							{
								simdjson::dom::object binding;
								if (!field->get_object().get(binding) && ReadString(binding, "kind") == "uniform")
								{
									reflection.HasOffset = ReadU32(binding, "offset", reflection.Offset);
									reflection.HasSize = ReadU32(binding, "size", reflection.Size);
								}
							}
						}
					}
				}
				reflection.ResourceShape = ReadString(type, "baseShape");
				reflection.ResourceAccess = ReadString(type, "access");
				if (const auto array = FindField(type, "array"))
				{
					const auto error = array->get_bool().get(reflection.ResourceArray);
					reflection.HasUnsupportedBindingLayout = reflection.HasUnsupportedBindingLayout || error != simdjson::SUCCESS;
				}
				if (const auto multisample = FindField(type, "multisample"))
				{
					const auto error = multisample->get_bool().get(reflection.ResourceMultisample);
					reflection.HasUnsupportedBindingLayout = reflection.HasUnsupportedBindingLayout || error != simdjson::SUCCESS;
				}
				if (const auto result = FindField(type, "resultType"))
				{
					simdjson::dom::object resultType;
					if (!result->get_object().get(resultType))
					{
						reflection.ResourceScalarType = ReadString(resultType, "scalarType");
						const auto kind = ReadString(resultType, "kind");
						if (kind == "scalar")
						{
							reflection.ResourceComponentCount = 1;
						}
						else if (kind == "vector")
						{
							ReadU32(resultType, "elementCount", reflection.ResourceComponentCount);
						}
						if (const auto element = FindField(resultType, "elementType"))
						{
							simdjson::dom::object elementType;
							if (!element->get_object().get(elementType))
							{
								reflection.ResourceScalarType = ReadString(elementType, "scalarType");
							}
						}
					}
				}
			}
		}

		return reflection;
	}

} // namespace Swim::ShaderCompiler::Detail
