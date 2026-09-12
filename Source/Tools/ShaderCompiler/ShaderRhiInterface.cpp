#include "Tools/ShaderCompiler/ShaderRhiInterface.h"
#include "Tools/ShaderCompiler/ShaderDescriptorBinding.h"

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
		if (reflection.HasUnsupportedGlobalScopeLayout ||
			(!reflection.GlobalScopeKind.empty() && reflection.GlobalScopeKind != "none"))
		{
			return fail("Unsupported global scope layout; use explicitly bound parameter groups");
		}
		Rhi::ShaderStageMask stages = Rhi::ShaderStageMask::None;
		for (const auto& entry : reflection.EntryPoints)
		{
			if ((entry.Stage == ShaderStage::Vertex && (static_cast<std::uint32_t>(stages) & static_cast<std::uint32_t>(Rhi::ShaderStageMask::Vertex)) != 0) ||
				(entry.Stage == ShaderStage::Fragment && (static_cast<std::uint32_t>(stages) & static_cast<std::uint32_t>(Rhi::ShaderStageMask::Fragment)) != 0))
			{
				return fail("Shader reflection requires at most one entry point per graphics stage");
			}
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
		}
		if (stages == Rhi::ShaderStageMask::None)
		{
			return fail("Shader reflection has no supported entry points");
		}
		for (const auto& parameter : reflection.GlobalParameters)
		{
			if (parameter.BindingKind == "pushConstantBuffer")
			{
				if (parameter.HasUnsupportedBindingLayout || parameter.TypeKind != "constantBuffer" || parameter.Count != 1 ||
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
			if (auto error = AppendRhiDescriptorBinding(parameter, stages, result.Interface); !error.empty())
			{
				return fail(std::move(error));
			}
		}
		for (const auto& entry : reflection.EntryPoints)
		{
			if (entry.HasUnsupportedScopeLayout || (!entry.ScopeKind.empty() && entry.ScopeKind != "none"))
			{
				return fail("Entry-point scope containers require a nested layout conversion: " + entry.Name);
			}
			const auto visibility = entry.Stage == ShaderStage::Vertex ? Rhi::ShaderStageMask::Vertex :
				entry.Stage == ShaderStage::Fragment ? Rhi::ShaderStageMask::Fragment : Rhi::ShaderStageMask::Compute;
			for (const auto& parameter : entry.Parameters)
			{
				if (parameter.HasUnsupportedBindingLayout)
				{
					return fail("Unsupported entry-point binding layout: " + entry.Name + "." + parameter.Name);
				}
				if (parameter.BindingKind == "descriptorTableSlot")
				{
					if (auto error = AppendRhiDescriptorBinding(parameter, visibility, result.Interface); !error.empty())
					{
						return fail(entry.Name + ": " + error);
					}
					continue;
				}
				// Only stage IO is ignored. Uniform bytes, implicit scope containers,
				// nested resources and entry-local push blocks must never disappear.
				const bool valueType = parameter.TypeKind == "scalar" || parameter.TypeKind == "vector" ||
					parameter.TypeKind == "matrix" || parameter.TypeKind == "struct";
				const bool varying = parameter.BindingKind == "varyingInput" || parameter.BindingKind == "varyingOutput" ||
					(parameter.BindingKind.empty() && parameter.SemanticName.starts_with("SV_"));
				if (!valueType || !varying)
				{
					return fail("Unsupported entry-point parameter: " + entry.Name + "." + parameter.Name);
				}
			}
		}
		return result;
	}

} // namespace Swim::ShaderCompiler
