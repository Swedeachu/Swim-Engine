#include "Tools/ShaderCompiler/ShaderReflection.h"

#include <simdjson.h>

#include <fstream>
#include <limits>
#include <optional>
#include <sstream>
#include <utility>

namespace Swim::ShaderCompiler
{
	namespace
	{
		std::optional<simdjson::dom::element> FindField(
			simdjson::dom::object object,
			std::string_view name)
		{
			auto result = object[name];
			if (result.error())
			{
				return std::nullopt;
			}
			return result.value_unsafe();
		}

		std::string ReadString(simdjson::dom::object object, std::string_view name)
		{
			const auto field = FindField(object, name);
			if (!field)
			{
				return {};
			}

			std::string_view value;
			if (field->get_string().get(value))
			{
				return {};
			}
			return std::string(value);
		}

		bool ReadU32(simdjson::dom::object object, std::string_view name, std::uint32_t& outValue)
		{
			const auto field = FindField(object, name);
			if (!field)
			{
				return false;
			}

			std::uint64_t value = 0;
			if (field->get_uint64().get(value))
			{
				return false;
			}
			if (value > std::numeric_limits<std::uint32_t>::max())
			{
				return false;
			}

			outValue = static_cast<std::uint32_t>(value);
			return true;
		}

		ShaderBindingReflection ParseBindingParameter(simdjson::dom::object parameter)
		{
			ShaderBindingReflection reflection;
			reflection.Name = ReadString(parameter, "name");

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
					ReadU32(binding, "count", reflection.Count);
				}
			}

			if (const auto typeField = FindField(parameter, "type"))
			{
				simdjson::dom::object type;
				if (!typeField->get_object().get(type))
				{
					reflection.TypeKind = ReadString(type, "kind");
					reflection.ResourceShape = ReadString(type, "baseShape");
				}
			}

