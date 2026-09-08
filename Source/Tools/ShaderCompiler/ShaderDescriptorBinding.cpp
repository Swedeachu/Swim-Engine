#include "Tools/ShaderCompiler/ShaderDescriptorBinding.h"
#include "Tools/ShaderCompiler/ShaderStorageTexture.h"

#include <algorithm>

namespace Swim::ShaderCompiler
{

	std::string AppendRhiDescriptorBinding(const ShaderBindingReflection& parameter,
		Rhi::ShaderStageMask stages, Rhi::ShaderProgramInterface& interface)
	{
		if (parameter.HasUnsupportedBindingLayout || parameter.BindingKind != "descriptorTableSlot" ||
			!parameter.HasIndex || parameter.Count == 0)
		{
			return "Unsupported descriptor binding: " + parameter.Name;
		}
		Rhi::DescriptorType type;
		Rhi::Format storageFormat = Rhi::Format::Undefined;
		if (parameter.TypeKind == "samplerState")
		{
			type = Rhi::DescriptorType::Sampler;
		}
		else if (parameter.TypeKind == "constantBuffer")
		{
			type = Rhi::DescriptorType::UniformBuffer;
		}
		else if (parameter.TypeKind == "resource" && parameter.ResourceAccess == "readWrite" &&
			parameter.ResourceShape == "texture2D" && stages == Rhi::ShaderStageMask::Compute &&
			!parameter.ResourceArray && !parameter.ResourceMultisample)
		{
			storageFormat = GetRhiStorageTextureFormat(parameter);
			if (storageFormat == Rhi::Format::Undefined)
			{
				return "Storage textures require an explicit supported format and matching scalar/vector type: " + parameter.Name;
			}
			type = Rhi::DescriptorType::StorageTexture;
		}
		else if (parameter.TypeKind == "resource" && parameter.ResourceAccess == "readWrite" &&
			stages == Rhi::ShaderStageMask::Compute && !parameter.ResourceArray && !parameter.ResourceMultisample &&
			(parameter.ResourceShape == "structuredBuffer" || parameter.ResourceShape == "byteAddressBuffer"))
		{
			type = Rhi::DescriptorType::StorageBuffer;
		}
		else if (parameter.TypeKind == "resource" && (parameter.ResourceAccess.empty() || parameter.ResourceAccess == "read"))
		{
			if (parameter.ResourceShape == "texture2D" && !parameter.ResourceArray && !parameter.ResourceMultisample &&
				parameter.ResourceScalarType == "float32")
			{
				type = Rhi::DescriptorType::SampledTexture;
			}
			else if (parameter.ResourceShape == "structuredBuffer" || parameter.ResourceShape == "byteAddressBuffer")
			{
				type = Rhi::DescriptorType::ReadOnlyStorageBuffer;
			}
			else
			{
				return "Unsupported reflected resource shape: " + parameter.ResourceShape;
			}
		}
		else
		{
			return "Unsupported reflected resource type/access: " + parameter.Name;
		}
		const auto space = parameter.HasSpace ? parameter.Space : 0;
		auto schema = std::find_if(interface.DescriptorSchemas.begin(), interface.DescriptorSchemas.end(),
			[space](const auto& candidate) { return candidate.Space == space; });
		if (schema == interface.DescriptorSchemas.end())
		{
			interface.DescriptorSchemas.push_back({ space, {} });
			schema = interface.DescriptorSchemas.end() - 1;
		}
		if (std::any_of(schema->Bindings.begin(), schema->Bindings.end(), [&](const auto& binding) { return binding.Binding == parameter.Index; }))
		{
			return "Duplicate reflected descriptor binding: " + parameter.Name;
		}
		// The caller supplies global or entry-point visibility; binding coordinates stay absolute.
		schema->Bindings.push_back({ parameter.Index, type, parameter.Count, stages, false, false, storageFormat });
		return {};
	}

} // namespace Swim::ShaderCompiler
