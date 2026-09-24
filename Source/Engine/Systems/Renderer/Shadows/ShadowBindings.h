#pragma once
#include "Engine/Systems/Renderer/RHI/RhiContracts.h"

#include <cstdint>

namespace Swim::Render
{
	// Descriptor contract of ShadowDepth.slang (SwimShadowDepth and SwimShadowMasked).
	// Space 0 is per shadow view and page slot; the masked variant also reads the
	// bindless textures in space 1, defined exactly like ForwardPlusBindlessSpace so one
	// BindlessResourceTable serves both.
	struct ShadowDepthBindings
	{
		static constexpr std::uint32_t Instances = 0;	// StructuredBuffer<GpuInstanceRecord>.
		static constexpr std::uint32_t Transforms = 1;	// StructuredBuffer<GpuTransformRecord>.
		static constexpr std::uint32_t DrawRecords = 2; // StructuredBuffer<GpuDrawRecord>: the view's visibility slots.
		static constexpr std::uint32_t Vertices = 3;	// StructuredBuffer<float>: StandardVertex page.
		static constexpr std::uint32_t Views = 4;		// StructuredBuffer<GpuShadowView>.
		static constexpr std::uint32_t Materials = 5;	// StructuredBuffer<StandardMaterialParameters>.
		static constexpr std::uint32_t Count = 6;
		static constexpr std::uint32_t PushConstantBytes = 16; // uint ViewIndex, uint MaterialCount, 2 reserved.

		static constexpr std::uint32_t BindlessSpace = 1;
		static constexpr std::uint32_t BindlessSamplers = 0;
		static constexpr std::uint32_t BindlessTextures = 1;
	};

	// The bindless space (1) the masked shadow program shares with Forward+.
	inline Rhi::DescriptorSchemaDesc ShadowBindlessSpace(std::uint32_t textures, std::uint32_t samplers)
	{
		Rhi::DescriptorSchemaDesc space{ ShadowDepthBindings::BindlessSpace,
			{ { ShadowDepthBindings::BindlessSamplers, Rhi::DescriptorType::Sampler, samplers, Rhi::ShaderStageMask::None },
				{ ShadowDepthBindings::BindlessTextures, Rhi::DescriptorType::SampledTexture, textures, Rhi::ShaderStageMask::None } } };
		for (auto& binding : space.Bindings)
		{
			binding.Stages = Rhi::ShaderStageMask::Vertex | Rhi::ShaderStageMask::Fragment | Rhi::ShaderStageMask::Compute;
			binding.PartiallyBound = binding.UpdateAfterBind = true;
		}
		return space;
	}
} // namespace Swim::Render
