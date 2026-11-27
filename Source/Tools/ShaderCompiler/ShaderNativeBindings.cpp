#include "Tools/ShaderCompiler/ShaderNativeBindings.h"

#include "Tools/ShaderCompiler/ShaderReflectionJson.h"

#include <algorithm>
#include <fstream>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <unordered_map>

namespace Swim::ShaderCompiler
{

	namespace
	{

		std::optional<Rhi::NativeRegisterClass> RegisterClass(std::string_view kind)
		{
			if (kind == "constantBuffer")
			{
				return Rhi::NativeRegisterClass::ConstantBuffer;
			}

			if (kind == "shaderResource")
			{
				return Rhi::NativeRegisterClass::ShaderResource;
			}

			if (kind == "unorderedAccess")
			{
				return Rhi::NativeRegisterClass::UnorderedAccess;
			}

			if (kind == "samplerState")
			{
				return Rhi::NativeRegisterClass::Sampler;
			}

			return std::nullopt;
		}

		// Bytes per element of a structured buffer; zero for every other resource.
		std::uint32_t StructureStride(const ShaderBindingReflection& parameter)
		{
			if (parameter.ResourceShape != "structuredBuffer")
			{
				return 0;
			}

			if (parameter.ElementSize != 0)
			{
				return parameter.ElementSize;
			}

			const auto& scalar = parameter.ResourceScalarType;
			const std::uint32_t bytes = scalar.ends_with("16") ? 2u : scalar.ends_with("64") ? 8u : 4u;
			return bytes * std::max(parameter.ResourceComponentCount, 1u);
		}

		// Adds the varying inputs of one parameter or field; locations of struct fields
		// are relative to their parent's.
		bool AddVertexInputs(simdjson::dom::object parameter, std::uint32_t base, ShaderNativeVertexInputsResult& result)
		{
			const auto bindingElement = Detail::FindField(parameter, "binding");
			simdjson::dom::object binding;

			if (!bindingElement || bindingElement->get_object().get(binding) || Detail::ReadString(binding, "kind") != "varyingInput")
			{
				return true; // Not a vertex input.
			}

			std::uint32_t location = 0;
			Detail::ReadU32(binding, "index", location);
			location += base;
			const auto typeElement = Detail::FindField(parameter, "type");
			simdjson::dom::object type;
			simdjson::dom::array fields;

			if (typeElement && !typeElement->get_object().get(type))
			{
				if (const auto fieldsElement = Detail::FindField(type, "fields"); fieldsElement && !fieldsElement->get_array().get(fields))
				{
					for (simdjson::dom::element fieldElement : fields)
					{
						simdjson::dom::object field;

						if (!fieldElement.get_object().get(field) && !AddVertexInputs(field, location, result))
						{
							return false;
						}
					}

					return true;
				}
			}

			Rhi::NativeVertexInput input;
			input.Location = location;
			input.SemanticName = Detail::ReadString(parameter, "semanticName");
			Detail::ReadU32(parameter, "semanticIndex", input.SemanticIndex);

			if (input.SemanticName.empty())
			{
				result.Error = "vertex input " + Detail::ReadString(parameter, "name") + " has no semantic";
				return false;
			}

			if (!input.SemanticName.starts_with("SV_"))
			{
				result.Inputs.push_back(std::move(input));
			}

			return true;
		}

	} // namespace

	ShaderNativeVertexInputsResult ParseNativeVertexInputs(std::string_view registerReflectionJson)
	{
		ShaderNativeVertexInputsResult result;
		simdjson::dom::parser parser;
		const simdjson::padded_string padded(registerReflectionJson.data(), registerReflectionJson.size());
		simdjson::dom::element document;
		simdjson::dom::object root;
		simdjson::dom::array entryPoints;

		if (parser.parse(padded).get(document) || document.get_object().get(root))
		{
			result.Error = "register reflection is not a JSON object";
			return result;
		}

		const auto entries = Detail::FindField(root, "entryPoints");

		if (!entries || entries->get_array().get(entryPoints))
		{
			return result;
		}

		for (simdjson::dom::element entryElement : entryPoints)
		{
			simdjson::dom::object entry;
			simdjson::dom::array parameters;

			if (entryElement.get_object().get(entry) || Detail::ReadString(entry, "stage") != "vertex")
			{
				continue;
			}

			if (const auto parametersElement = Detail::FindField(entry, "parameters");
				!parametersElement || parametersElement->get_array().get(parameters))
			{
				continue;
			}

			for (simdjson::dom::element parameterElement : parameters)
			{
				simdjson::dom::object parameter;

				if (!parameterElement.get_object().get(parameter) && !AddVertexInputs(parameter, 0, result))
				{
					return result;
				}
			}
		}

		std::sort(result.Inputs.begin(), result.Inputs.end(),
			[](const auto& a, const auto& b)
			{
				return a.Location < b.Location;
			});
		return result;
	}

	ShaderNativeVertexInputsResult LoadNativeVertexInputs(const std::filesystem::path& registerReflectionPath)
	{
		std::ifstream stream(registerReflectionPath, std::ios::binary);

		if (!stream)
		{
			ShaderNativeVertexInputsResult result;
			result.Error = "could not open " + registerReflectionPath.string();
			return result;
		}

		std::ostringstream text;
		text << stream.rdbuf();
		return ParseNativeVertexInputs(text.str());
	}

