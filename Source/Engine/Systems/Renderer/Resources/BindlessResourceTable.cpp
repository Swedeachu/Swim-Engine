#include "Engine/Systems/Renderer/Resources/BindlessResourceTable.h"

#include <algorithm>
#include <array>
#include <vector>

namespace Swim::Render
{
	namespace
	{
		const Rhi::DescriptorBindingDesc* FindBinding(const Rhi::DescriptorSchemaDesc& schema, std::uint32_t binding)
		{
			const auto found = std::find_if(schema.Bindings.begin(), schema.Bindings.end(),
				[&](const auto& candidate)
				{
					return candidate.Binding == binding;
				});
			return found == schema.Bindings.end() ? nullptr : &*found;
		}

		bool IsBindlessArray(const Rhi::DescriptorBindingDesc* binding, Rhi::DescriptorType type)
		{
			return binding && binding->Type == type && binding->Count > 0 && binding->PartiallyBound && binding->UpdateAfterBind &&
				!binding->VariableCount &&
				(type != Rhi::DescriptorType::SampledTexture ||
					(binding->SampledClass == Rhi::SampledTextureClass::Float &&
						binding->SampledDimension == Rhi::TextureViewDimension::Texture2D));
		}
	} // namespace

	BindlessResourceTable::BindlessResourceTable(Rhi::Device& device, const BindlessTableDesc& desc)
		: fallbackTexture(desc.FallbackTexture), fallbackSampler(desc.FallbackSampler), space(desc.Space),
		  textureBinding(desc.TextureBinding), samplerBinding(desc.SamplerBinding), name(desc.DebugName)
	{
		if (!desc.Layout || !fallbackTexture || !fallbackSampler || textureBinding == samplerBinding)
		{
			throw std::invalid_argument(name + " needs a layout, distinct bindings and fallback texture/sampler");
		}
		const auto& schemas = desc.Layout->GetInterface().DescriptorSchemas;
		const auto schema = std::find_if(schemas.begin(), schemas.end(),
			[&](const auto& candidate)
			{
				return candidate.Space == space;
			});
		// The table owns the whole space, so no other (fully bound) binding may live there.
		if (schema == schemas.end() || schema->Bindings.size() != 2 ||
			!IsBindlessArray(FindBinding(*schema, textureBinding), Rhi::DescriptorType::SampledTexture) ||
			!IsBindlessArray(FindBinding(*schema, samplerBinding), Rhi::DescriptorType::Sampler))
		{
			throw std::invalid_argument(name + " layout space must hold exactly a bindless float 2D texture array and a sampler array");
		}

		textures =
			std::make_unique<TextureRegistry>(GpuResourceRegistryDesc{ FindBinding(*schema, textureBinding)->Count, name + " textures" });
		samplers =
			std::make_unique<SamplerRegistry>(GpuResourceRegistryDesc{ FindBinding(*schema, samplerBinding)->Count, name + " samplers" });
		table = device.CreateDescriptorTable({ desc.Layout, space, 0, name });
		if (!table)
		{
			throw std::runtime_error(name + " descriptor table could not be created");
		}
		// The fallbacks take element 0 permanently; they are never released.
		const auto texture = textures->Create({ fallbackTexture });
		const auto sampler = samplers->Create({ fallbackSampler });
		if (texture.Index != FallbackIndex || sampler.Index != FallbackIndex)
		{
			throw std::logic_error(name + " fallback elements must occupy index 0");
		}
		WriteTexture(FallbackIndex, *fallbackTexture);
		WriteSampler(FallbackIndex, *fallbackSampler);
	}

	BindlessResourceTable::~BindlessResourceTable() = default;

	void BindlessResourceTable::WriteTexture(std::uint32_t index, Rhi::TextureView& view)
	{
		Rhi::DescriptorWrite write{};
		write.Binding = textureBinding;
		write.ArrayIndex = index;
		write.TextureResource = &view;
		table->Write({ &write, 1 });
		++descriptorWrites;
	}

	void BindlessResourceTable::WriteSampler(std::uint32_t index, Rhi::Sampler& sampler)
	{
		Rhi::DescriptorWrite write{};
		write.Binding = samplerBinding;
		write.ArrayIndex = index;
		write.SamplerResource = &sampler;
		table->Write({ &write, 1 });
		++descriptorWrites;
	}

