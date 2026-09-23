#include "Tools/ShaderCompiler/ShaderBindingReflection.h"
#include "Tools/ShaderCompiler/ShaderReflectionJson.h"

namespace Swim::ShaderCompiler::Detail
{
	namespace
	{
		std::uint32_t ReadUniformSize(simdjson::dom::object type)
		{
			const auto sizes = FindField(type, "sizes");
			simdjson::dom::array array;
			if (!sizes || sizes->get_array().get(array))
			{
				return 0;
			}
			for (auto entry : array)
			{
				simdjson::dom::object size;
				std::uint32_t value = 0;
				if (!entry.get_object().get(size) && ReadString(size, "kind") == "uniform" && ReadU32(size, "value", value))
				{
					return value;
				}
			}
			return 0;
		}

		// Flattens struct fields with absolute offsets; false on malformed layouts.
		bool ReadStructFields(simdjson::dom::object type, const std::string& prefix, std::uint32_t base,
			std::vector<ShaderUniformReflection>& fields, std::uint32_t depth)
		{
			const auto list = FindField(type, "fields");
			simdjson::dom::array array;
			if (depth > 16 || !list || list->get_array().get(array))
			{
				return false;
			}
			for (auto entry : array)
			{
				simdjson::dom::object field;
				simdjson::dom::object binding;
				simdjson::dom::object fieldType;
				std::uint32_t offset = 0;
				std::uint32_t size = 0;
				if (entry.get_object().get(field) || !FindField(field, "binding") ||
					FindField(field, "binding")->get_object().get(binding) || ReadString(binding, "kind") != "uniform" ||
					!ReadU32(binding, "offset", offset) || !ReadU32(binding, "size", size) || !FindField(field, "type") ||
					FindField(field, "type")->get_object().get(fieldType) || offset > UINT32_MAX - base)
				{
					return false;
				}
				const auto name = prefix + ReadString(field, "name");
				if (ReadString(fieldType, "kind") == "struct")
				{
					if (!ReadStructFields(fieldType, name + ".", base + offset, fields, depth + 1))
					{
						return false;
					}
					continue;
				}
				ShaderUniformReflection leaf{ name, base + offset, size, {}, 0 };
				const auto kind = ReadString(fieldType, "kind");
				if (kind == "scalar")
				{
					leaf.ScalarType = ReadString(fieldType, "scalarType");
					leaf.ComponentCount = 1;
				}
				else if (kind == "vector")
				{
					simdjson::dom::object elementType;
					std::uint32_t count = 0;
					if (FindField(fieldType, "elementType") && !FindField(fieldType, "elementType")->get_object().get(elementType) &&
						ReadU32(fieldType, "elementCount", count) && ReadString(elementType, "kind") == "scalar")
					{
						leaf.ScalarType = ReadString(elementType, "scalarType");
						leaf.ComponentCount = count;
					}
				}
				fields.push_back(std::move(leaf));
			}
			return true;
		}
	} // namespace

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
						!ReadU32(type, "elementCount", reflection.DescriptorArrayCount))
					{
						reflection.HasUnsupportedBindingLayout = true;
					}
					// Slang reports unbounded (runtime-sized) arrays with elementCount 0.
					reflection.DescriptorArrayRuntimeSized =
						!reflection.HasUnsupportedBindingLayout && reflection.DescriptorArrayCount == 0;
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
						if (reflection.ResourceShape == "structuredBuffer" && ReadString(resultType, "kind") == "struct")
						{
							reflection.ElementSize = ReadUniformSize(resultType);
							if (!ReadStructFields(resultType, {}, 0, reflection.ElementFields, 0))
							{
								reflection.ElementFields.clear(); // Layout metadata only; the binding itself stays valid.
								reflection.ElementSize = 0;
							}
						}
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
