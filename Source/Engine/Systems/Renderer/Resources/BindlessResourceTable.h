#pragma once
#include "Engine/Systems/Renderer/Resources/BindlessTableDesc.h"
#include "Engine/Systems/Renderer/Resources/BindlessTableStats.h"
#include "Engine/Systems/Renderer/Resources/GpuResourceRegistry.h"
#include "Engine/Systems/Renderer/Resources/Internal/BindlessElement.h"

#include <memory>
#include <optional>

namespace Swim::Render
{
	// One persistent descriptor table holding every sampled texture and sampler a
	// shader may index (critical-path item 45). Shaders receive plain uint32
	// indices (GetIndex); element 0 of each array is a permanent fallback.
	//
	// Registration writes the element immediately (update-after-bind), so a new
	// index is usable by any submission recorded afterwards. Release invalidates
	// the handle at once, but the element keeps its resource until lastUse
	// completes; Collect then rewrites it to the fallback and returns the index
	// to a FIFO free list. An index is therefore never rewritten while submitted
	// work can still read it, which is what update-unused-while-pending allows.
	//
	// Externally synchronized with descriptor writes and command recording that
	// binds GetTable(). Registered views/samplers must stay alive until their
	// release retires. Destroy the table only after the last submission that
	// bound it has completed; Drain waits for pending releases only.
	class BindlessResourceTable
	{
	  public:
		static constexpr std::uint32_t FallbackIndex = 0;

		// Throws std::invalid_argument for an unsuitable layout/space or fallbacks,
		// and std::runtime_error when the device cannot create the table.
		BindlessResourceTable(Rhi::Device& device, const BindlessTableDesc& desc);
		~BindlessResourceTable();
		BindlessResourceTable(const BindlessResourceTable&) = delete;
		BindlessResourceTable& operator=(const BindlessResourceTable&) = delete;

		// Empty when every element is live or retiring. Throws std::invalid_argument
		// when the RHI rejects the view (for example not a float 2D sampled view).
		std::optional<BindlessTextureHandle> TryRegisterTexture(Rhi::TextureView& view);
		std::optional<BindlessSamplerHandle> TryRegisterSampler(Rhi::Sampler& sampler);
		// As above, but a full table throws std::length_error.
		BindlessTextureHandle RegisterTexture(Rhi::TextureView& view);
		BindlessSamplerHandle RegisterSampler(Rhi::Sampler& sampler);

		// FallbackIndex for invalid, stale or released handles.
		std::uint32_t GetIndex(BindlessTextureHandle texture) const;
		std::uint32_t GetIndex(BindlessSamplerHandle sampler) const;
		bool IsValid(BindlessTextureHandle texture) const;
		bool IsValid(BindlessSamplerHandle sampler) const;

		// lastUse must cover every submission that may index the element. Returns
		// false for invalid/stale handles; the fallback elements are never released.
		bool Release(BindlessTextureHandle texture, Rhi::TimelinePoint lastUse = {});
		bool Release(BindlessSamplerHandle sampler, Rhi::TimelinePoint lastUse = {});

		// Nonblocking: rewrites completed releases to the fallbacks and frees their
		// indices. Returns the number of elements freed.
		std::size_t Collect();
		// Waits for every pending release, then collects.
		std::size_t Drain();

		Rhi::DescriptorTable& GetTable() const { return *table; }

		std::uint32_t GetSpace() const { return space; }

		BindlessTableStats GetStats() const;

	  private:
		using TextureRegistry = GpuResourceRegistry<BindlessTextureTag, Internal::BindlessElement<Rhi::TextureView>>;
		using SamplerRegistry = GpuResourceRegistry<BindlessSamplerTag, Internal::BindlessElement<Rhi::Sampler>>;

		void WriteTexture(std::uint32_t index, Rhi::TextureView& view);
		void WriteSampler(std::uint32_t index, Rhi::Sampler& sampler);

		std::unique_ptr<Rhi::DescriptorTable> table;
		std::unique_ptr<TextureRegistry> textures;
		std::unique_ptr<SamplerRegistry> samplers;
		Rhi::TextureView* fallbackTexture = nullptr;
		Rhi::Sampler* fallbackSampler = nullptr;
		std::uint32_t space = 0;
		std::uint32_t textureBinding = 0;
		std::uint32_t samplerBinding = 0;
		std::uint64_t descriptorWrites = 0;
		std::string name;
	};
} // namespace Swim::Render
