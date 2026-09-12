#include "Tools/ShaderCompiler/ShaderParameterLayout.h"
#include "Tools/ShaderCompiler/ShaderParameterOffsets.h"
#include "Tools/ShaderCompiler/ShaderBindingReflection.h"
#include "Tools/ShaderCompiler/ShaderReflectionJson.h"

#include <utility>

namespace Swim::ShaderCompiler::Detail
{
	namespace
	{
		bool Add(std::uint32_t& target, std::uint32_t offset)
		{
			if (offset > UINT32_MAX - target)
			{
				return false;
			}
			target += offset;
			return true;
		}

		bool ReadOffsets(simdjson::dom::object layout, ShaderParameterOffsets& offsets, bool single = false)
		{
			const auto read = [&](simdjson::dom::element element)
			{
				simdjson::dom::object binding;
				if (element.get_object().get(binding))
				{
					return false;
				}
				const auto kind = ReadString(binding, "kind");
				std::uint32_t count = 1;
				if (FindField(binding, "count") && (!ReadU32(binding, "count", count) || count == 0 || (single && count != 1)))
				{
					return false;
				}
				if (kind == "descriptorTableSlot" && !offsets.HasDescriptor)
				{
					offsets.HasDescriptor = true;
					return ReadU32(binding, "index", offsets.Descriptor) && count - 1 <= UINT32_MAX - offsets.Descriptor &&
						(!FindField(binding, "space") || ReadU32(binding, "space", offsets.Space));
				}
				if (kind == "subElementRegisterSpace" && !offsets.HasRegisterSpace && !FindField(binding, "space"))
				{
					offsets.HasRegisterSpace = true;
					return ReadU32(binding, "index", offsets.RegisterSpace) && count - 1 <= UINT32_MAX - offsets.RegisterSpace;
				}
				if (kind == "uniform" && !offsets.HasUniform && !FindField(binding, "space"))
				{
					offsets.HasUniform = true;
					return ReadU32(binding, "offset", offsets.Uniform) &&
						ReadU32(binding, "size", offsets.UniformSize) && offsets.UniformSize <= UINT32_MAX - offsets.Uniform;
				}
				return false;
			};
			const auto binding = FindField(layout, "binding");
			const auto bindings = FindField(layout, "bindings");
			if (binding && bindings)
			{
				return false;
			}
			if (binding)
			{
				return read(*binding);
			}
			if (bindings)
			{
				simdjson::dom::array array;
				if (bindings->get_array().get(array) || array.size() == 0)
				{
					return false;
				}
				for (auto value : array)
				{
					if (!read(value))
					{
						return false;
					}
				}
			}
			return true;
		}

		bool ApplyOffsets(ShaderParameterOffsets& base, const ShaderParameterOffsets& local)
		{
			return Add(base.Descriptor, local.Descriptor) && Add(base.Space, local.Space) &&
				Add(base.RegisterSpace, local.RegisterSpace) && Add(base.Uniform, local.Uniform);
		}

		bool GetObject(simdjson::dom::object parent, const char* name, simdjson::dom::object& object)
		{
			const auto field = FindField(parent, name);
			return field && !field->get_object().get(object);
		}

		bool IsUniformValue(simdjson::dom::object type, std::uint32_t depth)
		{
			if (depth > 64)
			{
				return false;
			}
			const auto kind = ReadString(type, "kind");
			if (kind == "scalar" || kind == "vector" || kind == "matrix")
			{
				return true;
			}
			if (kind == "array")
			{
				std::uint32_t count = 0;
				simdjson::dom::object element;
				return ReadU32(type, "elementCount", count) && count > 0 &&
					GetObject(type, "elementType", element) && IsUniformValue(element, depth + 1);
			}
			if (kind == "struct")
			{
				const auto fields = FindField(type, "fields");
				simdjson::dom::array array;
				if (!fields || fields->get_array().get(array))
				{
					return false;
				}
				for (auto field : array)
				{
					simdjson::dom::object variable, child;
					if (field.get_object().get(variable) || !GetObject(variable, "type", child) || !IsUniformValue(child, depth + 1))
					{
						return false;
					}
				}
				return true;
			}
			return false;
		}