	std::string ParseNativeSizeQueries(std::string_view registerReflectionJson, std::vector<Rhi::NativeBinding>& bindings)
	{
		simdjson::dom::parser parser;
		const simdjson::padded_string padded(registerReflectionJson.data(), registerReflectionJson.size());
		simdjson::dom::element document;
		simdjson::dom::object root;
		simdjson::dom::array queries;

		if (parser.parse(padded).get(document) || document.get_object().get(root))
		{
			return "register reflection is not a JSON object";
		}

		const auto list = Detail::FindField(root, "nativeSizeQueries");

		if (!list || list->get_array().get(queries))
		{
			return {};
		}

		for (simdjson::dom::element element : queries)
		{
			simdjson::dom::object query;
			std::uint32_t index = 0;
			std::uint32_t space = 0;

			if (element.get_object().get(query))
			{
				return "nativeSizeQueries holds a non-object";
			}

			const auto type = RegisterClass(Detail::ReadString(query, "class"));
			Detail::ReadU32(query, "index", index);
			Detail::ReadU32(query, "space", space);

			for (auto& binding : bindings)
			{
				if (type && binding.Class == *type && binding.Register == index && binding.RegisterSpace == space)
				{
					binding.SizeQueried = true;
				}
			}
		}

		return {};
	}

	std::string LoadNativeSizeQueries(const std::filesystem::path& registerReflectionPath, std::vector<Rhi::NativeBinding>& bindings)
	{
		std::ifstream stream(registerReflectionPath, std::ios::binary);

		if (!stream)
		{
			return "could not open " + registerReflectionPath.string();
		}

		std::ostringstream text;
		text << stream.rdbuf();
		return ParseNativeSizeQueries(text.str(), bindings);
	}

	std::string FindNativeSizeQueries(std::string_view hlsl)
	{
		const std::string text(hlsl);
		std::set<std::string> types;
		static const std::regex call(R"(([A-Za-z_][A-Za-z0-9_]*)\.GetDimensions\()");

		for (auto it = std::sregex_iterator(text.begin(), text.end(), call); it != std::sregex_iterator(); ++it)
		{
			const std::regex declared("StructuredBuffer<([^;(){},]*) > " + (*it)[1].str() + "[ ,)]");

			if (std::smatch match; std::regex_search(text, match, declared))
			{
				types.insert(match[1].str());
			}
		}

		if (types.empty())
		{
			return {};
		}

		static const std::regex global(R"(StructuredBuffer<([^;(){},]*) > [A-Za-z0-9_]+ : register\(([tu])([0-9]+)(, space([0-9]+))?\))");
		std::set<std::string> entries;
		std::string list;

		for (auto it = std::sregex_iterator(text.begin(), text.end(), global); it != std::sregex_iterator(); ++it)
		{
			const auto& match = *it;

			if (!types.contains(match[1].str()))
			{
				continue;
			}

			const auto entry = std::string(R"({"class":")") + (match[2] == "u" ? "unorderedAccess" : "shaderResource") + R"(","index":)" +
				match[3].str() + R"(,"space":)" + (match[5].matched ? match[5].str() : "0") + "}";

			if (entries.insert(entry).second)
			{
				list += (list.empty() ? "" : ",") + entry;
			}
		}

		return list.empty() ? std::string() : "[" + list + "]";
	}

	ShaderNativeBindingsResult BuildNativeBindings(const ShaderReflection& bindings, const ShaderReflection& registers)
	{
		ShaderNativeBindingsResult result;

		struct Register
		{
			const ShaderBindingReflection* Parameter = nullptr;
			bool EntryPoint = false;
		};

		std::unordered_map<std::string, Register> native;
		const auto index = [&](const std::vector<ShaderBindingReflection>& parameters, bool entryPoint)
		{
			for (const auto& parameter : parameters)
			{
				native.emplace(parameter.Name, Register{ &parameter, entryPoint });
			}
		};

		index(registers.GlobalParameters, false);

		for (const auto& entry : registers.EntryPoints)
		{
			index(entry.Parameters, true);
		}

		// Entry-point parameter registers are reflected relative to the entry point:
		// in each compiled stage they follow the registers of the global parameters
		// in space 0.
		std::unordered_map<int, std::uint32_t> globalEnd;

		for (const auto& parameter : registers.GlobalParameters)
		{
			if (const auto type = RegisterClass(parameter.BindingKind); type && parameter.HasIndex && parameter.Space == 0)
			{
				auto& end = globalEnd[static_cast<int>(*type)];
				end = std::max(end, parameter.Index + std::max(parameter.Count, 1u));
			}
		}

		const auto add = [&](const ShaderBindingReflection& parameter)
		{
			const bool push = parameter.BindingKind == "pushConstantBuffer";

			if (!push && (parameter.BindingKind != "descriptorTableSlot" || !parameter.HasIndex))
			{
				return true; // Not a descriptor (varyings, uniforms).
			}

			const auto found = native.find(parameter.Name);
			const auto* reg = found == native.end() ? nullptr : found->second.Parameter;
			const auto type = reg ? RegisterClass(reg->BindingKind) : std::nullopt;

			if (!type || !reg->HasIndex)
			{
				result.Error = "no native register for " + parameter.Name;
				return false;
			}

			Rhi::NativeBinding binding;
			binding.Space = push ? Rhi::NativeBinding::PushConstantSpace : parameter.Space;
			binding.Binding = push ? 0u : parameter.Index;
			binding.Class = *type;
			binding.Register = reg->Index;
			binding.RegisterSpace = reg->Space;
			binding.StructureStride = StructureStride(*reg);

			if (found->second.EntryPoint && reg->Space == 0)
			{
				binding.Register += globalEnd[static_cast<int>(*type)];
			}
			result.Bindings.push_back(binding);
			return true;
		};

		for (const auto& parameter : bindings.GlobalParameters)
		{
			if (!add(parameter))
			{
				return result;
			}
		}

		for (const auto& entry : bindings.EntryPoints)
		{
			for (const auto& parameter : entry.Parameters)
			{
				if (!add(parameter))
				{
					return result;
				}
			}
		}

		return result;
	}

} // namespace Swim::ShaderCompiler
