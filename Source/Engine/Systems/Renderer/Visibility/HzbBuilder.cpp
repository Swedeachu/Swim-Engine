#include "Engine/Systems/Renderer/Visibility/HzbBuilder.h"

#include "Engine/Systems/Renderer/RenderGraph/RenderCommandContext.h"
#include "Engine/Systems/Renderer/Visibility/HzbPyramid.h"

#include <algorithm>
#include <array>
#include <bit>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace Swim::Render
{

	namespace
	{

		void ValidateSource(const Rhi::TextureDesc& depthDesc, bool depthFormat, const std::string& name)
		{
			if (depthDesc.Dimension != Rhi::TextureDimension::Texture2D || depthDesc.ArrayLayers != 1 ||
				depthDesc.Samples != Rhi::SampleCount::X1 || (!depthFormat && depthDesc.PixelFormat != Rhi::Format::R32Float) ||
				(static_cast<std::uint32_t>(depthDesc.Usage) & static_cast<std::uint32_t>(Rhi::TextureUsage::Sampled)) == 0)
			{
				throw std::invalid_argument(name + " source must be a sampled single-sample 2D depth or R32Float texture");
			}
		}

		// Mips round up (odd edges are kept) while image mips round down: when the two chains
		// differ, the image is padded to powers of two so every mip holds its rounded-up size.
		std::pair<std::uint32_t, std::uint32_t> PyramidSize(const std::vector<HzbExtent>& mips)
		{
			for (std::uint32_t mip = 0; mip < mips.size(); ++mip)
			{
				if (std::max(mips[0].Width >> mip, 1u) != mips[mip].Width || std::max(mips[0].Height >> mip, 1u) != mips[mip].Height)
				{
					return { std::bit_ceil(mips[0].Width), std::bit_ceil(mips[0].Height) };
				}
			}

			return { mips[0].Width, mips[0].Height };
		}

	} // namespace

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

		ValidateSource(depthDesc, depthFormat, name);

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
		const auto [pyramidWidth, pyramidHeight] = PyramidSize(mips);
		pyramidDesc.Extent = { pyramidWidth, pyramidHeight, 1 };
		pyramidDesc.PixelFormat = Rhi::Format::R32Float;
		pyramidDesc.Usage = Rhi::TextureUsage::Sampled | Rhi::TextureUsage::Storage | Rhi::TextureUsage::TransferSource;
		pyramidDesc.MipLevels = hzb.MipCount;
		pyramidDesc.DebugName = name;
		hzb.Pyramid = graph.CreateTexture(pyramidDesc);

		const std::uint32_t farthestIsMax = convention == DepthConvention::Forward ? 1u : 0u;

		// Mips are reduced in pairs: one dispatch writes mip and mip + 1 (HzbReduce.slang).
		for (std::uint32_t mip = 0; mip < hzb.MipCount; mip += 2)
		{
			const bool paired = mip + 1 < hzb.MipCount;
			const auto source = mip == 0 ? depth : hzb.Pyramid;
			const Rhi::TextureSubresourceRange sourceRange{ mip == 0 ? 0u : mip - 1, 1, 0, 1 };
			const Rhi::TextureSubresourceRange destinationRange{ mip, paired ? 2u : 1u, 0, 1 };
			const auto sourceExtent = mip == 0 ? HzbExtent{ hzb.Width, hzb.Height } : mips[mip - 1];
			const std::array<std::uint32_t, 7> constants{ sourceExtent.Width, sourceExtent.Height, mips[mip].Width, mips[mip].Height,
				farthestIsMax, paired ? mips[mip + 1].Width : 0u, paired ? mips[mip + 1].Height : 0u };
			Rhi::TextureViewDesc sourceView;
			sourceView.PixelFormat = mip == 0 ? depthDesc.PixelFormat : Rhi::Format::R32Float;
			sourceView.BaseMipLevel = sourceRange.BaseMipLevel;
			sourceView.Aspect = mip == 0 && depthFormat ? Rhi::TextureAspect::Depth : Rhi::TextureAspect::Automatic;
			Rhi::TextureViewDesc destinationView;
			destinationView.PixelFormat = Rhi::Format::R32Float;
			destinationView.BaseMipLevel = mip;
			Rhi::TextureViewDesc nextView = destinationView; // Unpaired: bound but not written.
			nextView.BaseMipLevel = paired ? mip + 1 : mip;
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
					auto table = c.CreateDescriptorTable({ layout, space, 0, label + " table" });

					if (!table)
					{
						throw std::runtime_error(label + " descriptor table could not be created");
					}

					std::array<Rhi::DescriptorWrite, 3> writes{};
					writes[0].Binding = HzbBindings::Source;
					writes[0].TextureResource = &c.CreateView(source, sourceView);
					writes[1].Binding = HzbBindings::Destination;
					writes[1].TextureResource = &c.CreateView(pyramid, destinationView);
					writes[2].Binding = HzbBindings::Next;
					writes[2].TextureResource = &c.CreateView(pyramid, nextView);
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
