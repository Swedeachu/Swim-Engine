#pragma once
#include <cstdint>

namespace Swim::Render
{
	// Descriptor contracts (space 0) of the three screen-space programs. Every texture is
	// read with Load; all run 8 x 8 groups and take no push constants.
	inline constexpr std::uint32_t ScreenSpaceThreadGroupSize = 8;

	struct ScreenSpaceAoBindings // SwimScreenSpaceAo: GTAO visibility per pixel.
	{
		static constexpr std::uint32_t Depth = 0;  // Texture2D<float>: reverse-Z (D32Float depth aspect or R32Float).
		static constexpr std::uint32_t Normal = 1; // Texture2D<float4>: world normal + roughness (ForwardPlusTargets::Normal).
		static constexpr std::uint32_t Params = 2; // StructuredBuffer<ScreenSpaceParams>.
		static constexpr std::uint32_t Output = 3; // RWTexture2D<float> r32f: visibility.
		static constexpr std::uint32_t Count = 4;
	};

	struct ScreenSpaceBlurBindings // SwimScreenSpaceBlur: 5x5 depth-aware blur.
	{
		static constexpr std::uint32_t Source = 0; // Texture2D<float>: raw visibility.
		static constexpr std::uint32_t Depth = 1;
		static constexpr std::uint32_t Params = 2;
		static constexpr std::uint32_t Output = 3; // RWTexture2D<float> r32f.
		static constexpr std::uint32_t Count = 4;
	};

	struct ScreenSpaceCompositeBindings // SwimScreenSpaceComposite: AO on indirect light, then fog.
	{
		static constexpr std::uint32_t Color = 0;	 // Texture2D<float4>: HDR scene color.
		static constexpr std::uint32_t Indirect = 1; // Texture2D<float4>: ForwardPlusTargets::Indirect.
		static constexpr std::uint32_t Ao = 2;		 // Texture2D<float>: blurred visibility (1x1 stand-in without AO).
		static constexpr std::uint32_t Depth = 3;
		static constexpr std::uint32_t Params = 4;
		static constexpr std::uint32_t Output = 5; // RWTexture2D<float4> rgba16f.
		static constexpr std::uint32_t Count = 6;
	};
} // namespace Swim::Render
