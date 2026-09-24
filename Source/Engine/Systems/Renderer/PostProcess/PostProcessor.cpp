#include "Engine/Systems/Renderer/PostProcess/PostProcessor.h"
#include "Engine/Systems/Renderer/PostProcess/PostProcessReference.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderCommandContext.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraphTransfers.h"

#include <array>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace Swim::Render
{
	namespace
	{
		using S = Rhi::ResourceState;

		constexpr std::uint64_t HistogramBytes = std::uint64_t(PostHistogramBins) * sizeof(std::uint32_t);

		Rhi::DescriptorWrite BufferWrite(RenderCommandContext& c, std::uint32_t binding, GraphBuffer buffer)
		{
			const auto range = c.GetRange(buffer);
			Rhi::DescriptorWrite write{};
			write.Binding = binding;
			write.BufferResource = range.Buffer;
			write.BufferOffset = range.Offset;
			write.BufferRange = range.Size;
			return write;
		}

		Rhi::DescriptorWrite TextureWrite(RenderCommandContext& c, std::uint32_t binding, GraphTexture texture, Rhi::Format format)
		{
			Rhi::TextureViewDesc view;
			view.PixelFormat = format;
			view.MipLevelCount = 1;
			Rhi::DescriptorWrite write{};
			write.Binding = binding;
			write.TextureResource = &c.CreateView(texture, view);
			return write;
		}

		// Binds the program's table, pushes the constants and dispatches.
		template <typename Constants, std::size_t Count>
		void Dispatch(RenderCommandContext& c, const PostProgram& program, const std::string& label,
			const std::array<Rhi::DescriptorWrite, Count>& writes, const Constants& constants, std::uint32_t groupsX, std::uint32_t groupsY)
		{
			auto table = c.Device().CreateDescriptorTable({ program.Layout, program.Space, 0, label });
			if (!table)
			{
				throw std::runtime_error(label + " descriptor table could not be created");
			}
			table->Write(writes);
			auto& retained = static_cast<Rhi::DescriptorTable&>(c.Retain(std::move(table)));
			auto& list = c.Commands();
			list.BindComputePipeline(*program.Pipeline);
			list.BindDescriptorTable(program.Space, retained);
			list.PushConstants(Rhi::ShaderStageMask::Compute, 0, std::as_bytes(std::span(&constants, 1)));
			list.Dispatch(groupsX, groupsY, 1);
		}

		std::uint32_t Groups(std::uint32_t size, std::uint32_t group)
		{
			return (size + group - 1) / group;
		}

		bool HasUsage(const Rhi::TextureDesc& desc, Rhi::TextureUsage usage)
		{
			return (static_cast<std::uint32_t>(desc.Usage) & static_cast<std::uint32_t>(usage)) != 0;
		}
	} // namespace

	PostProcessor::PostProcessor(Rhi::Device& device, PostProcessorDesc descInput) : desc(std::move(descInput))
	{
		for (const auto* program :
			{ &desc.Histogram, &desc.Exposure, &desc.BloomDownsample, &desc.BloomUpsample, &desc.Composite, &desc.CompositeHdr })
		{
			if (!program->Pipeline || !program->Layout)
			{
				throw std::invalid_argument(desc.DebugName + " needs every post-processing program");
			}
		}
		exposureState = device.CreateBuffer({ sizeof(GpuExposureState), Rhi::BufferUsage::Storage | Rhi::BufferUsage::TransferSource,
			Rhi::MemoryPreference::DeviceLocal, desc.DebugName + " exposure state" });
		if (!exposureState)
		{
			throw std::runtime_error(desc.DebugName + " exposure state buffer could not be created");
		}
	}

	PostProcessor::~PostProcessor() = default;

	Rhi::TextureDesc PostProcessor::BloomLevelDesc(std::uint32_t width, std::uint32_t height)
	{
		Rhi::TextureDesc level;
		level.Extent = { width, height, 1 };
		level.PixelFormat = Rhi::Format::RGBA16Float;
		level.Usage = Rhi::TextureUsage::Sampled | Rhi::TextureUsage::Storage | Rhi::TextureUsage::TransferSource;
		return level;
	}

	PostProcessGraphResources PostProcessor::Record(RenderGraph& graph, const PostProcessFrame& frame)
	{
		const auto& name = desc.DebugName;
		const auto& settings = frame.Settings;
		ValidatePostProcessSettings(settings);
		if (!std::isfinite(frame.DeltaTime) || frame.DeltaTime < 0.0f)
		{
			throw std::invalid_argument(name + " delta time must be finite and non-negative");
		}
		const auto sourceDesc = graph.GetDesc(frame.Source); // Copies: creating resources may reallocate the graph's list.
		const auto outputDesc = graph.GetDesc(frame.Output);
		const bool hdr = IsHdrEncoding(settings.Output.Encoding);
		const auto outputFormat = hdr ? Rhi::Format::RGBA16Float : Rhi::Format::RGBA8Unorm;
		if (sourceDesc.Dimension != Rhi::TextureDimension::Texture2D || sourceDesc.PixelFormat != Rhi::Format::RGBA16Float ||
			sourceDesc.ArrayLayers != 1 || sourceDesc.Samples != Rhi::SampleCount::X1 || !HasUsage(sourceDesc, Rhi::TextureUsage::Sampled))
		{
			throw std::invalid_argument(name + " source must be a sampled single-sample 2D RGBA16Float texture");
		}
		if (outputDesc.Dimension != Rhi::TextureDimension::Texture2D || outputDesc.PixelFormat != outputFormat ||
			outputDesc.Extent.Width != sourceDesc.Extent.Width || outputDesc.Extent.Height != sourceDesc.Extent.Height ||
			outputDesc.ArrayLayers != 1 || !HasUsage(outputDesc, Rhi::TextureUsage::Storage))
		{
			throw std::invalid_argument(
				name + " output must be a source-sized Storage texture: RGBA8Unorm for sRGB output, RGBA16Float for HDR10/scRGB");
		}
		const std::uint32_t width = sourceDesc.Extent.Width;
		const std::uint32_t height = sourceDesc.Extent.Height;
		const bool automatic = settings.Exposure.Mode == ExposureMode::Automatic;
		const auto& exposure = settings.Exposure;

		PostProcessGraphResources resources;
		resources.ExposureReset = !historyValid;
		resources.BloomLevels = settings.Bloom.Enabled ? Post::BloomLevelCount(width, height, settings.Bloom.MipCount) : 0u;
		resources.ParamsRecord = Post::BuildPostParams(settings, resources.BloomLevels);
		resources.Params =
			graph.CreateUpload(std::as_bytes(std::span(&resources.ParamsRecord, 1)), name + " params", Rhi::BufferUsage::Storage, 16);
		resources.ExposureState = graph.ImportBuffer(*exposureState, S::ShaderRead);
		const auto state = resources.ExposureState;
		const auto source = frame.Source;

		// 1. Histogram (automatic exposure only).
		GraphBuffer histogram;
		const std::vector<std::byte> zeros(HistogramBytes);
		if (automatic)
		{
			histogram = graph.CreateBuffer(
				{ HistogramBytes, Rhi::BufferUsage::Storage | Rhi::BufferUsage::TransferDestination | Rhi::BufferUsage::TransferSource,
					Rhi::MemoryPreference::DeviceLocal, name + " histogram" });
			resources.Histogram = histogram;
			AddBufferUpload(graph, name + " histogram clear", zeros, histogram);
			const PostHistogramConstants constants{ width, height, exposure.MinLog2Luminance,
				1.0f / (exposure.MaxLog2Luminance - exposure.MinLog2Luminance) };
			graph.AddPass(
				name + " histogram", Rhi::QueueType::Compute,
				[&](RenderGraphBuilder& b)
				{
					b.Read(source, S::ShaderRead);
					b.ReadWrite(histogram, S::ShaderRead | S::ShaderWrite);
				},
				[program = desc.Histogram, label = name + " histogram", source, histogram, constants](RenderCommandContext& c)
				{
					using B = PostHistogramBindings;
					const std::array<Rhi::DescriptorWrite, 2> writes{ TextureWrite(c, B::Source, source, Rhi::Format::RGBA16Float),
						BufferWrite(c, B::Histogram, histogram) };
					Dispatch(c, program, label, writes, constants, Groups(constants.Width, B::ThreadGroupSize),
						Groups(constants.Height, B::ThreadGroupSize));
				});
		}
		else
		{
			// Manual exposure never reads the histogram, but the binding needs a buffer.
			histogram = graph.CreateUpload(std::span<const std::byte>(zeros), name + " empty histogram", Rhi::BufferUsage::Storage, 16);
		}

		// 2. Exposure.
		PostExposureConstants exposureConstants;
		exposureConstants.Mode = static_cast<std::uint32_t>(exposure.Mode);
		exposureConstants.ManualEv100 = exposure.ManualEv100;
		exposureConstants.Compensation = exposure.Compensation;
		exposureConstants.MinEv100 = exposure.MinEv100;
		exposureConstants.MaxEv100 = exposure.MaxEv100;
		exposureConstants.LowPercentile = exposure.LowPercentile;
		exposureConstants.HighPercentile = exposure.HighPercentile;
		exposureConstants.SpeedUp = exposure.SpeedUp;
		exposureConstants.SpeedDown = exposure.SpeedDown;
		exposureConstants.DeltaTime = frame.DeltaTime;
		exposureConstants.MinLog2Luminance = exposure.MinLog2Luminance;
		exposureConstants.Log2Range = exposure.MaxLog2Luminance - exposure.MinLog2Luminance;
		exposureConstants.Reset = resources.ExposureReset ? 1u : 0u;
		resources.ExposurePass = graph.AddPass(
			name + " exposure", Rhi::QueueType::Compute,
			[&](RenderGraphBuilder& b)
			{
				b.Read(histogram, S::ShaderRead);
				b.ReadWrite(state, S::ShaderRead | S::ShaderWrite);
			},
			[program = desc.Exposure, label = name + " exposure", histogram, state, exposureConstants](RenderCommandContext& c)
			{
				using B = PostExposureBindings;
				const std::array<Rhi::DescriptorWrite, 2> writes{ BufferWrite(c, B::Histogram, histogram),
					BufferWrite(c, B::State, state) };
				Dispatch(c, program, label, writes, exposureConstants, 1, 1);
			});

		// 3. Bloom.
		const std::uint32_t levels = resources.BloomLevels;
		for (std::uint32_t i = 0; i < levels; ++i)
		{
			const std::uint32_t levelWidth = width >> (i + 1);
			const std::uint32_t levelHeight = height >> (i + 1);
			auto levelDesc = BloomLevelDesc(levelWidth, levelHeight);
			const std::string levelName = name + " bloom down " + std::to_string(i);
			levelDesc.DebugName = levelName;
			const auto destination = graph.CreateTexture(levelDesc);
			const auto from = i == 0 ? source : resources.BloomDown.back();
			resources.BloomDown.push_back(destination);
			const PostBloomDownsampleConstants constants{ i == 0 ? width : width >> i, i == 0 ? height : height >> i, levelWidth,
				levelHeight, i == 0 ? 1u : 0u, settings.Bloom.Threshold, settings.Bloom.Knee, 0 };
			graph.AddPass(
				levelName, Rhi::QueueType::Compute,
				[&](RenderGraphBuilder& b)
				{
					b.Read(from, S::ShaderRead);
					b.Read(state, S::ShaderRead);
					b.Write(destination, S::ShaderWrite);
				},
				[program = desc.BloomDownsample, label = levelName, from, destination, state, constants](RenderCommandContext& c)
				{
					using B = PostBloomDownsampleBindings;
					const std::array<Rhi::DescriptorWrite, 3> writes{ TextureWrite(c, B::Source, from, Rhi::Format::RGBA16Float),
						TextureWrite(c, B::Destination, destination, Rhi::Format::RGBA16Float), BufferWrite(c, B::State, state) };
					Dispatch(c, program, label, writes, constants, Groups(constants.DestinationWidth, B::ThreadGroupSize),
						Groups(constants.DestinationHeight, B::ThreadGroupSize));
				});
		}
		if (levels >= 2)
		{
			resources.BloomUp.resize(levels - 1);
			for (std::uint32_t i = levels - 1; i-- > 0;)
			{
				const auto low = i + 1 == levels - 1 ? resources.BloomDown[levels - 1] : resources.BloomUp[i + 1];
				const auto high = resources.BloomDown[i];
				const std::uint32_t levelWidth = width >> (i + 1);
				const std::uint32_t levelHeight = height >> (i + 1);
				auto levelDesc = BloomLevelDesc(levelWidth, levelHeight);
				const std::string levelName = name + " bloom up " + std::to_string(i);
				levelDesc.DebugName = levelName;
				const auto destination = graph.CreateTexture(levelDesc);
				resources.BloomUp[i] = destination;
				const PostBloomUpsampleConstants constants{ width >> (i + 2), height >> (i + 2), levelWidth, levelHeight };
				graph.AddPass(
					levelName, Rhi::QueueType::Compute,
					[&](RenderGraphBuilder& b)
					{
						b.Read(low, S::ShaderRead);
						b.Read(high, S::ShaderRead);
						b.Write(destination, S::ShaderWrite);
					},
					[program = desc.BloomUpsample, label = levelName, low, high, destination, constants](RenderCommandContext& c)
					{
						using B = PostBloomUpsampleBindings;
						const std::array<Rhi::DescriptorWrite, 3> writes{ TextureWrite(c, B::Low, low, Rhi::Format::RGBA16Float),
							TextureWrite(c, B::High, high, Rhi::Format::RGBA16Float),
							TextureWrite(c, B::Destination, destination, Rhi::Format::RGBA16Float) };
						Dispatch(c, program, label, writes, constants, Groups(constants.DestinationWidth, B::ThreadGroupSize),
							Groups(constants.DestinationHeight, B::ThreadGroupSize));
					});
			}
		}

		// 4. Composite.
		GraphTexture bloom;
		std::uint32_t bloomWidth = 1;
		std::uint32_t bloomHeight = 1;
		if (levels > 0)
		{
			bloom = levels >= 2 ? resources.BloomUp[0] : resources.BloomDown[0];
			bloomWidth = width >> 1;
			bloomHeight = height >> 1;
		}
		else
		{
			Rhi::TextureDesc standIn;
			standIn.Extent = { 1, 1, 1 };
			standIn.PixelFormat = Rhi::Format::RGBA16Float;
			standIn.Usage = Rhi::TextureUsage::Sampled | Rhi::TextureUsage::TransferDestination;
			const std::string standInName = name + " null bloom";
			standIn.DebugName = standInName;
			bloom = graph.CreateTexture(standIn);
			const std::array<std::byte, 8> texel{};
			AddTextureUpload(graph, standInName + " upload", texel, bloom, { 0, {}, {}, { 1, 1, 1 } });
		}
		const auto output = frame.Output;
		const auto params = resources.Params;
		const PostCompositeConstants compositeConstants{ width, height, bloomWidth, bloomHeight };
		resources.CompositePass = graph.AddPass(
			name + " composite", Rhi::QueueType::Compute,
			[&](RenderGraphBuilder& b)
			{
				b.Read(source, S::ShaderRead);
				b.Read(bloom, S::ShaderRead);
				b.Read(state, S::ShaderRead);
				b.Read(params, S::ShaderRead);
				b.Write(output, S::ShaderWrite);
			},
			[program = hdr ? desc.CompositeHdr : desc.Composite, label = name + " composite", source, bloom, state, params, output,
				outputFormat, compositeConstants](RenderCommandContext& c)
			{
				using B = PostCompositeBindings;
				const std::array<Rhi::DescriptorWrite, 5> writes{ TextureWrite(c, B::Source, source, Rhi::Format::RGBA16Float),
					TextureWrite(c, B::Bloom, bloom, Rhi::Format::RGBA16Float), BufferWrite(c, B::State, state),
					BufferWrite(c, B::Params, params), TextureWrite(c, B::Output, output, outputFormat) };
				Dispatch(c, program, label, writes, compositeConstants, Groups(compositeConstants.Width, B::ThreadGroupSize),
					Groups(compositeConstants.Height, B::ThreadGroupSize));
			});
		historyValid = true;
		return resources;
	}
} // namespace Swim::Render
