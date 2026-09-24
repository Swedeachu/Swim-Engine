#pragma once
#include "Engine/Systems/Renderer/RenderGraph/RenderGraph.h"
#include "Engine/Systems/Renderer/Temporal/TemporalBindings.h"
#include "Engine/Systems/Renderer/Temporal/TemporalGraphResources.h"
#include "Engine/Systems/Renderer/Temporal/TemporalRecords.h"
#include "Engine/Systems/Renderer/Temporal/TemporalSettings.h"

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace Swim::Render
{
	// One compiled temporal program.
	struct TemporalProgram
	{
		Rhi::ComputePipeline* Pipeline = nullptr;
		Rhi::PipelineLayout* Layout = nullptr;
		std::uint32_t Space = 0;
	};

	struct TemporalAntiAliasingDesc
	{
		TemporalProgram Resolve; // SwimTemporalResolve
		std::string DebugName = "TAA";
	};

	struct TemporalFrame
	{
		GraphTexture Color;	   // Jittered HDR scene color: RGBA16Float, Sampled, single-sample 2D.
		GraphTexture Depth;	   // Same size, Sampled: D32Float (depth aspect) or R32Float, reverse-Z.
		GraphTexture Velocity; // Same size, Sampled, RG16Float: ForwardPlusTargets::Velocity.
		TemporalSettings Settings;
	};

	// Temporal anti-aliasing (critical-path item 75): one graph-scheduled compute resolve
	// per frame that reprojects the previous output through the motion vectors, clips it
	// against the current frame's neighborhood and blends. Two persistent RGBA16Float
	// textures ping-pong as output and history. TemporalReference.h is the CPU definition.
	//
	// Per frame: render the scene with GetJitterNdc() as ForwardPlusView::Jitter and the
	// previous unjittered view-projection, then Record. Record assumes the graph executes;
	// it advances the jitter sequence and makes this output the next frame's history.
	class TemporalAntiAliasing
	{
	  public:
		// Throws std::invalid_argument when the program is missing.
		TemporalAntiAliasing(Rhi::Device& device, TemporalAntiAliasingDesc desc);
		~TemporalAntiAliasing();

		// The jitter the next recorded frame must be rendered with.
		std::array<float, 2> GetJitterPixels(const TemporalSettings& settings) const;
		std::array<float, 2> GetJitterNdc(const TemporalSettings& settings, std::uint32_t width, std::uint32_t height) const;

		// Throws std::invalid_argument for invalid settings or inputs that break the frame
		// contract, std::runtime_error when the history textures cannot be created.
		TemporalGraphResources Record(RenderGraph& graph, const TemporalFrame& frame);

		// The next frame starts from the current frame alone (camera cuts, teleports).
		// A size change resets the history as well.
		void ResetHistory() { historyValid = false; }

		bool HasHistory() const { return historyValid; }

		std::uint64_t GetFrameIndex() const { return frameIndex; }

		static Rhi::TextureDesc HistoryDesc(std::uint32_t width, std::uint32_t height);

	  private:
		Rhi::Device& device;
		TemporalAntiAliasingDesc desc;
		std::array<std::unique_ptr<Rhi::Texture>, 2> history;
		std::array<bool, 2> written{ false, false };		// Holds defined contents (imported as ShaderRead).
		std::vector<std::unique_ptr<Rhi::Texture>> retired; // Replaced by a resize; released by the next graph.
		std::uint32_t width = 0;
		std::uint32_t height = 0;
		std::uint32_t latest = 0; // Index of the texture holding the last output.
		bool historyValid = false;
		std::uint64_t frameIndex = 0;
	};
} // namespace Swim::Render
