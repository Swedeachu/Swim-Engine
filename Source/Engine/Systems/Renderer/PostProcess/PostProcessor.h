#pragma once
#include "Engine/Systems/Renderer/PostProcess/PostProcessBindings.h"
#include "Engine/Systems/Renderer/PostProcess/PostProcessGraphResources.h"
#include "Engine/Systems/Renderer/PostProcess/PostProcessSettings.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraph.h"

#include <memory>
#include <string>

namespace Swim::Render
{
	// One compiled post-processing program.
	struct PostProgram
	{
		Rhi::ComputePipeline* Pipeline = nullptr;
		Rhi::PipelineLayout* Layout = nullptr;
		std::uint32_t Space = 0;
	};

	struct PostProcessorDesc
	{
		PostProgram Histogram;		 // SwimPostHistogram
		PostProgram Exposure;		 // SwimPostExposure
		PostProgram BloomDownsample; // SwimPostBloomDownsample
		PostProgram BloomUpsample;	 // SwimPostBloomUpsample
		PostProgram Composite;		 // SwimPostComposite (RGBA8Unorm output)
		PostProgram CompositeHdr;	 // SwimPostCompositeHdr (RGBA16Float output)
		std::string DebugName = "Post";
	};

	struct PostProcessFrame
	{
		GraphTexture Source; // HDR scene color: RGBA16Float, Sampled, 2D.
		// Same size. Storage; RGBA8Unorm for OutputEncoding::Srgb, RGBA16Float for HDR10/scRGB.
		GraphTexture Output;
		PostProcessSettings Settings;
		float DeltaTime = 0.0f; // Seconds since the previous frame (exposure adaptation).
	};

	// HDR scene color -> display (critical-path items 73-74), as graph-scheduled compute:
	//  1. automatic exposure: luminance histogram, then one exposure pass that averages
	//     between percentiles and adapts a persistent GpuExposureState over time
	//     (manual exposure runs the exposure pass alone);
	//  2. bloom: a Karis-averaged, soft-thresholded 13-tap downsample chain and a tent
	//     upsample chain (none when disabled);
	//  3. composite: exposure, bloom, color grading, tone mapping and the output
	//     encoding (sRGB with dither, HDR10 PQ or scRGB).
	// PostProcessReference.h is the CPU definition of every pass.
	class PostProcessor
	{
	  public:
		// Throws std::invalid_argument when a program is missing, std::runtime_error when
		// the exposure state buffer cannot be created.
		PostProcessor(Rhi::Device& device, PostProcessorDesc desc);
		~PostProcessor();

		// Throws std::invalid_argument for invalid settings, a negative or non-finite
		// DeltaTime, or source/output textures that break the frame contract.
		PostProcessGraphResources Record(RenderGraph& graph, const PostProcessFrame& frame);

		// The next frame snaps the exposure instead of adapting (camera cuts, level loads).
		void ResetExposureHistory() { historyValid = false; }

		Rhi::Buffer& GetExposureStateBuffer() const { return *exposureState; }

		static Rhi::TextureDesc BloomLevelDesc(std::uint32_t width, std::uint32_t height);

	  private:
		PostProcessorDesc desc;
		std::unique_ptr<Rhi::Buffer> exposureState;
		bool historyValid = false;
	};
} // namespace Swim::Render
