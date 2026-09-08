#include "Tools/ShaderCompiler/ShaderRhiInterface.h"
#include "Tools/ShaderCompiler/ShaderStorageTexture.h"

#include <algorithm>

namespace Swim::ShaderCompiler
{

	ShaderRhiInterfaceResult BuildRhiShaderInterface(const ShaderReflection& reflection)
	{
		ShaderRhiInterfaceResult result;
		const auto fail = [&](std::string message)
		{
			result.Interface = {};
			result.Error = std::move(message);
			return result;
		};
		Rhi::ShaderStageMask stages = Rhi::ShaderStageMask::None;
		for (const auto& entry : reflection.EntryPoints)
		{
			if (entry.Stage == ShaderStage::Vertex)
			{
				stages = stages | Rhi::ShaderStageMask::Vertex;
			}
			else if (entry.Stage == ShaderStage::Fragment)
			{
				stages = stages | Rhi::ShaderStageMask::Fragment;
			}
			else if (entry.Stage == ShaderStage::Compute && reflection.EntryPoints.size() == 1)
			{
				if (std::any_of(entry.ThreadGroupSize.begin(), entry.ThreadGroupSize.end(), [](auto size) { return size == 0; }))
				{
					return fail("Compute requires three fixed positive local-size dimensions");
				}
				stages = Rhi::ShaderStageMask::Compute;
				result.Interface.ComputeThreadGroupSize = entry.ThreadGroupSize;
			}
			else
			{
				return fail("RHI reflection requires graphics stages or one compute entry point");
			}
			for (const auto& parameter : entry.Parameters)
			{
				if (parameter.BindingKind == "descriptorTableSlot" || parameter.BindingKind == "pushConstantBuffer" ||
					parameter.TypeKind == "parameterBlock" || parameter.TypeKind == "constantBuffer" ||
					parameter.TypeKind == "resource" || parameter.TypeKind == "samplerState" || parameter.TypeKind == "array")
				{
					return fail("Entry-point resource parameters require a scoped reflection conversion");
				}
			}
		}
		if (stages == Rhi::ShaderStageMask::None)
		{
			return fail("Shader reflection has no supported entry points");
		}
		for (const auto& parameter : reflection.GlobalParameters)
		{
			if (parameter.BindingKind == "pushConstantBuffer")
			{
				if (parameter.TypeKind != "constantBuffer" || parameter.Count != 1 ||
					!parameter.HasOffset || !parameter.HasSize || parameter.Size == 0 ||
					parameter.Offset % 4 != 0 || parameter.Size % 4 != 0 || parameter.Size > UINT32_MAX - parameter.Offset ||
					!result.Interface.PushConstants.empty())
				{
					return fail("Push constants require one aligned, sized global constant buffer: " + parameter.Name);
				}
				// Whole-block byte extent comes from Slang; no C++ packing guesses.
				result.Interface.PushConstants.push_back({ parameter.Offset, parameter.Size, stages });
				continue;
			}
			if (parameter.BindingKind != "descriptorTableSlot" || !parameter.HasIndex || parameter.Count == 0)
			{
				return fail("Unsupported global resource binding: " + parameter.Name);
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
					return fail("Storage textures require an explicit supported format and matching scalar/vector type: " + parameter.Name);
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
					return fail("Unsupported reflected resource shape: " + parameter.ResourceShape);
				}
			}
			else
			{
				return fail("Unsupported reflected resource type/access: " + parameter.Name);
			}
			const auto space = parameter.HasSpace ? parameter.Space : 0;
			auto schema = std::find_if(result.Interface.DescriptorSchemas.begin(), result.Interface.DescriptorSchemas.end(),
				[space](const auto& candidate) { return candidate.Space == space; });
			if (schema == result.Interface.DescriptorSchemas.end())
			{
				result.Interface.DescriptorSchemas.push_back({ space, {} });
				schema = result.Interface.DescriptorSchemas.end() - 1;
			}
			if (std::any_of(schema->Bindings.begin(), schema->Bindings.end(), [&](const auto& binding) { return binding.Binding == parameter.Index; }))
			{
				return fail("Duplicate reflected descriptor binding: " + parameter.Name);
			}
			// Conservatively visible to every program stage; no handwritten stage guesses.
			schema->Bindings.push_back({ parameter.Index, type, parameter.Count, stages, false, false, storageFormat });
		}
		return result;
	}

} // namespace Swim::ShaderCompiler