		bool ParseLayout(simdjson::dom::object parameter, ShaderParameterOffsets base,
			const std::string& path, std::vector<ShaderBindingReflection>& output,
			std::size_t uniformOwner, std::uint32_t depth)
		{
			if (depth > 64)
			{
				return false;
			}
			simdjson::dom::object type;
			ShaderParameterOffsets local;
			if (!GetObject(parameter, "type", type) || !ReadOffsets(parameter, local) || !ApplyOffsets(base, local))
			{
				return false;
			}
			const auto kind = ReadString(type, "kind");
			if (local.HasUniform && (uniformOwner >= output.size() || base.Uniform > output[uniformOwner].Size ||
				local.UniformSize > output[uniformOwner].Size - base.Uniform))
			{
				return false;
			}
			if (kind == "struct")
			{
				const auto fields = FindField(type, "fields");
				simdjson::dom::array array;
				if (!fields || fields->get_array().get(array))
				{
					return false;
				}
				for (auto field : array)
				{
					simdjson::dom::object child;
					if (field.get_object().get(child))
					{
						return false;
					}
					const auto name = ReadString(child, "name");
					if (name.empty() || !ParseLayout(child, base, path + "." + name, output, uniformOwner, depth + 1))
					{
						return false;
					}
				}
				return true;
			}
			if (kind == "parameterBlock" || kind == "constantBuffer")
			{
				simdjson::dom::object container, element;
				ShaderParameterOffsets containerOffsets, elementOffsets;
				if (local.HasUniform || (kind == "parameterBlock" ? (!local.HasRegisterSpace || local.HasDescriptor) :
					(!local.HasDescriptor || local.HasRegisterSpace)) ||
					!GetObject(type, "containerVarLayout", container) || !GetObject(type, "elementVarLayout", element) ||
					!ReadOffsets(container, containerOffsets, true) || !ReadOffsets(element, elementOffsets))
				{
					return false;
				}
				if (kind == "parameterBlock")
				{
					// Parameter blocks start a new descriptor set. Child set indices
					// remain relative to this block; descriptor bindings restart at zero.
					base.Space = base.RegisterSpace;
					base.Descriptor = 0;
				}
				base.Uniform = 0;
				auto containerBase = base;
				if (containerOffsets.HasUniform || containerOffsets.RegisterSpace != 0 ||
					(kind == "parameterBlock" ? !containerOffsets.HasRegisterSpace : containerOffsets.HasRegisterSpace) ||
					!ApplyOffsets(containerBase, containerOffsets))
				{
					return false;
				}
				uniformOwner = SIZE_MAX;
				if (containerOffsets.HasDescriptor)
				{
					if (!elementOffsets.HasUniform || elementOffsets.Uniform != 0 || elementOffsets.UniformSize == 0)
					{
						return false;
					}
					ShaderBindingReflection uniform;
					uniform.Name = path;
					uniform.BindingKind = "descriptorTableSlot";
					uniform.TypeKind = "constantBuffer";
					uniform.HasIndex = uniform.HasSpace = uniform.HasSize = true;
					uniform.Index = containerBase.Descriptor;
					uniform.Space = containerBase.Space;
					uniform.Size = elementOffsets.UniformSize;
					uniformOwner = output.size();
					output.push_back(std::move(uniform));
				}
				else if (elementOffsets.HasUniform && elementOffsets.UniformSize != 0)
				{
					return false;
				}
				return ParseLayout(element, base, path, output, uniformOwner, depth + 1);
			}
			if (local.HasUniform)
			{
				if (local.HasDescriptor || local.HasRegisterSpace || !IsUniformValue(type, depth))
				{
					return false;
				}
				output[uniformOwner].UniformFields.push_back({ path, base.Uniform, local.UniformSize });
				return true;
			}
			if (!local.HasDescriptor || local.HasRegisterSpace)
			{
				return false;
			}
			auto leaf = ParseSlangBindingParameter(parameter);
			if (leaf.HasUnsupportedBindingLayout || leaf.Count != 1)
			{
				return false;
			}
			leaf.Name = path;
			leaf.Index = base.Descriptor;
			leaf.Space = base.Space;
			leaf.HasIndex = leaf.HasSpace = true;
			output.push_back(std::move(leaf));
			return true;
		}
	}

	void ParseSlangParameterLayout(simdjson::dom::object parameter,
		std::vector<ShaderBindingReflection>& outParameters)
	{
		auto reflection = ParseSlangBindingParameter(parameter);
		simdjson::dom::object type;
		const bool group = reflection.TypeKind == "parameterBlock" ||
			(reflection.TypeKind == "constantBuffer" && reflection.BindingKind != "pushConstantBuffer" &&
				GetObject(parameter, "type", type) && FindField(type, "elementVarLayout"));
		const bool structure = reflection.TypeKind == "struct" &&
			reflection.BindingKind != "varyingInput" && reflection.BindingKind != "varyingOutput" && reflection.SemanticName.empty();
		if (!group && !structure)
		{
			if (reflection.TypeKind == "array" && reflection.DescriptorElementTypeKind == "constantBuffer")
			{
				simdjson::dom::object array, buffer, element;
				if (GetObject(parameter, "type", array) && GetObject(array, "elementType", buffer) &&
					GetObject(buffer, "elementType", element) && !IsUniformValue(element, 0))
				{
					reflection.HasUnsupportedBindingLayout = true;
				}
			}
			outParameters.push_back(std::move(reflection));
			return;
		}
		std::vector<ShaderBindingReflection> flattened;
		if (!ParseLayout(parameter, {}, reflection.Name, flattened, SIZE_MAX, 0))
		{
			reflection.HasUnsupportedBindingLayout = true;
			outParameters.push_back(std::move(reflection));
			return;
		}
		for (auto& leaf : flattened)
		{
			outParameters.push_back(std::move(leaf));
		}
	}

}
