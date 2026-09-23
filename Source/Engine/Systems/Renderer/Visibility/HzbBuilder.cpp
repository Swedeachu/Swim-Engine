#include "Engine/Systems/Renderer/Visibility/HzbBuilder.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderCommandContext.h"
#include "Engine/Systems/Renderer/Visibility/HzbPyramid.h"

#include <array>
#include <stdexcept>

namespace Swim::Render
{
	HzbBuilder::HzbBuilder(HzbBuilderDesc desc)
		: pipeline(desc.ReducePipeline), layout(desc.Layout), space(desc.Space), name(std::move(desc.DebugName))
	{
		if (!pipeline || !layout)
		{
			throw std::invalid_argument(name + " needs the reduction pipeline and layout");
		}
	}

	HzbGraphResources HzbBuilder::Record(RenderGraph& graph, GraphTexture depth, DepthConvention convention) const
	{
		using S = Rhi::ResourceState;
		const auto depthDesc = graph.GetDesc(depth); // A copy: creating the pyramid may reallocate the graph's resource list.
		const bool depthFormat = Rhi::IsDepthFormat(depthDesc.PixelFormat);
		if (depthDesc.Dimension != Rhi::TextureDimension::Texture2D || depthDesc.ArrayLayers != 1 ||
			depthDesc.Samples != Rhi::SampleCount::X1 || (!depthFormat && depthDesc.PixelFormat != Rhi::Format::R32Float) ||
			(static_cast<std::uint32_t>(depthDesc.Usage) & static_cast<std::uint32_t>(Rhi::TextureUsage::Sampled)) == 0)
		{
			throw std::invalid_argument(name + " source must be a sampled single-sample 2D depth or R32Float texture");
		}
		const auto mips = ComputeHzbMips(depthDesc.Extent.Width, depthDesc.Extent.Height);
		if (mips.empty())
		{
			throw std::invalid_argument(name + " source must be larger than 1x1");
		}

		HzbGraphResources hzb;
		hzb.Width = depthDesc.Extent.Width;
		hzb.Height = depthDesc.Extent.Height;
		hzb.MipCount = static_cast<std::uint32_t>(mips.size());
		hzb.Convention = convention;
		Rhi::TextureDesc pyramidDesc;
		pyramidDesc.Extent = { mips[0].Width, mips[0].Height, 1 };
		pyramidDesc.PixelFormat = Rhi::Format::R32Float;
		pyramidDesc.Usage = Rhi::TextureUsage::Sampled | Rhi::TextureUsage::Storage | Rhi::TextureUsage::TransferSource;
		pyramidDesc.MipLevels = hzb.MipCount;
		pyramidDesc.DebugName = name;
		hzb.Pyramid = graph.CreateTexture(pyramidDesc);

		const std::uint32_t farthestIsMax = convention == DepthConvention::Forward ? 1u : 0u;
		for (std::uint32_t mip = 0; mip < hzb.MipCount; ++mip)
		{
			const auto source = mip == 0 ? depth : hzb.Pyramid;
			const Rhi::TextureSubresourceRange sourceRange{ mip == 0 ? 0u : mip - 1, 1, 0, 1 };
			const Rhi::TextureSubresourceRange destinationRange{ mip, 1, 0, 1 };
			const auto sourceExtent = mip == 0 ? HzbExtent{ hzb.Width, hzb.Height } : mips[mip - 1];
			const std::array<std::uint32_t, 5> constants{ sourceExtent.Width, sourceExtent.Height, mips[mip].Width, mips[mip].Height,
				farthestIsMax };
			Rhi::TextureViewDesc sourceView;
			sourceView.PixelFormat = mip == 0 ? depthDesc.PixelFormat : Rhi::Format::R32Float;
			sourceView.BaseMipLevel = sourceRange.BaseMipLevel;
			sourceView.Aspect = mip == 0 && depthFormat ? Rhi::TextureAspect::Depth : Rhi::TextureAspect::Automatic;
			Rhi::TextureViewDesc destinationView;
			destinationView.PixelFormat = Rhi::Format::R32Float;
			destinationView.BaseMipLevel = mip;
			hzb.Passes.push_back(graph.AddPass(
				name + " mip " + std::to_string(mip), Rhi::QueueType::Compute,
				[&](RenderGraphBuilder& b)
				{
					if (mip == 0)
					{
						b.Read(depth, S::ShaderRead);
					}
					else
					{
						b.Read(hzb.Pyramid, S::ShaderRead, sourceRange);
					}
					b.Write(hzb.Pyramid, S::ShaderWrite, destinationRange);
				},
				[=, pipeline = pipeline, layout = layout, space = space, pyramid = hzb.Pyramid, label = name](RenderCommandContext& c)
				{
					auto table = c.Device().CreateDescriptorTable({ layout, space, 0, label + " table" });
					if (!table)
					{
						throw std::runtime_error(label + " descriptor table could not be created");
					}
					std::array<Rhi::DescriptorWrite, 2> writes{};
					writes[0].Binding = HzbBindings::Source;
					writes[0].TextureResource = &c.CreateView(source, sourceView);
					writes[1].Binding = HzbBindings::Destination;
					writes[1].TextureResource = &c.CreateView(pyramid, destinationView);
					table->Write(writes);
					auto& retained = static_cast<Rhi::DescriptorTable&>(c.Retain(std::move(table)));
					auto& list = c.Commands();
					list.BindComputePipeline(*pipeline);
					list.BindDescriptorTable(space, retained);
					list.PushConstants(Rhi::ShaderStageMask::Compute, 0, std::as_bytes(std::span(constants)));
					constexpr auto group = HzbBindings::ThreadGroupSize;
					list.Dispatch((constants[2] + group - 1) / group, (constants[3] + group - 1) / group, 1);
				}));
		}
		return hzb;
	}
} // namespace Swim::Render
