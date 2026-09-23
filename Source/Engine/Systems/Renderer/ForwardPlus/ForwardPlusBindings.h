#pragma once
#include "Engine/Systems/Renderer/RHI/RhiContracts.h"

#include <cstdint>

namespace Swim::Render
{
	// Descriptor contract of ClusteredForward.slang (both variants: SwimForwardOpaque
	// and SwimForwardTransparent). Space 0 is per page slot; space 1 is the shared
	// bindless table (BindlessResourceTable over ForwardPlusBindlessSpace).
	struct ForwardPlusDrawBindings
	{
		static constexpr std::uint32_t Instances = 0;	// StructuredBuffer<GpuInstanceRecord>.
		static constexpr std::uint32_t Transforms = 1;	// StructuredBuffer<GpuTransformRecord>.
		static constexpr std::uint32_t DrawRecords = 2; // StructuredBuffer<GpuDrawRecord> (visibility slots).
		static constexpr std::uint32_t Vertices = 3;	// StructuredBuffer<float>: the slot's vertex page.
		static constexpr std::uint32_t View = 4;		// StructuredBuffer<ForwardView> (one record).
		static constexpr std::uint32_t Materials = 5;	// StructuredBuffer<StandardMaterialParameters>.
		static constexpr std::uint32_t Lights = 6;		// StructuredBuffer<GpuLightRecord>.
		static constexpr std::uint32_t LightHeader = 7;
		static constexpr std::uint32_t ClusterGrid = 8;
		static constexpr std::uint32_t ClusterRecords = 9;
		static constexpr std::uint32_t ClusterIndices = 10;
		static constexpr std::uint32_t EnvironmentIrradiance = 11;	// StructuredBuffer<float4>: order-2 SH.
		static constexpr std::uint32_t EnvironmentPrefiltered = 12; // TextureCube<float4>, every mip.
		static constexpr std::uint32_t EnvironmentBrdfLut = 13;		// Texture2D<float4>.
		static constexpr std::uint32_t EnvironmentSampler = 14;		// Linear clamp.
		static constexpr std::uint32_t Count = 15;

		static constexpr std::uint32_t BindlessSpace = 1;
		static constexpr std::uint32_t BindlessSamplers = 0; // SamplerState[].
		static constexpr std::uint32_t BindlessTextures = 1; // Texture2D<float4>[].
	};

	// ForwardTransparentSort.slang: one 256-thread group per index-page slot sorts
	// that slot's transparent bin back to front and writes compacted commands.
	struct ForwardTransparentSortBindings
	{
		static constexpr std::uint32_t Commands = 0;	// Visibility commands (read).
		static constexpr std::uint32_t DrawRecords = 1; // Visibility draw records.
		static constexpr std::uint32_t Counts = 2;		// Visibility per-bin counts.
		static constexpr std::uint32_t Instances = 3;
		static constexpr std::uint32_t Transforms = 4;
		static constexpr std::uint32_t View = 5;
		static constexpr std::uint32_t Scratch = 6;		   // RW ForwardSortEntry[slots * SortSize].
		static constexpr std::uint32_t SortedCommands = 7; // RW DrawIndexedIndirectCommand[slots * capacity].
		static constexpr std::uint32_t SortedCounts = 8;   // RW uint[slots].
		static constexpr std::uint32_t Count = 9;
		static constexpr std::uint32_t ThreadGroupSize = 256;
		// uint FirstBin, uint FirstCommand (slot 0's range.First), uint Capacity, uint SortSize.
		static constexpr std::uint32_t PushConstantBytes = 16;
		static constexpr std::uint32_t MaxDraws = 65536; // Transparent capacity per page slot.
	};

	// The bindless space (1) both Forward+ programs share: `textures` sampled 2D
	// textures and `samplers` samplers, partially bound and update-after-bind.
	inline Rhi::DescriptorSchemaDesc ForwardPlusBindlessSpace(std::uint32_t textures, std::uint32_t samplers)
	{
		Rhi::DescriptorSchemaDesc space{
			ForwardPlusDrawBindings::BindlessSpace,
			{ { ForwardPlusDrawBindings::BindlessSamplers, Rhi::DescriptorType::Sampler, samplers, Rhi::ShaderStageMask::None },
				{ ForwardPlusDrawBindings::BindlessTextures, Rhi::DescriptorType::SampledTexture, textures, Rhi::ShaderStageMask::None } }
		};
		for (auto& binding : space.Bindings)
		{
			binding.Stages = Rhi::ShaderStageMask::Vertex | Rhi::ShaderStageMask::Fragment | Rhi::ShaderStageMask::Compute;
			binding.PartiallyBound = binding.UpdateAfterBind = true;
		}
		return space;
	}
} // namespace Swim::Render
