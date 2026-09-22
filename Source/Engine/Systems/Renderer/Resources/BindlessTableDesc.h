#pragma once
#include "Engine/Systems/Renderer/RHI/RhiContracts.h"

#include <cstdint>
#include <string>

namespace Swim::Render
{
	struct BindlessTableDesc
	{
		// Any pipeline layout that defines Space through PipelineLayoutDesc::DescriptorSpaces
		// as exactly two partially bound, update-after-bind arrays: Sampler at
		// SamplerBinding and float Texture2D SampledTexture at TextureBinding. The array
		// counts are the table capacities. The layout must outlive the table; the table
		// binds to every pipeline whose layout defines the space identically.
		Rhi::PipelineLayout* Layout = nullptr;
		std::uint32_t Space = 0;
		std::uint32_t SamplerBinding = 0;
		std::uint32_t TextureBinding = 1;
		// Element 0 of each array, and what retired elements are rewritten to so a stale
		// index samples something valid. Must outlive the table.
		Rhi::TextureView* FallbackTexture = nullptr;
		Rhi::Sampler* FallbackSampler = nullptr;
		std::string DebugName = "Bindless resources";
	};
} // namespace Swim::Render
