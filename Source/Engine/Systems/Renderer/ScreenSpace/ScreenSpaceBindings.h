#pragma once
#include <cstdint>

namespace Swim::Render
{
	// Descriptor contracts (space 0) of the four screen-space programs. Every texture is
	// read with Load; all run 8 x 8 groups and take no push constants.
	inline constexpr std::uint32_t ScreenSpaceThreadGroupSize = 8;
	// The composite's probe inputs: at most this many GpuReflectionProbeRecords (32 bytes each,
	// Reflections/ReflectionProbeTypes.h) in a cube array of 6 layers per probe.
	inline constexpr std::uint32_t ScreenSpaceMaxProbes = 16;
	inline constexpr std::uint32_t ScreenSpaceProbeRecordBytes = 32;

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

	struct ScreenSpaceReflectionBindings // SwimScreenSpaceReflection: one screen-space mirror ray per pixel.
	{
		static constexpr std::uint32_t Depth = 0;
		static constexpr std::uint32_t Normal = 1;	 // Texture2D<float4>: world normal + roughness.
		static constexpr std::uint32_t Color = 2;	 // Texture2D<float4>: HDR scene color (the radiance rays find).
		static constexpr std::uint32_t Indirect = 3; // Texture2D<float4>: ForwardPlusTargets::Indirect (AO at the hit).
		static constexpr std::uint32_t Ao = 4;		 // Texture2D<float>: blurred visibility (1x1 stand-in without AO).
		static constexpr std::uint32_t Params = 5;
		static constexpr std::uint32_t Output = 6; // RWTexture2D<float4> rgba16f: hit radiance, confidence.
		// Reflections of reflections: the previous frame's finished color, read at the hit
		// reprojected by the hit's motion vector (SsrHistory = 1); Color and a 1x1 zero
		// velocity stand in without history.
		static constexpr std::uint32_t Velocity = 7; // Texture2D<float2> rg16f: ForwardPlusTargets::Velocity.
		static constexpr std::uint32_t History = 8;	 // Texture2D<float4> rgba16f: TAA's previous output.
		// ForwardPlusTargets::BackDepth (SsrBackDepth = 1): each surface's thickness; a 1x1 stand-in without.
		static constexpr std::uint32_t BackDepth = 9;
		static constexpr std::uint32_t Count = 10;
	};

	struct ScreenSpaceCompositeBindings // SwimScreenSpaceComposite: AO on indirect light, reflections, then fog.
	{
		static constexpr std::uint32_t Color = 0;	 // Texture2D<float4>: HDR scene color.
		static constexpr std::uint32_t Indirect = 1; // Texture2D<float4>: ForwardPlusTargets::Indirect.
		static constexpr std::uint32_t Ao = 2;		 // Texture2D<float>: blurred visibility (1x1 stand-in without AO).
		static constexpr std::uint32_t Depth = 3;
		static constexpr std::uint32_t Params = 4;
		static constexpr std::uint32_t Output = 5;		// RWTexture2D<float4> rgba16f.
		static constexpr std::uint32_t Reflection = 6;	// Texture2D<float4>: the reflection pass (1x1 stand-in without SSR).
		static constexpr std::uint32_t Reflectance = 7; // Texture2D<float4>: ForwardPlusTargets::Reflectance (stand-in without SSR).
		static constexpr std::uint32_t Specular = 8;	// Texture2D<float4>: ForwardPlusTargets::Specular (stand-in without SSR).
		static constexpr std::uint32_t Normal = 9;		// Texture2D<float4>: world normal + roughness (the glossy resolve).
		// The local reflection fallback (ProbeCount > 0; 1x1 stand-ins otherwise):
		static constexpr std::uint32_t ProbeCubes = 10;	  // TextureCubeArray<float4>: prefiltered probes (alpha: captured distance).
		static constexpr std::uint32_t ProbeSampler = 11; // SamplerState: linear, clamp, mips.
		static constexpr std::uint32_t ProbeRecords = 12; // StructuredBuffer<GpuReflectionProbeRecord>.
		static constexpr std::uint32_t ObjectId = 13;	  // Texture2D<float>: ForwardPlusTargets::ObjectId (object probes).
		static constexpr std::uint32_t Count = 14;
	};
} // namespace Swim::Render
