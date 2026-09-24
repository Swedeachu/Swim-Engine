#include "Engine/Systems/Renderer/Temporal/TemporalAntiAliasing.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderCommandContext.h"
#include "Engine/Systems/Renderer/Temporal/TemporalReference.h"

#include <memory>
#include <stdexcept>

namespace Swim::Render
{
	namespace
	{
		using S = Rhi::ResourceState;

		bool HasUsage(const Rhi::TextureDesc& desc, Rhi::TextureUsage usage)
		{
			return (static_cast<std::uint32_t>(desc.Usage) & static_cast<std::uint32_t>(usage)) != 0;
		}

		bool IsPlain2D(const Rhi::TextureDesc& desc)
		{
			return desc.Dimension == Rhi::TextureDimension::Texture2D && desc.ArrayLayers == 1 && desc.Samples == Rhi::SampleCount::X1 &&
				HasUsage(desc, Rhi::TextureUsage::Sampled);
		}
	} // namespace

	TemporalAntiAliasing::TemporalAntiAliasing(Rhi::Device& deviceInput, TemporalAntiAliasingDesc descInput)
		: device(deviceInput), desc(std::move(descInput))
	{
		if (!desc.Resolve.Pipeline || !desc.Resolve.Layout)
		{
			throw std::invalid_argument(desc.DebugName + " needs the resolve program");
		}
	}

	TemporalAntiAliasing::~TemporalAntiAliasing() = default;

	std::array<float, 2> TemporalAntiAliasing::GetJitterPixels(const TemporalSettings& settings) const
	{
		ValidateTemporalSettings(settings);
		return Temporal::JitterPixels(frameIndex, settings.JitterPhases);
	}

	std::array<float, 2> TemporalAntiAliasing::GetJitterNdc(
		const TemporalSettings& settings, std::uint32_t viewWidth, std::uint32_t viewHeight) const
	{
		ValidateTemporalSettings(settings);
		return Temporal::JitterNdc(frameIndex, settings.JitterPhases, viewWidth, viewHeight);
	}

	Rhi::TextureDesc TemporalAntiAliasing::HistoryDesc(std::uint32_t historyWidth, std::uint32_t historyHeight)
	{
		Rhi::TextureDesc texture;
		texture.Extent = { historyWidth, historyHeight, 1 };
		texture.PixelFormat = Rhi::Format::RGBA16Float;
		texture.Usage = Rhi::TextureUsage::Sampled | Rhi::TextureUsage::Storage | Rhi::TextureUsage::TransferSource;
		return texture;
	}

