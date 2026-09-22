#pragma once
#include "Engine/Systems/Renderer/Resources/GpuResourceRegistry.h"
#include "Engine/Systems/Renderer/Resources/GpuSamplerCacheDesc.h"
#include "Engine/Systems/Renderer/Resources/GpuSamplerCacheStats.h"
#include "Engine/Systems/Renderer/Resources/Internal/GpuSamplerRecord.h"

namespace Swim::Render
{
	class BindlessResourceTable;

	// Sampler residency with identity separate from images: equal SamplerDescs
	// (names ignored) share one RHI sampler and, when a BindlessResourceTable is
	// given, one bindless sampler element. References are counted; the last
	// Release retires the sampler and its element after the latest reported
	// last use. Every lastUse given to one cache must come from the same timeline.
	//
	// Externally synchronized, owner thread. The device and optional bindless
	// table must outlive the cache.
	class GpuSamplerCache
	{
	  public:
		GpuSamplerCache(Rhi::Device& device, BindlessResourceTable* bindless = nullptr, GpuSamplerCacheDesc desc = {});
		~GpuSamplerCache();
		GpuSamplerCache(const GpuSamplerCache&) = delete;
		GpuSamplerCache& operator=(const GpuSamplerCache&) = delete;

		// Adds a reference. Throws std::length_error when the cache or bindless
		// sampler array is full and std::runtime_error when creation fails.
		GpuSamplerHandle Acquire(const Rhi::SamplerDesc& desc);
		// Drops one reference; false for invalid/stale handles. lastUse must cover
		// every submission this holder recorded with the sampler.
		bool Release(GpuSamplerHandle sampler, Rhi::TimelinePoint lastUse = {});

		Rhi::Sampler* Get(GpuSamplerHandle sampler) const;
		// BindlessResourceTable::FallbackIndex without a table or for invalid handles.
		std::uint32_t GetBindlessIndex(GpuSamplerHandle sampler) const;

		std::size_t Collect();
		std::size_t Drain();
		GpuSamplerCacheStats GetStats() const;

	  private:
		using Registry = GpuResourceRegistry<GpuSamplerTag, Internal::GpuSamplerRecord>;

		Rhi::Device& device;
		BindlessResourceTable* bindless;
		Registry samplers;
		std::string name;
		std::uint64_t created = 0;
		std::uint64_t reused = 0;
	};
} // namespace Swim::Render
