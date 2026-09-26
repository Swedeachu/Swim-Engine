#include "Engine/Systems/Renderer/Runtime/MaterialLibrary.h"

#include "Engine/Systems/Renderer/GpuMaterials/GpuMaterialTable.h"
#include "Engine/Systems/Renderer/Materials/MaterialInstance.h"
#include "Engine/Systems/Renderer/Materials/MaterialTemplate.h"
#include "Engine/Systems/Renderer/Materials/StandardMaterial.h"
#include "Engine/Systems/Renderer/Residency/AssetResidencyService.h"
#include "Engine/Systems/Renderer/Resources/BindlessResourceTable.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Engine
{
	namespace
	{
		std::uint32_t Flags(const MaterialDesc& desc)
		{
			namespace Pbr = Swim::Render::StandardPbr;
			std::uint32_t flags = 0;
			flags |= desc.Blend == MaterialBlend::Masked ? Pbr::FlagAlphaMask : 0u;
			flags |= desc.Blend == MaterialBlend::Transparent ? Pbr::FlagAlphaBlend : 0u;
			flags |= desc.DoubleSided ? Pbr::FlagDoubleSided : 0u;
			return flags;
		}

		float Finite(float value, float fallback, float low, float high)
		{
			return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
		}
	} // namespace

	MaterialLibrary::MaterialLibrary(Swim::Render::GpuMaterialTable& tableValue,
		std::shared_ptr<const Swim::Render::MaterialTemplate> templateValue, Swim::Render::AssetResidencyService& residencyValue,
		std::uint32_t samplerIndex)
		: table(tableValue), materialTemplate(std::move(templateValue)), residency(residencyValue), sampler(samplerIndex)
	{
		MaterialDesc fallback;
		fallback.Name = "Default";
		fallback.BaseColor = { 0.8f, 0.8f, 0.8f, 1.0f };
		defaultSet = Create(fallback);
	}

	MaterialLibrary::~MaterialLibrary() = default;

	void MaterialLibrary::SetRouter(Router value)
	{
		router = std::move(value);
		if (!router)
		{
			return;
		}
		for (auto& [set, entry] : entries)
		{
			router(set, ToParameters(entry.Desc));
		}
	}

	Swim::Render::StandardPbr::Parameters MaterialLibrary::ToParameters(const MaterialDesc& desc)
	{
		Swim::Render::StandardPbr::Parameters parameters;
		parameters.BaseColorFactor = desc.BaseColor;
		parameters.EmissiveFactor = desc.Emissive;
		parameters.MetallicFactor = desc.Metallic;
		parameters.RoughnessFactor = desc.Roughness;
		parameters.NormalScale = desc.NormalScale;
		parameters.OcclusionStrength = desc.OcclusionStrength;
		parameters.AlphaCutoff = desc.AlphaCutoff;
		parameters.Flags = Flags(desc);
		return parameters;
	}

	void MaterialLibrary::Write(Entry& entry, bool force)
	{
		const MaterialDesc& d = entry.Desc;
		const std::array<Swim::Assets::AssetHandle<Swim::Assets::TextureAsset>, 5> textures{ d.BaseColorTexture, d.MetallicRoughnessTexture,
			d.NormalTexture, d.EmissiveTexture, d.OcclusionTexture };
		std::array<std::uint32_t, 5> indices{};
		for (std::size_t i = 0; i < textures.size(); ++i)
		{
			indices[i] =
				textures[i].IsValid() ? residency.GetBindlessIndex(textures[i]) : Swim::Render::BindlessResourceTable::FallbackIndex;
		}
		if (!force && indices == entry.TextureIndices)
		{
			return;
		}
		entry.TextureIndices = indices;
		auto& instance = *entry.Instance;
		instance.SetVector("BaseColorFactor",
			std::array<float, 4>{ Finite(d.BaseColor[0], 1, 0, 1), Finite(d.BaseColor[1], 1, 0, 1), Finite(d.BaseColor[2], 1, 0, 1),
				Finite(d.BaseColor[3], 1, 0, 1) });
		instance.SetVector("EmissiveFactor",
			std::array<float, 3>{
				Finite(d.Emissive[0], 0, 0, 1e4f), Finite(d.Emissive[1], 0, 0, 1e4f), Finite(d.Emissive[2], 0, 0, 1e4f) });
		instance.SetFloat("MetallicFactor", Finite(d.Metallic, 0, 0, 1));
		instance.SetFloat("RoughnessFactor", Finite(d.Roughness, 0.5f, 0, 1));
		instance.SetFloat("NormalScale", Finite(d.NormalScale, 1, 0, 10));
		instance.SetFloat("OcclusionStrength", Finite(d.OcclusionStrength, 1, 0, 1));
		instance.SetFloat("AlphaCutoff", Finite(d.AlphaCutoff, 0.5f, 0, 1));
		instance.SetUint("Flags", Flags(d));
		instance.SetSampler("MaterialSampler", sampler);
		// A texture that is not resident yet samples the white fallback (element 0), which
		// the shader treats as "no texture" for the base color and occlusion.
		instance.SetTexture("BaseColorTexture", indices[0]);
		instance.SetTexture("MetallicRoughnessTexture", indices[1]);
		instance.SetTexture("NormalTexture", indices[2]);
		instance.SetTexture("EmissiveTexture", indices[3]);
		instance.SetTexture("OcclusionTexture", indices[4]);
	}

	std::uint32_t MaterialLibrary::Create(const MaterialDesc& desc)
	{
		Entry entry;
		entry.Desc = desc;
		entry.Instance = std::make_shared<Swim::Render::MaterialInstance>(materialTemplate);
		entry.TextureIndices.fill(UINT32_MAX);
		Write(entry, true);
		entry.Handle = table.Create(entry.Instance);
		const std::uint32_t set = table.GetIndex(entry.Handle);
		if (router)
		{
			router(set, ToParameters(desc));
		}
		entries[set] = std::move(entry);
		return set;
	}

	std::uint32_t MaterialLibrary::GetOrCreate(const MaterialDesc& desc)
	{
		if (!desc.Name.empty())
		{
			for (const auto& [set, entry] : entries)
			{
				if (entry.Desc.Name == desc.Name)
				{
					return set;
				}
			}
		}
		return Create(desc);
	}

	bool MaterialLibrary::Update(std::uint32_t materialSet, const MaterialDesc& desc)
	{
		const auto found = entries.find(materialSet);
		if (found == entries.end())
		{
			return false;
		}
		const bool rebin = Flags(found->second.Desc) != Flags(desc);
		found->second.Desc = desc;
		Write(found->second, true);
		if (rebin && router)
		{
			router(materialSet, ToParameters(desc));
		}
		return true;
	}

	bool MaterialLibrary::Release(std::uint32_t materialSet)
	{
		const auto found = entries.find(materialSet);
		if (found == entries.end() || materialSet == defaultSet)
		{
			return false;
		}
		table.Release(found->second.Handle);
		entries.erase(found);
		return true;
	}

	const MaterialDesc* MaterialLibrary::Find(std::uint32_t materialSet) const
	{
		const auto found = entries.find(materialSet);
		return found == entries.end() ? nullptr : &found->second.Desc;
	}

	std::uint32_t MaterialLibrary::FindByName(std::string_view name) const
	{
		for (const auto& [set, entry] : entries)
		{
			if (entry.Desc.Name == name)
			{
				return set;
			}
		}
		return 0;
	}

	void MaterialLibrary::Update()
	{
		for (auto& [set, entry] : entries)
		{
			(void)set;
			Write(entry, false);
		}
	}
} // namespace Engine