	TemporalGraphResources TemporalAntiAliasing::Record(RenderGraph& graph, const TemporalFrame& frame)
	{
		const auto& name = desc.DebugName;
		ValidateTemporalSettings(frame.Settings);
		const auto colorDesc = graph.GetDesc(frame.Color); // Copies: creating resources may reallocate the graph's list.
		const auto depthDesc = graph.GetDesc(frame.Depth);
		const auto velocityDesc = graph.GetDesc(frame.Velocity);
		if (!IsPlain2D(colorDesc) || colorDesc.PixelFormat != Rhi::Format::RGBA16Float)
		{
			throw std::invalid_argument(name + " color must be a sampled single-sample 2D RGBA16Float texture");
		}
		const auto sameSize = [&](const Rhi::TextureDesc& other)
		{
			return other.Extent.Width == colorDesc.Extent.Width && other.Extent.Height == colorDesc.Extent.Height;
		};
		const bool depthIsDepth = depthDesc.PixelFormat == Rhi::Format::D32Float;
		if (!IsPlain2D(depthDesc) || !sameSize(depthDesc) || (!depthIsDepth && depthDesc.PixelFormat != Rhi::Format::R32Float))
		{
			throw std::invalid_argument(name + " depth must be a color-sized sampled D32Float or R32Float texture");
		}
		if (!IsPlain2D(velocityDesc) || !sameSize(velocityDesc) || velocityDesc.PixelFormat != Rhi::Format::RG16Float)
		{
			throw std::invalid_argument(name + " velocity must be a color-sized sampled RG16Float texture");
		}
		const std::uint32_t frameWidth = colorDesc.Extent.Width;
		const std::uint32_t frameHeight = colorDesc.Extent.Height;

		// (Re)create the ping-pong pair; the replaced pair lives until this graph completes.
		if (!history[0] || frameWidth != width || frameHeight != height)
		{
			for (auto& texture : history)
			{
				if (texture)
				{
					retired.push_back(std::move(texture));
				}
			}
			auto historyDesc = HistoryDesc(frameWidth, frameHeight);
			for (std::uint32_t i = 0; i < 2; ++i)
			{
				const std::string debugName = name + (i == 0 ? " history A" : " history B");
				historyDesc.DebugName = debugName;
				history[i] = device.CreateTexture(historyDesc);
				if (!history[i])
				{
					throw std::runtime_error(name + " history textures could not be created");
				}
			}
			written = { false, false };
			width = frameWidth;
			height = frameHeight;
			historyValid = false;
		}

		TemporalGraphResources resources;
		resources.HistoryValid = historyValid;
		resources.FrameIndex = frameIndex;
		resources.JitterPixels = Temporal::JitterPixels(frameIndex, frame.Settings.JitterPhases);
		const std::uint32_t target = historyValid ? 1u - latest : latest;
		resources.History = historyValid ? graph.ImportTexture(*history[latest], S::ShaderRead) : frame.Color;
		// Write defines every texel: an undefined import is enough, exported for the next frame.
		resources.Output = graph.ImportTexture(*history[target], written[target] ? S::ShaderRead : S::Undefined);
		graph.Export(resources.Output, S::ShaderRead);

		TemporalResolveConstants constants;
		constants.Width = frameWidth;
		constants.Height = frameHeight;
		constants.Feedback = frame.Settings.Feedback;
		constants.ClipGamma = frame.Settings.ClipGamma;
		constants.HistoryValid = historyValid ? 1u : 0u;

		auto release = std::make_shared<std::vector<std::unique_ptr<Rhi::Texture>>>(std::move(retired));
		retired.clear();
		const auto color = frame.Color;
		const auto depth = frame.Depth;
		const auto velocity = frame.Velocity;
		const auto past = resources.History;
		const auto output = resources.Output;
		const bool separateHistory = historyValid;
		resources.ResolvePass = graph.AddPass(
			name + " resolve", Rhi::QueueType::Compute,
			[&](RenderGraphBuilder& b)
			{
				b.Read(color, S::ShaderRead);
				b.Read(depth, S::ShaderRead);
				b.Read(velocity, S::ShaderRead);
				if (separateHistory)
				{
					b.Read(past, S::ShaderRead);
				}
				b.Write(output, S::ShaderWrite);
			},
			[program = desc.Resolve, label = name + " resolve", color, depth, velocity, past, output, depthIsDepth, constants, release](
				RenderCommandContext& c)
			{
				for (auto& texture : *release)
				{
					c.Retain(std::move(texture)); // Kept alive until this graph's GPU work completes.
				}
				release->clear();
				using B = TemporalResolveBindings;
				const auto view = [&](GraphTexture texture, Rhi::Format format, Rhi::TextureAspect aspect = Rhi::TextureAspect::Automatic)
				{
					Rhi::TextureViewDesc viewDesc;
					viewDesc.PixelFormat = format;
					viewDesc.Aspect = aspect;
					return &c.CreateView(texture, viewDesc);
				};
				std::array<Rhi::DescriptorWrite, B::Count> writes{};
				writes[B::Current].TextureResource = view(color, Rhi::Format::RGBA16Float);
				writes[B::Depth].TextureResource =
					depthIsDepth ? view(depth, Rhi::Format::D32Float, Rhi::TextureAspect::Depth) : view(depth, Rhi::Format::R32Float);
				writes[B::Velocity].TextureResource = view(velocity, Rhi::Format::RG16Float);
				writes[B::History].TextureResource = view(past, Rhi::Format::RGBA16Float);
				writes[B::Output].TextureResource = view(output, Rhi::Format::RGBA16Float);
				for (std::uint32_t binding = 0; binding < B::Count; ++binding)
				{
					writes[binding].Binding = binding;
				}
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
				const std::uint32_t groups = B::ThreadGroupSize;
				list.Dispatch((constants.Width + groups - 1) / groups, (constants.Height + groups - 1) / groups, 1);
			});

		written[target] = true;
		latest = target;
		historyValid = true;
		++frameIndex;
		return resources;
	}
} // namespace Swim::Render