			return reflection;
		}

		void ParseParameterArray(
			simdjson::dom::element parametersElement,
			std::vector<ShaderBindingReflection>& outParameters)
		{
			simdjson::dom::array parameters;
			if (parametersElement.get_array().get(parameters))
			{
				return;
			}

			for (simdjson::dom::element parameterElement : parameters)
			{
				simdjson::dom::object parameter;
				if (parameterElement.get_object().get(parameter))
				{
					continue;
				}
				outParameters.push_back(ParseBindingParameter(parameter));
			}
		}

		void ParseThreadGroupSize(
			simdjson::dom::object entryPoint,
			std::array<std::uint32_t, 3>& outSize)
		{
			const auto field = FindField(entryPoint, "threadGroupSize");
			if (!field)
			{
				return;
			}

			simdjson::dom::array values;
			if (field->get_array().get(values))
			{
				return;
			}

			std::size_t index = 0;
			for (simdjson::dom::element valueElement : values)
			{
				if (index >= outSize.size())
				{
					break;
				}

				std::uint64_t value = 0;
				if (!valueElement.get_uint64().get(value) &&
					value <= std::numeric_limits<std::uint32_t>::max())
				{
					outSize[index] = static_cast<std::uint32_t>(value);
				}
				++index;
			}
		}
	}

	ShaderStage ShaderStageFromSlangName(std::string_view stageName)
	{
		if (stageName == "vertex")
		{
			return ShaderStage::Vertex;
		}
		if (stageName == "fragment" || stageName == "pixel")
		{
			return ShaderStage::Fragment;
		}
		if (stageName == "compute")
		{
			return ShaderStage::Compute;
		}
		if (stageName == "geometry")
		{
			return ShaderStage::Geometry;
		}
		if (stageName == "hull" || stageName == "tesscontrol")
		{
			return ShaderStage::Hull;
		}
		if (stageName == "domain" || stageName == "tesseval")
		{
			return ShaderStage::Domain;
		}
		if (stageName == "mesh")
		{
			return ShaderStage::Mesh;
		}
		if (stageName == "amplification" || stageName == "task")
		{
			return ShaderStage::Amplification;
		}
		if (stageName == "raygeneration")
		{
			return ShaderStage::RayGeneration;
		}
		if (stageName == "intersection")
		{
			return ShaderStage::Intersection;
		}
		if (stageName == "anyhit")
		{
			return ShaderStage::AnyHit;
		}
		if (stageName == "closesthit")
		{
			return ShaderStage::ClosestHit;
		}
		if (stageName == "miss")
		{
			return ShaderStage::Miss;
		}
		if (stageName == "callable")
		{
			return ShaderStage::Callable;
		}
		return ShaderStage::Unknown;
	}

	std::string_view ShaderStageName(ShaderStage stage)
	{
		switch (stage)
		{
		case ShaderStage::Vertex: return "vertex";
		case ShaderStage::Fragment: return "fragment";
		case ShaderStage::Compute: return "compute";
		case ShaderStage::Geometry: return "geometry";
		case ShaderStage::Hull: return "hull";
		case ShaderStage::Domain: return "domain";
		case ShaderStage::Mesh: return "mesh";
		case ShaderStage::Amplification: return "amplification";
		case ShaderStage::RayGeneration: return "raygeneration";
		case ShaderStage::Intersection: return "intersection";
		case ShaderStage::AnyHit: return "anyhit";
		case ShaderStage::ClosestHit: return "closesthit";
		case ShaderStage::Miss: return "miss";
		case ShaderStage::Callable: return "callable";
		case ShaderStage::Unknown:
		default:
			return "unknown";
		}
	}

	ShaderReflectionResult ParseSlangReflectionJson(std::string_view jsonText)
	{
		ShaderReflectionResult result;
		if (jsonText.empty())
		{
			result.Error = "Slang reflection JSON is empty";
			return result;
		}

		simdjson::dom::parser parser;
		simdjson::padded_string paddedJson(jsonText.data(), jsonText.size());
		simdjson::dom::element document;
		const simdjson::error_code parseError = parser.parse(paddedJson).get(document);
		if (parseError)
		{
			result.Error = std::string("Failed to parse Slang reflection JSON: ") + simdjson::error_message(parseError);
			return result;
		}

		simdjson::dom::object root;
		if (document.get_object().get(root))
		{
			result.Error = "Slang reflection root is not a JSON object";
			return result;
		}

		result.Reflection.SchemaVersion = ReadString(root, "version");
		if (result.Reflection.SchemaVersion.empty())
		{
			result.Reflection.SchemaVersion = "1.0";
		}

		if (const auto globalScopeField = FindField(root, "globalScope"))
		{
			simdjson::dom::object globalScope;
			if (!globalScopeField->get_object().get(globalScope))
			{
				result.Reflection.GlobalScopeKind = ReadString(globalScope, "kind");
				if (const auto parameters = FindField(globalScope, "parameters"))
				{
					ParseParameterArray(*parameters, result.Reflection.GlobalParameters);
				}
			}
		}
		else if (const auto parameters = FindField(root, "parameters"))
		{
			ParseParameterArray(*parameters, result.Reflection.GlobalParameters);
		}

		if (const auto entryPointsField = FindField(root, "entryPoints"))
		{
			simdjson::dom::array entryPoints;
			if (!entryPointsField->get_array().get(entryPoints))
			{
				for (simdjson::dom::element entryPointElement : entryPoints)
				{
					simdjson::dom::object entryPointObject;
					if (entryPointElement.get_object().get(entryPointObject))
					{
						continue;
					}

					ShaderEntryPointReflection entryPoint;
					entryPoint.Name = ReadString(entryPointObject, "name");
					entryPoint.Stage = ShaderStageFromSlangName(ReadString(entryPointObject, "stage"));
					ParseThreadGroupSize(entryPointObject, entryPoint.ThreadGroupSize);

					if (const auto scopeField = FindField(entryPointObject, "scope"))
					{
						simdjson::dom::object scope;
						if (!scopeField->get_object().get(scope))
						{
							if (const auto parameters = FindField(scope, "parameters"))
							{
								ParseParameterArray(*parameters, entryPoint.Parameters);
							}
						}
					}
					else if (const auto parameters = FindField(entryPointObject, "parameters"))
					{
						ParseParameterArray(*parameters, entryPoint.Parameters);
					}

					result.Reflection.EntryPoints.push_back(std::move(entryPoint));
				}
			}
		}

		if (result.Reflection.EntryPoints.empty())
		{
			result.Error = "Slang reflection contains no entry points";
		}
		return result;
	}

	ShaderReflectionResult LoadSlangReflectionJson(const std::filesystem::path& path)
	{
		std::ifstream stream(path, std::ios::binary);
		if (!stream)
		{
			ShaderReflectionResult result;
			result.Error = "Could not open Slang reflection file: " + path.string();
			return result;
		}

		std::ostringstream buffer;
		buffer << stream.rdbuf();
		if (!stream.good() && !stream.eof())
		{
			ShaderReflectionResult result;
			result.Error = "Could not read Slang reflection file: " + path.string();
			return result;
		}
		return ParseSlangReflectionJson(buffer.str());
	}

}
