#pragma once
#include <cstdint>

namespace Swim::Render
{
	// Descriptor contracts of the environment programs (Shaders/Slang/Environment).
	// Every program uses one space and 8x8 thread groups over the texels it writes,
	// except the irradiance projection (one 64-thread group).

	struct EnvironmentSkyBindings // EnvironmentSky.slang
	{
		static constexpr std::uint32_t Destination = 0; // RWTexture2D<float4> (rgba16f): one face of mip 0.
		static constexpr std::uint32_t ThreadGroupSize = 8;
		static constexpr std::uint32_t PushConstantBytes = 96; // Environment::ProceduralSkyConstants.
	};

	struct EnvironmentDownsampleBindings // EnvironmentDownsample.slang
	{
		static constexpr std::uint32_t Source = 0;		// Texture2D<float4>: one face of mip - 1.
		static constexpr std::uint32_t Destination = 1; // RWTexture2D<float4> (rgba16f): the same face of mip.
		static constexpr std::uint32_t ThreadGroupSize = 8;
		static constexpr std::uint32_t PushConstantBytes = 16; // uint DestinationSize, 3 reserved.
	};

	struct EnvironmentPrefilterBindings // EnvironmentPrefilter.slang
	{
		static constexpr std::uint32_t Source = 0;		// TextureCube<float4>: the whole source chain.
		static constexpr std::uint32_t Sampler = 1;		// SamplerState: linear, clamp-to-edge.
		static constexpr std::uint32_t Destination = 2; // RWTexture2D<float4> (rgba16f): one face of one mip.
		static constexpr std::uint32_t ThreadGroupSize = 8;
		static constexpr std::uint32_t PushConstantBytes = 32; // EnvironmentPrefilterConstants.
	};

	struct EnvironmentPrefilterConstants
	{
		std::uint32_t Face = 0;
		std::uint32_t Size = 0; // Destination mip size.
		float Roughness = 0.0f; // Perceptual; 0 copies the source (lod 0).
		std::uint32_t SampleCount = 0;
		std::uint32_t SourceSize = 0;
		std::uint32_t SourceMipCount = 0;
		std::uint32_t Reserved[2] = {};
	};

	static_assert(sizeof(EnvironmentPrefilterConstants) == EnvironmentPrefilterBindings::PushConstantBytes);

	struct EnvironmentIrradianceBindings // EnvironmentIrradiance.slang
	{
		static constexpr std::uint32_t Source = 0; // Texture2DArray<float4>: the six faces of one mip.
		static constexpr std::uint32_t Output = 1; // RWStructuredBuffer<float4>: 9 SH coefficients (irradiance / pi).
		static constexpr std::uint32_t ThreadGroupSize = 64;
		static constexpr std::uint32_t PushConstantBytes = 16; // uint FaceSize, 3 reserved.
		static constexpr std::uint64_t OutputBytes = 9 * 16;
	};

	struct EnvironmentBrdfLutBindings // EnvironmentBrdfLut.slang
	{
		static constexpr std::uint32_t Destination = 0; // RWTexture2D<float4> (rgba16f): (A, B, 0, 1).
		static constexpr std::uint32_t ThreadGroupSize = 8;
		static constexpr std::uint32_t PushConstantBytes = 16; // uint Size, uint SampleCount, 2 reserved.
	};
} // namespace Swim::Render
