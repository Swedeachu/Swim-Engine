#pragma once
#include "Engine/Systems/Renderer/RenderGraph/RenderGraph.h"
#include "Engine/Systems/Renderer/Visibility/HzbGraphResources.h"

#include <cstdint>
#include <string>

namespace Swim::Render
{
	// Descriptor contract of Shaders/Slang/GpuScene/HzbReduce.slang.
	struct HzbBindings
	{
		static constexpr std::uint32_t Source = 0;			// Texture2D<float>: the depth, or the previous mip.
		static constexpr std::uint32_t Destination = 1;		// RWTexture2D<float> (r32f): the mip being written.
		static constexpr std::uint32_t ThreadGroupSize = 8; // 8x8 threads, one per destination texel.
		static constexpr std::uint32_t PushConstantBytes = 20;
	};

	struct HzbBuilderDesc
	{
		Rhi::ComputePipeline* ReducePipeline = nullptr;
		Rhi::PipelineLayout* Layout = nullptr;
		std::uint32_t Space = 0;
		std::string DebugName = "HZB";
	};

	// Hierarchical depth (item 50): reduces a depth texture into a transient R32Float
	// mip chain whose texels keep the farthest depth of their footprint, one compute
	// pass per mip, all graph-scheduled. The depth must be sampleable (D32Float with
	// TextureUsage::Sampled, or an R32Float copy). HzbReference is the CPU definition.
	class HzbBuilder
	{
	  public:
		explicit HzbBuilder(HzbBuilderDesc desc);

		HzbGraphResources Record(RenderGraph& graph, GraphTexture depth, DepthConvention convention = CanonicalDepthConvention) const;

	  private:
		Rhi::ComputePipeline* pipeline;
		Rhi::PipelineLayout* layout;
		std::uint32_t space;
		std::string name;
	};
} // namespace Swim::Render