	std::optional<BindlessTextureHandle> BindlessResourceTable::TryRegisterTexture(Rhi::TextureView& view)
	{
		const auto handle = textures->TryCreate({ &view });
		if (!handle)
		{
			return std::nullopt;
		}
		try
		{
			WriteTexture(handle->Index, view);
		}
		catch (...)
		{
			// Nothing was submitted against the element; it frees at the next Collect.
			textures->Release(*handle);
			throw;
		}
		return handle;
	}

	std::optional<BindlessSamplerHandle> BindlessResourceTable::TryRegisterSampler(Rhi::Sampler& sampler)
	{
		const auto handle = samplers->TryCreate({ &sampler });
		if (!handle)
		{
			return std::nullopt;
		}
		try
		{
			WriteSampler(handle->Index, sampler);
		}
		catch (...)
		{
			samplers->Release(*handle);
			throw;
		}
		return handle;
	}

	BindlessTextureHandle BindlessResourceTable::RegisterTexture(Rhi::TextureView& view)
	{
		if (auto handle = TryRegisterTexture(view))
		{
			return *handle;
		}
		throw std::length_error(name + " has no free texture elements");
	}

	BindlessSamplerHandle BindlessResourceTable::RegisterSampler(Rhi::Sampler& sampler)
	{
		if (auto handle = TryRegisterSampler(sampler))
		{
			return *handle;
		}
		throw std::length_error(name + " has no free sampler elements");
	}

	bool BindlessResourceTable::IsValid(BindlessTextureHandle texture) const
	{
		return textures->IsValid(texture);
	}

	bool BindlessResourceTable::IsValid(BindlessSamplerHandle sampler) const
	{
		return samplers->IsValid(sampler);
	}

	std::uint32_t BindlessResourceTable::GetIndex(BindlessTextureHandle texture) const
	{
		return textures->IsValid(texture) ? texture.Index : FallbackIndex;
	}

	std::uint32_t BindlessResourceTable::GetIndex(BindlessSamplerHandle sampler) const
	{
		return samplers->IsValid(sampler) ? sampler.Index : FallbackIndex;
	}

	bool BindlessResourceTable::Release(BindlessTextureHandle texture, Rhi::TimelinePoint lastUse)
	{
		return texture.Index != FallbackIndex && textures->Release(texture, lastUse);
	}

	bool BindlessResourceTable::Release(BindlessSamplerHandle sampler, Rhi::TimelinePoint lastUse)
	{
		return sampler.Index != FallbackIndex && samplers->Release(sampler, lastUse);
	}

	std::size_t BindlessResourceTable::Collect()
	{
		// A stale index must never reach a destroyed view: the element is pointed
		// back at the fallback before its index becomes reusable.
		auto collected = textures->CollectRetired(
			[&](BindlessTextureHandle handle, Internal::BindlessElement<Rhi::TextureView>&)
			{
				WriteTexture(handle.Index, *fallbackTexture);
			});
		collected += samplers->CollectRetired(
			[&](BindlessSamplerHandle handle, Internal::BindlessElement<Rhi::Sampler>&)
			{
				WriteSampler(handle.Index, *fallbackSampler);
			});
		return collected;
	}

	std::size_t BindlessResourceTable::Drain()
	{
		auto drained = textures->Drain(
			[&](BindlessTextureHandle handle, Internal::BindlessElement<Rhi::TextureView>&)
			{
				WriteTexture(handle.Index, *fallbackTexture);
			});
		drained += samplers->Drain(
			[&](BindlessSamplerHandle handle, Internal::BindlessElement<Rhi::Sampler>&)
			{
				WriteSampler(handle.Index, *fallbackSampler);
			});
		return drained;
	}

	BindlessTableStats BindlessResourceTable::GetStats() const
	{
		const auto textureStats = textures->GetStats();
		const auto samplerStats = samplers->GetStats();
		return { textureStats.Live, textureStats.Retiring, textureStats.MaxSlots, samplerStats.Live, samplerStats.Retiring,
			samplerStats.MaxSlots, descriptorWrites };
	}
} // namespace Swim::Render
