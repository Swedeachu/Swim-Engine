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
		const bool descriptorArray = parameter.TypeKind == "array";
		const auto& typeKind = descriptorArray ? parameter.DescriptorElementTypeKind : parameter.TypeKind;
		const auto count = descriptorArray ? parameter.DescriptorArrayCount : parameter.Count;
		if (descriptorArray && (parameter.Count != 1 || count == 0))
		{
			return "Descriptor arrays require one binding and a fixed positive element count: " + parameter.Name;
		}
		Rhi::DescriptorType type;
		Rhi::Format storageFormat = Rhi::Format::Undefined;
		Rhi::SampledTextureClass sampledClass = Rhi::SampledTextureClass::Float;
		Rhi::TextureViewDimension sampledDimension = Rhi::TextureViewDimension::Texture2D;
		if (typeKind == "samplerState")
		{
			type = Rhi::DescriptorType::Sampler;
		}
		else if (typeKind == "constantBuffer")
		{
			type = Rhi::DescriptorType::UniformBuffer;
		}
		else if (typeKind == "resource" && parameter.ResourceAccess == "readWrite" &&
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
		else if (typeKind == "resource" && parameter.ResourceAccess == "readWrite" &&
			stages == Rhi::ShaderStageMask::Compute && !parameter.ResourceArray && !parameter.ResourceMultisample &&
			(parameter.ResourceShape == "structuredBuffer" || parameter.ResourceShape == "byteAddressBuffer"))
		{
			type = Rhi::DescriptorType::StorageBuffer;
		}
		else if (typeKind == "resource" && (parameter.ResourceAccess.empty() || parameter.ResourceAccess == "read"))
		{
			if ((parameter.ResourceShape == "texture1D" || parameter.ResourceShape == "texture2D" ||
					parameter.ResourceShape == "textureCube" || (parameter.ResourceShape == "texture3D" && !parameter.ResourceArray)) && !parameter.ResourceMultisample &&
				parameter.ResourceComponentCount >= 1 && parameter.ResourceComponentCount <= 4 &&
				(parameter.ResourceScalarType == "float32" || parameter.ResourceScalarType == "uint32" || parameter.ResourceScalarType == "int32"))
			{
				if (!parameter.ResourceFormat.empty() && parameter.ResourceFormat != "unknown")
				{
					return "Sampled textures require an unknown image format; the RHI binding describes numeric class, not an exact storage format: " + parameter.Name;
				}
				type = Rhi::DescriptorType::SampledTexture;
				if (parameter.ResourceShape == "texture1D")
				{
					sampledDimension = parameter.ResourceArray ? Rhi::TextureViewDimension::Texture1DArray : Rhi::TextureViewDimension::Texture1D;
				}
				else if (parameter.ResourceShape == "texture2D")
				{
					sampledDimension = parameter.ResourceArray ? Rhi::TextureViewDimension::Texture2DArray : Rhi::TextureViewDimension::Texture2D;
				}
				else if (parameter.ResourceShape == "textureCube")
				{
					sampledDimension = parameter.ResourceArray ? Rhi::TextureViewDimension::TextureCubeArray : Rhi::TextureViewDimension::TextureCube;
				}
				else
				{
					sampledDimension = Rhi::TextureViewDimension::Texture3D;
				}
				sampledClass = parameter.ResourceScalarType == "uint32" ? Rhi::SampledTextureClass::Uint :
					parameter.ResourceScalarType == "int32" ? Rhi::SampledTextureClass::Sint : Rhi::SampledTextureClass::Float;
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
		schema->Bindings.push_back({ parameter.Index, type, count, stages, false, false, storageFormat, sampledClass, sampledDimension });
		return {};
	}

} // namespace Swim::ShaderCompiler
