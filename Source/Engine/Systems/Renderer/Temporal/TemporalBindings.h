#pragma once
#include <cstdint>

namespace Swim::Render
{
	// Descriptor contract of SwimTemporalResolve (space 0). Every input is read with Load.
	struct TemporalResolveBindings
	{
		static constexpr std::uint32_t Current = 0;	 // Texture2D<float4>: this frame's jittered HDR color (RGBA16Float).
		static constexpr std::uint32_t Depth = 1;	 // Texture2D<float>: reverse-Z depth (D32Float depth aspect or R32Float).
		static constexpr std::uint32_t Velocity = 2; // Texture2D<float2>: motion vectors, UV now minus UV before (RG16Float).
		static constexpr std::uint32_t History = 3;	 // Texture2D<float4>: last frame's output (RGBA16Float).
		static constexpr std::uint32_t Output = 4;	 // RWTexture2D<float4> rgba16f: the resolved frame and next history.
		static constexpr std::uint32_t Count = 5;
		static constexpr std::uint32_t ThreadGroupSize = 8; // 8 x 8 texels per group.
		static constexpr std::uint32_t PushConstantBytes = 32;
	};
} // namespace Swim::Render
