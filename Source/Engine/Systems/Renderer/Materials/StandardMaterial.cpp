#include "Engine/Systems/Renderer/Materials/StandardMaterial.h"

#include <stdexcept>

namespace Swim::Render
{
	MaterialTemplateDesc StandardMaterialTemplateDesc()
	{
		using T = MaterialParameterType;
		MaterialTemplateDesc desc;
		desc.Name = "Standard";
		desc.RecordSize = StandardMaterialRecordSize;
		desc.Parameters = {
			{ "BaseColorFactor", T::Float4, 0 },
			{ "EmissiveFactor", T::Float3, 16 },
			{ "MetallicFactor", T::Float, 28 },
			{ "RoughnessFactor", T::Float, 32 },
			{ "NormalScale", T::Float, 36 },
			{ "OcclusionStrength", T::Float, 40 },
			{ "AlphaCutoff", T::Float, 44 },
			{ "BaseColorTexture", T::TextureIndex, 48 },
			{ "MetallicRoughnessTexture", T::TextureIndex, 52 },
			{ "NormalTexture", T::TextureIndex, 56 },
			{ "OcclusionTexture", T::TextureIndex, 60 },
			{ "EmissiveTexture", T::TextureIndex, 64 },
			{ "MaterialSampler", T::SamplerIndex, 68 },
			{ "Flags", T::Uint, 72 },
			{ "Reserved", T::Uint, 76 },
		};
		return desc;
	}

	std::shared_ptr<const MaterialTemplate> CreateStandardMaterialTemplate()
	{
		auto materialTemplate = std::make_shared<MaterialTemplate>(StandardMaterialTemplateDesc());
		const std::array<float, 4> white{ 1, 1, 1, 1 };
		const std::array<float, 1> one{ 1.0f };
		const std::array<float, 1> cutoff{ 0.5f };
		materialTemplate->SetDefault("BaseColorFactor", white);
		materialTemplate->SetDefault("MetallicFactor", one);
		materialTemplate->SetDefault("RoughnessFactor", one);
		materialTemplate->SetDefault("NormalScale", one);
		materialTemplate->SetDefault("OcclusionStrength", one);
		materialTemplate->SetDefault("AlphaCutoff", cutoff);
		return materialTemplate;
	}

	namespace
	{
		void RequireStandard(const MaterialInstance& instance)
		{
			const auto& materialTemplate = instance.GetTemplate();
			const auto expected = StandardMaterialTemplateDesc();
			bool same = materialTemplate.GetRecordSize() == expected.RecordSize &&
				materialTemplate.GetParameters().size() == expected.Parameters.size();
			for (std::size_t i = 0; same && i < expected.Parameters.size(); ++i)
			{
				const auto* parameter = materialTemplate.FindParameter(expected.Parameters[i].Name);
				same = parameter && parameter->Type == expected.Parameters[i].Type && parameter->Offset == expected.Parameters[i].Offset;
			}
			if (!same)
			{
				throw std::invalid_argument("Material " + materialTemplate.GetName() + " does not use the standard material layout");
			}
		}
	} // namespace

	StandardPbr::Parameters ReadStandardParameters(const MaterialInstance& instance)
	{
		RequireStandard(instance);
		StandardPbr::Parameters parameters;
		parameters.BaseColorFactor = instance.GetVector("BaseColorFactor");
		const auto emissive = instance.GetVector("EmissiveFactor");
		parameters.EmissiveFactor = { emissive[0], emissive[1], emissive[2] };
		parameters.MetallicFactor = instance.GetFloat("MetallicFactor");
		parameters.RoughnessFactor = instance.GetFloat("RoughnessFactor");
		parameters.NormalScale = instance.GetFloat("NormalScale");
		parameters.OcclusionStrength = instance.GetFloat("OcclusionStrength");
		parameters.AlphaCutoff = instance.GetFloat("AlphaCutoff");
		parameters.Flags = instance.GetUint("Flags");
		return parameters;
	}

	StandardMaterialTextures ReadStandardTextures(const MaterialInstance& instance)
	{
		RequireStandard(instance);
		return { instance.GetUint("BaseColorTexture"), instance.GetUint("MetallicRoughnessTexture"), instance.GetUint("NormalTexture"),
			instance.GetUint("OcclusionTexture"), instance.GetUint("EmissiveTexture"), instance.GetUint("MaterialSampler") };
	}
} // namespace Swim::Render
