#pragma once

#include "Engine/Systems/Renderer/RenderGraph/RenderCommandContext.h"

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace Swim::Render
{

	// The engine geometry passes an external drawer can add draws to.
	enum class ExternalGeometryPass : std::uint8_t
	{
		DepthPrepass, // Depth only (D32Float), before the opaque shading pass.
		Opaque,		  // The seven Forward+ opaque targets + depth.
		Transparent,  // The four Forward+ transparent targets, depth tested, not written.
		Shadow,		  // One shadow-atlas tile per call (depth only).
		Count
	};

	constexpr std::uint32_t ExternalGeometryPassBit(ExternalGeometryPass pass)
	{
		return 1u << static_cast<std::uint32_t>(pass);
	}

	// What a drawer sees inside an engine pass: rendering has begun, the viewport and
	// scissor are set (the tile for shadows), and the engine's own draws are recorded.
	// The drawer binds its pipeline and tables and draws; pipeline bindings are not
	// restored afterwards.
	struct ExternalDrawContext
	{
		RenderCommandContext& Context;
		ExternalGeometryPass Pass = ExternalGeometryPass::Opaque;
		// Opaque: the depth prepass already wrote this frame's depth, so draw with the
		// prepassed state (equal-or-nearer test, no depth writes) and the same vertices.
		bool DepthPrepassed = false;
		std::uint32_t Width = 0; // Render area (the atlas for shadows).
		std::uint32_t Height = 0;
		// The engine's space-0 table for a pipeline layout whose space 0 follows
		// ForwardPlusDrawBindings (forward passes) or ShadowDepthBindings (shadows).
		std::function<Rhi::DescriptorTable&(Rhi::PipelineLayout& layout)> FrameTable;
		Rhi::DescriptorTable* Bindless = nullptr; // Space 1 (BindlessResourceTable).
		// Shadow: the row of the shadow views buffer (GpuShadowView) this tile renders.
		std::uint32_t ShadowView = 0;
		std::array<float, 16> ShadowViewProjection{};
	};

	// Draws added to engine passes by something outside the renderer (a render feature).
	// The listed graph resources are declared as reads in every pass named in Passes.
	struct ExternalGeometryDraws
	{
		std::uint32_t Passes = 0; // ExternalGeometryPassBit mask.
		std::vector<GraphBuffer> Buffers;		  // ShaderRead (vertex and fragment stages).
		std::vector<GraphBuffer> IndirectBuffers; // IndirectArgument (arguments and counts).
		std::vector<GraphTexture> Textures;		  // ShaderRead.
		std::function<void(ExternalDrawContext&)> Draw;
	};

	inline bool DrawsIn(const std::vector<ExternalGeometryDraws>& draws, ExternalGeometryPass pass)
	{
		for (const auto& draw : draws)
		{
			if ((draw.Passes & ExternalGeometryPassBit(pass)) != 0)
			{
				return true;
			}
		}

		return false;
	}

} // namespace Swim::Render
