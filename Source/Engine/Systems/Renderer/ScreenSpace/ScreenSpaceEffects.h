#pragma once
#include "Engine/Systems/Renderer/RenderGraph/RenderGraph.h"
#include "Engine/Systems/Renderer/ScreenSpace/ScreenSpaceBindings.h"
#include "Engine/Systems/Renderer/ScreenSpace/ScreenSpaceGraphResources.h"
#include "Engine/Systems/Renderer/ScreenSpace/ScreenSpaceReference.h"
#include "Engine/Systems/Renderer/ScreenSpace/ScreenSpaceSettings.h"

#include <string>

namespace Swim::Render
{
	// One compiled screen-space program.
	struct ScreenSpaceProgram
	{
		Rhi::ComputePipeline* Pipeline = nullptr;
		Rhi::PipelineLayout* Layout = nullptr;
		std::uint32_t Space = 0;
	};

	struct ScreenSpaceEffectsDesc
	{
		ScreenSpaceProgram AmbientOcclusion; // SwimScreenSpaceAo
		ScreenSpaceProgram Blur;			 // SwimScreenSpaceBlur
		ScreenSpaceProgram Composite;		 // SwimScreenSpaceComposite
		std::string DebugName = "Screen space";
	};

	struct ScreenSpaceFrame
	{
		GraphTexture Color;	   // HDR scene color: RGBA16Float, Sampled, single-sample 2D.
		GraphTexture Depth;	   // Same size, Sampled: D32Float (depth aspect) or R32Float, reverse-Z.
		GraphTexture Normal;   // Same size, Sampled, RGBA16Float: ForwardPlusTargets::Normal.
		GraphTexture Indirect; // Same size, Sampled, RGBA16Float: ForwardPlusTargets::Indirect.
		ScreenSpaceView View;  // The camera the inputs were rendered with, including their jitter.
		ScreenSpaceSettings Settings;
		std::uint32_t NoiseFrame = 0; // Rotates the AO noise; pass the frame index when TAA follows.
	};

	// Screen-space ambient occlusion and fog (critical-path item 76), as graph-scheduled
	// compute between Forward+ and TAA:
	//  1. GTAO: cosine-weighted horizon-based visibility from depth and the Forward+
	//     normals (R32Float, unclamped);
	//  2. a 5x5 depth-aware blur, then clamp and power (R32Float);
	//  3. composite: removes the occluded share of the indirect radiance Forward+ wrote
	//     (never direct light or emission), then applies exponential height fog with a
	//     sun in-scattering lobe, into a new RGBA16Float color.
	// Disabled AO skips passes 1-2; with AO and fog both off nothing is recorded and the
	// input color is returned. ScreenSpaceReference.h is the CPU definition.
	class ScreenSpaceEffects
	{
	  public:
		// Throws std::invalid_argument when a program is missing.
		explicit ScreenSpaceEffects(ScreenSpaceEffectsDesc desc);

		// Throws std::invalid_argument for invalid settings or view, or inputs that break
		// the frame contract.
		ScreenSpaceGraphResources Record(RenderGraph& graph, const ScreenSpaceFrame& frame) const;

		static Rhi::TextureDesc OcclusionDesc(std::uint32_t width, std::uint32_t height);
		static Rhi::TextureDesc OutputDesc(std::uint32_t width, std::uint32_t height);

	  private:
		ScreenSpaceEffectsDesc desc;
	};
} // namespace Swim::Render
