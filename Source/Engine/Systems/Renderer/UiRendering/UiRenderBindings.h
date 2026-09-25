#pragma once
#include <cstdint>

namespace Swim::Render
{
	// Descriptor contract of SwimUiQuad (vertexMain + fragmentMain).
	struct UiRenderBindings
	{
		static constexpr std::uint32_t Quads = 0; // StructuredBuffer<UiQuad> (GpuUiQuad).
		static constexpr std::uint32_t Count = 1;
		// Push constants: GpuUiDrawConstants.
		static constexpr std::uint32_t PushConstantBytes = 16;
		// The bindless space shared with Forward+ and particles (BindlessResourceTable):
		// atlas pages and UI images are sampled through it.
		static constexpr std::uint32_t BindlessSpace = 1;
		static constexpr std::uint32_t BindlessSamplers = 0; // SamplerState[].
		static constexpr std::uint32_t BindlessTextures = 1; // Texture2D<float4>[].
	};
} // namespace Swim::Render
