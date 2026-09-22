#pragma once

#include <compare>
#include <cstdint>

namespace Swim::Render
{
	// Compact generational identity for a persistent renderer resource. Index is
	// the dense, shader-visible slot (metadata row, bindless element); Generation
	// rejects stale handles after release. Handles never own or point at objects.
	template <typename Tag> struct GpuHandle
	{
		static constexpr std::uint32_t InvalidIndex = UINT32_MAX;

		std::uint32_t Index = InvalidIndex;
		std::uint32_t Generation = 0;

		constexpr bool IsValid() const { return Index != InvalidIndex && Generation != 0; }

		constexpr explicit operator bool() const { return IsValid(); }

		constexpr std::uint64_t Pack() const { return (std::uint64_t(Generation) << 32) | Index; }

		static constexpr GpuHandle Unpack(std::uint64_t packed)
		{
			return { static_cast<std::uint32_t>(packed & 0xffffffffu), static_cast<std::uint32_t>(packed >> 32) };
		}

		constexpr auto operator<=>(const GpuHandle&) const = default;
	};

	struct GpuMeshTag;
	struct GpuTextureTag;
	struct GpuSamplerTag;
	struct GpuMaterialTag;
	struct RenderObjectTag;
	struct GpuSkinTag;
	struct BindlessTextureTag;
	struct BindlessSamplerTag;

	using GpuMeshHandle = GpuHandle<GpuMeshTag>;
	using GpuTextureHandle = GpuHandle<GpuTextureTag>;
	using GpuSamplerHandle = GpuHandle<GpuSamplerTag>;
	using GpuMaterialHandle = GpuHandle<GpuMaterialTag>;
	using RenderObjectHandle = GpuHandle<RenderObjectTag>;
	using GpuSkinHandle = GpuHandle<GpuSkinTag>;
	// Bindless element identities: Index is the shader-visible array element.
	// Image and sampler identities are independent, so any pair can be combined.
	using BindlessTextureHandle = GpuHandle<BindlessTextureTag>;
	using BindlessSamplerHandle = GpuHandle<BindlessSamplerTag>;
} // namespace Swim::Render
