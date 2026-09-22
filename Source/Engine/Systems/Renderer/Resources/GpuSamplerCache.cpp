#include "Engine/Systems/Renderer/Resources/GpuSamplerCache.h"
#include "Engine/Systems/Renderer/Resources/BindlessResourceTable.h"

namespace Swim::Render
{
	namespace
	{
		bool SameSampler(const Rhi::SamplerDesc& a, const Rhi::SamplerDesc& b)
		{
			return a.MinFilter == b.MinFilter && a.MagFilter == b.MagFilter && a.MipFilter == b.MipFilter && a.AddressU == b.AddressU &&
				a.AddressV == b.AddressV && a.AddressW == b.AddressW && a.MipLodBias == b.MipLodBias && a.MinLod == b.MinLod &&
				a.MaxLod == b.MaxLod && a.MaxAnisotropy == b.MaxAnisotropy && a.EnableAnisotropy == b.EnableAnisotropy &&
				a.EnableComparison == b.EnableComparison && a.Comparison == b.Comparison;
		}

		Rhi::TimelinePoint Later(Rhi::TimelinePoint current, Rhi::TimelinePoint next)
		{
			if (!next.Semaphore)
			{
				return current;
			}
			if (current.Semaphore && current.Semaphore != next.Semaphore)
			{
				throw std::invalid_argument("GpuSamplerCache releases must share one timeline");
			}
			return current.Semaphore && current.Value > next.Value ? current : next;
		}
	} // namespace

	GpuSamplerCache::GpuSamplerCache(Rhi::Device& device, BindlessResourceTable* bindless, GpuSamplerCacheDesc desc)
		: device(device), bindless(bindless), samplers({ desc.MaxSamplers, desc.DebugName }), name(std::move(desc.DebugName))
	{
	}

	GpuSamplerCache::~GpuSamplerCache() = default;

	GpuSamplerHandle GpuSamplerCache::Acquire(const Rhi::SamplerDesc& desc)
	{
		GpuSamplerHandle existing;
		samplers.ForEach(
			[&](GpuSamplerHandle handle, Internal::GpuSamplerRecord& record)
			{
				if (!existing && SameSampler(record.Desc, desc))
				{
					existing = handle;
				}
			});
		if (existing)
		{
			++samplers.Get(existing)->References;
			++reused;
			return existing;
		}

		Internal::GpuSamplerRecord record;
		record.Desc = desc;
		record.Desc.DebugName = {};
		record.References = 1;
		record.Sampler = device.CreateSampler(desc);
		if (!record.Sampler)
		{
			throw std::runtime_error(name + " could not create a sampler");
		}
		auto* sampler = record.Sampler.get();
		const auto handle = samplers.TryCreate(std::move(record));
		if (!handle)
		{
			throw std::length_error(name + " has no free sampler slots"); // The unused sampler is destroyed here.
		}
		++created;
		if (bindless)
		{
			try
			{
				samplers.Get(*handle)->Bindless = bindless->RegisterSampler(*sampler);
			}
			catch (...)
			{
				samplers.Release(*handle); // Never submitted; retires at the next Collect.
				throw;
			}
		}
		return *handle;
	}

	bool GpuSamplerCache::Release(GpuSamplerHandle sampler, Rhi::TimelinePoint lastUse)
	{
		auto* record = samplers.Get(sampler);
		if (!record)
		{
			return false;
		}
		record->LastUse = Later(record->LastUse, lastUse);
		if (--record->References > 0)
		{
			return true;
		}
		const auto retireAfter = record->LastUse;
		if (bindless)
		{
			bindless->Release(record->Bindless, retireAfter);
		}
		samplers.Release(sampler, retireAfter);
		return true;
	}

	Rhi::Sampler* GpuSamplerCache::Get(GpuSamplerHandle sampler) const
	{
		const auto* record = samplers.Get(sampler);
		return record ? record->Sampler.get() : nullptr;
	}

	std::uint32_t GpuSamplerCache::GetBindlessIndex(GpuSamplerHandle sampler) const
	{
		const auto* record = samplers.Get(sampler);
		return record && bindless ? bindless->GetIndex(record->Bindless) : BindlessResourceTable::FallbackIndex;
	}

	std::size_t GpuSamplerCache::Collect()
	{
		return samplers.CollectRetired();
	}

	std::size_t GpuSamplerCache::Drain()
	{
		return samplers.Drain();
	}

	GpuSamplerCacheStats GpuSamplerCache::GetStats() const
	{
		const auto stats = samplers.GetStats();
		GpuSamplerCacheStats result{ stats.Live, stats.Retiring, 0, created, reused };
		samplers.ForEach(
			[&](GpuSamplerHandle, const Internal::GpuSamplerRecord& record)
			{
				result.References += record.References;
			});
		return result;
	}
} // namespace Swim::Render
