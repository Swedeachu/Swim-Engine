#include "Tools/ShaderCompiler/ShaderTweaks.h"

#include "Tools/ShaderCompiler/ShaderReflectionJson.h"

namespace Swim::ShaderCompiler
{

	namespace
	{

		using Detail::FindField;
		using Detail::ReadString;

		bool ReadNumber(simdjson::dom::element element, double& value)
		{
			if (!element.get_double().get(value))
			{
				return true;
			}

			std::int64_t integer = 0;

			if (!element.get_int64().get(integer))
			{
				value = static_cast<double>(integer);
				return true;
			}

			return false;
		}

		bool ReadTweak(simdjson::dom::object field, double& min, double& max)
		{
			const auto attributes = FindField(field, "userAttribs");
			simdjson::dom::array list;

			if (!attributes || attributes->get_array().get(list))
			{
				return false;
			}

			for (simdjson::dom::element attributeElement : list)
			{
				simdjson::dom::object attribute;
				simdjson::dom::array arguments;

				if (attributeElement.get_object().get(attribute) || ReadString(attribute, "name") != "Tweak")
				{
					continue;
				}

				const auto args = FindField(attribute, "arguments");

				if (!args || args->get_array().get(arguments) || arguments.size() < 2)
				{
					continue;
				}

				if (ReadNumber(arguments.at(0).value_unsafe(), min) && ReadNumber(arguments.at(1).value_unsafe(), max) && min <= max)
				{
					return true;
				}
			}

			return false;
		}

		void ReadFields(simdjson::dom::object type, std::uint32_t base, const std::string& prefix, std::vector<ShaderTweakReflection>& out,
			std::uint32_t depth)
		{
			const auto fieldsElement = FindField(type, "fields");
			simdjson::dom::array fields;

			if (depth > 8 || !fieldsElement || fieldsElement->get_array().get(fields))
			{
				return;
			}

			for (simdjson::dom::element fieldElement : fields)
			{
				simdjson::dom::object field;
				simdjson::dom::object binding;
				simdjson::dom::object fieldType;

				if (fieldElement.get_object().get(field))
				{
					continue;
				}

				const auto bindingElement = FindField(field, "binding");
				const auto typeElement = FindField(field, "type");
				std::uint32_t offset = 0;

				if (!bindingElement || bindingElement->get_object().get(binding) || !Detail::ReadU32(binding, "offset", offset) ||
					!typeElement || typeElement->get_object().get(fieldType))
				{
					continue;
				}

				const std::string name = prefix + ReadString(field, "name");
				const std::string kind = ReadString(fieldType, "kind");

				if (kind == "struct")
				{
					ReadFields(fieldType, base + offset, name + ".", out, depth + 1);
					continue;
				}

				const std::string scalar = ReadString(fieldType, "scalarType");
				double min = 0.0;
				double max = 1.0;

				if (kind == "scalar" && (scalar == "float32" || scalar == "int32" || scalar == "uint32") && ReadTweak(field, min, max))
				{
					out.push_back({ name, base + offset, scalar, min, max });
				}
			}
		}

		void ReadParameters(simdjson::dom::object owner, std::vector<ShaderTweakReflection>& out)
		{
			const auto parametersElement = FindField(owner, "parameters");
			simdjson::dom::array parameters;

			if (!parametersElement || parametersElement->get_array().get(parameters))
			{
				return;
			}

			for (simdjson::dom::element parameterElement : parameters)
			{
				simdjson::dom::object parameter;
				simdjson::dom::object binding;
				simdjson::dom::object type;
				simdjson::dom::object element;

				if (parameterElement.get_object().get(parameter))
				{
					continue;
				}

				const auto bindingElement = FindField(parameter, "binding");
				const auto typeElement = FindField(parameter, "type");

				if (!bindingElement || bindingElement->get_object().get(binding) || ReadString(binding, "kind") != "pushConstantBuffer" ||
					!typeElement || typeElement->get_object().get(type))
				{
					continue;
				}

				const auto elementType = FindField(type, "elementType");

				if (elementType && !elementType->get_object().get(element))
				{
					ReadFields(element, 0, {}, out, 0);
				}
				else
				{
					ReadFields(type, 0, {}, out, 0);
				}
			}
		}

	} // namespace

	std::vector<ShaderTweakReflection> FindSlangTweaks(std::string_view jsonText)
	{
		std::vector<ShaderTweakReflection> tweaks;
		simdjson::dom::parser parser;
		simdjson::padded_string padded(jsonText.data(), jsonText.size());
		simdjson::dom::element document;
		simdjson::dom::object root;

		if (parser.parse(padded).get(document) || document.get_object().get(root))
		{
			return tweaks;
		}

		ReadParameters(root, tweaks);

		const auto entryPoints = FindField(root, "entryPoints");
		simdjson::dom::array entries;

		if (entryPoints && !entryPoints->get_array().get(entries))
		{
			for (simdjson::dom::element entryElement : entries)
			{
				simdjson::dom::object entry;

				if (!entryElement.get_object().get(entry))
				{
					ReadParameters(entry, tweaks);
				}
			}
		}

		return tweaks;
	}

} // namespace Swim::ShaderCompiler
