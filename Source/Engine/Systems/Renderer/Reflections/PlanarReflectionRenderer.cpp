#include "Engine/Systems/Renderer/Reflections/PlanarReflectionRenderer.h"
#include "Engine/Systems/Renderer/Environment/CubeImage.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderCommandContext.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraphTransfers.h"

#include <algorithm>
#include <array>
#include <stdexcept>

namespace Swim::Render
{

	namespace
	{

		using S = Rhi::ResourceState;
		constexpr auto AtlasFormat = Rhi::Format::RGBA16Float;

		std::uint32_t Groups(std::uint32_t size, std::uint32_t group)
		{
			return (size + group - 1) / group;
		}

		Rhi::TextureViewDesc LayerView(std::uint32_t layer)
		{
			Rhi::TextureViewDesc view;
			view.Dimension = Rhi::TextureViewDimension::Texture2D;
			view.PixelFormat = AtlasFormat;
			view.BaseArrayLayer = layer;
			return view;
		}

		struct ResolveConstants
		{
			std::uint32_t Width = 0;
			std::uint32_t Height = 0;
			std::uint32_t Mode = 0; // 0: resolve, 1: clear.
			std::uint32_t Environment = 0;
			float NearClip = 0.0f;
			float EnvironmentScale = 1.0f;
			float EnvironmentRotation = 0.0f;
			float Reserved = 0.0f;
			float FrustumScale[4] = {}; // P00, P11, P02, P12.
			float Right[4] = {};
			float Up[4] = {};
			float Forward[4] = {};
		};

		static_assert(sizeof(ResolveConstants) == PlanarReflectionResolveBindings::PushConstantBytes);

		Rhi::DescriptorTable& Table(RenderCommandContext& c, const EnvironmentProgram& program, const std::string& label,
			std::span<const Rhi::DescriptorWrite> writes)
		{
			auto table = c.Device().CreateDescriptorTable({ program.Layout, program.Space, 0, label });

			if (!table)
			{
				throw std::runtime_error(label + " descriptor table could not be created");
			}

			table->Write(writes);
			return static_cast<Rhi::DescriptorTable&>(c.Retain(std::move(table)));
		}

	} // namespace

	PlanarReflectionRenderer::PlanarReflectionRenderer(Rhi::Device& deviceValue, PlanarReflectionRendererDesc descValue)
		: device(deviceValue), desc(std::move(descValue))
	{
		if (!desc.Resolve.Pipeline || !desc.Resolve.Layout || !desc.Sampler)
		{
			throw std::invalid_argument(desc.DebugName + " needs the resolve program and a sampler");
		}
	}

	Rhi::TextureDesc PlanarReflectionRenderer::AtlasDesc(std::uint32_t size, std::uint32_t count)
	{
		Rhi::TextureDesc texture;
		texture.Dimension = Rhi::TextureDimension::Texture2D;
		texture.Extent = { size, size, 1 };
		texture.PixelFormat = AtlasFormat;
		texture.Usage = Rhi::TextureUsage::Sampled | Rhi::TextureUsage::Storage;
		texture.ArrayLayers = count;
		return texture;
	}

	bool PlanarReflectionRenderer::Ensure(std::uint32_t size, std::uint32_t count)
	{
		if (size < 64 || size > 2048 || (size & (size - 1)) != 0 || count == 0 || count > MaxPlanarReflections)
		{
			throw std::invalid_argument(desc.DebugName + " needs a power-of-two resolution in 64..2048 and 1..8 layers");
		}

		if (atlas && size == resolution && count == layers)
		{
			return false;
		}

		auto textureDesc = AtlasDesc(size, count);
		const std::string name = desc.DebugName + " atlas";
		textureDesc.DebugName = name;
		atlas = device.CreateTexture(textureDesc);

		if (!atlas)
		{
			throw std::runtime_error(desc.DebugName + ": cannot create the atlas");
		}

		resolution = size;
		layers = count;
		initialized = false;
		return true;
	}

	PlanarReflectionRenderer::Atlas PlanarReflectionRenderer::Import(RenderGraph& graph)
	{
		if (!atlas)
		{
			throw std::logic_error(desc.DebugName + ": Ensure before Import");
		}

		Atlas result;
		{
			Rhi::TextureDesc cubeDesc;
			cubeDesc.Dimension = Rhi::TextureDimension::TextureCube;
			cubeDesc.Extent = { 1, 1, 1 };
			cubeDesc.ArrayLayers = Environment::CubeFaceCount;
			cubeDesc.PixelFormat = AtlasFormat;
			cubeDesc.Usage = Rhi::TextureUsage::Sampled | Rhi::TextureUsage::TransferDestination;
			const std::string cubeName = desc.DebugName + " environment stand-in";
			cubeDesc.DebugName = cubeName;
			result.EnvironmentStandIn = graph.CreateTexture(cubeDesc);
			const std::array<std::uint16_t, 4> zero{};

			for (std::uint32_t face = 0; face < Environment::CubeFaceCount; ++face)
			{
				AddTextureUpload(graph, cubeName + " upload", std::as_bytes(std::span(zero)), result.EnvironmentStandIn,
					{ 0, { 0, face }, {}, { 1, 1, 1 } });
			}
		}
		result.Texture = graph.ImportTexture(*atlas, initialized ? S::ShaderRead : S::Undefined);

		if (!initialized)
		{
			// Every layer starts defined (black, sky distance), so the array view the composite
			// binds never samples an undefined layer.
			const auto standIn = [&](Rhi::Format format, std::span<const std::byte> texel, const char* name)
			{
				Rhi::TextureDesc textureDesc;
				textureDesc.Extent = { 1, 1, 1 };
				textureDesc.PixelFormat = format;
				textureDesc.Usage = Rhi::TextureUsage::Sampled | Rhi::TextureUsage::TransferDestination;
				textureDesc.DebugName = name;
				const auto texture = graph.CreateTexture(textureDesc);
				AddTextureUpload(graph, std::string(name) + " upload", texel, texture, { 0, {}, {}, { 1, 1, 1 } });
				return texture;
			};
			const std::array<std::uint16_t, 4> zeroColor{};
			const float zeroDepth = 0.0f;
			const auto colorStandIn = standIn(AtlasFormat, std::as_bytes(std::span(zeroColor)), "Planar reflection clear color");
			const auto depthStandIn = standIn(Rhi::Format::R32Float, std::as_bytes(std::span(&zeroDepth, 1)), "Planar reflection clear depth");
			const auto texture = result.Texture;
			const auto environment = result.EnvironmentStandIn;
			graph.AddPass(
				desc.DebugName + " clear", Rhi::QueueType::Compute,
				[&](RenderGraphBuilder& b)
				{
					b.Read(colorStandIn, S::ShaderRead);
					b.Read(depthStandIn, S::ShaderRead);
					b.Read(environment, S::ShaderRead);
					b.Write(texture, S::ShaderWrite);
				},
				[program = desc.Resolve, label = desc.DebugName + " clear", texture, colorStandIn, depthStandIn, environment,
					sampler = desc.Sampler, count = layers, size = resolution](RenderCommandContext& c)
				{
					auto& list = c.Commands();
					list.BindComputePipeline(*program.Pipeline);
					Rhi::TextureViewDesc colorView;
					colorView.PixelFormat = AtlasFormat;
					Rhi::TextureViewDesc depthView;
					depthView.PixelFormat = Rhi::Format::R32Float;
					auto& colorResource = c.CreateView(colorStandIn, colorView);
					auto& depthResource = c.CreateView(depthStandIn, depthView);
					Rhi::TextureViewDesc cubeView;
					cubeView.Dimension = Rhi::TextureViewDimension::TextureCube;
					cubeView.PixelFormat = AtlasFormat;
					cubeView.ArrayLayerCount = Environment::CubeFaceCount;
					auto& environmentResource = c.CreateView(environment, cubeView);

					for (std::uint32_t layer = 0; layer < count; ++layer)
					{
						std::array<Rhi::DescriptorWrite, 5> writes{};
						writes[0].Binding = PlanarReflectionResolveBindings::Color;
						writes[0].TextureResource = &colorResource;
						writes[1].Binding = PlanarReflectionResolveBindings::Depth;
						writes[1].TextureResource = &depthResource;
						writes[2].Binding = PlanarReflectionResolveBindings::Destination;
						writes[2].TextureResource = &c.CreateView(texture, LayerView(layer));
						writes[3].Binding = PlanarReflectionResolveBindings::Environment;
						writes[3].TextureResource = &environmentResource;
						writes[4].Binding = PlanarReflectionResolveBindings::Sampler;
						writes[4].SamplerResource = sampler;
						list.BindDescriptorTable(program.Space, Table(c, program, label, writes));
						ResolveConstants constants;
						constants.Width = size;
						constants.Height = size;
						constants.Mode = 1u;
						list.PushConstants(Rhi::ShaderStageMask::Compute, 0, std::as_bytes(std::span(&constants, 1)));
						constexpr auto group = PlanarReflectionResolveBindings::ThreadGroupSize;
						list.Dispatch(Groups(size, group), Groups(size, group), 1);
					}

				});
			initialized = true;
		}

		return result;
	}

	GraphPass PlanarReflectionRenderer::RecordResolve(RenderGraph& graph, const Atlas& target, const PlanarReflections::Capture& capture,
		GraphTexture color, GraphTexture depth, const ReflectionProbeCaptureSky& sky) const
	{
		if (capture.Slot >= layers || capture.Width == 0 || capture.Height == 0 || capture.Width > resolution || capture.Height > resolution)
		{
			throw std::invalid_argument(desc.DebugName + ": capture slot or size out of range");
		}

		const auto& colorDesc = graph.GetDesc(color);
		const auto& depthDesc = graph.GetDesc(depth);

		if (colorDesc.Extent.Width != capture.Width || colorDesc.Extent.Height != capture.Height || colorDesc.PixelFormat != AtlasFormat ||
			depthDesc.Extent.Width != capture.Width || depthDesc.Extent.Height != capture.Height || depthDesc.PixelFormat != Rhi::Format::D32Float)
		{
			throw std::invalid_argument(desc.DebugName + ": a capture must be Width x Height RGBA16Float color with D32Float depth");
		}

		const auto layer = capture.Slot;
		const auto texture = target.Texture;
		const auto environment = sky.Environment.value_or(target.EnvironmentStandIn);
		const std::uint32_t environmentMips = graph.GetDesc(environment).MipLevels;
		ResolveConstants constants;
		constants.Width = capture.Width;
		constants.Height = capture.Height;
		constants.Environment = sky.Environment ? 1u : 0u;
		constants.NearClip = capture.NearClip;
		constants.EnvironmentScale = sky.Scale;
		constants.EnvironmentRotation = sky.Rotation;

		for (int c = 0; c < 4; ++c)
		{
			constants.FrustumScale[c] = capture.FrustumScale[c];
		}

		for (int c = 0; c < 3; ++c)
		{
			constants.Right[c] = capture.Right[c];
			constants.Up[c] = capture.Up[c];
			constants.Forward[c] = capture.Forward[c];
		}

		const std::string label = desc.DebugName + " resolve";
		return graph.AddPass(
			label, Rhi::QueueType::Compute,
			[&](RenderGraphBuilder& b)
			{
				b.Read(color, S::ShaderRead);
				b.Read(depth, S::ShaderRead);
				b.Read(environment, S::ShaderRead);
				b.Write(texture, S::ShaderWrite, { 0, 1, layer, 1 });
			},
			[program = desc.Resolve, sampler = desc.Sampler, label, color, depth, environment, environmentMips, texture, layer,
				constants](RenderCommandContext& c)
			{
				std::array<Rhi::DescriptorWrite, 5> writes{};
				Rhi::TextureViewDesc colorView;
				colorView.PixelFormat = AtlasFormat;
				Rhi::TextureViewDesc depthView;
				depthView.PixelFormat = Rhi::Format::D32Float;
				depthView.Aspect = Rhi::TextureAspect::Depth;
				writes[0].Binding = PlanarReflectionResolveBindings::Color;
				writes[0].TextureResource = &c.CreateView(color, colorView);
				writes[1].Binding = PlanarReflectionResolveBindings::Depth;
				writes[1].TextureResource = &c.CreateView(depth, depthView);
				writes[2].Binding = PlanarReflectionResolveBindings::Destination;
				writes[2].TextureResource = &c.CreateView(texture, LayerView(layer));
				Rhi::TextureViewDesc cubeView;
				cubeView.Dimension = Rhi::TextureViewDimension::TextureCube;
				cubeView.PixelFormat = AtlasFormat;
				cubeView.MipLevelCount = environmentMips;
				cubeView.ArrayLayerCount = Environment::CubeFaceCount;
				writes[3].Binding = PlanarReflectionResolveBindings::Environment;
				writes[3].TextureResource = &c.CreateView(environment, cubeView);
				writes[4].Binding = PlanarReflectionResolveBindings::Sampler;
				writes[4].SamplerResource = sampler;
				auto& list = c.Commands();
				list.BindComputePipeline(*program.Pipeline);
				list.BindDescriptorTable(program.Space, Table(c, program, label, writes));
				list.PushConstants(Rhi::ShaderStageMask::Compute, 0, std::as_bytes(std::span(&constants, 1)));
				constexpr auto group = PlanarReflectionResolveBindings::ThreadGroupSize;
				list.Dispatch(Groups(constants.Width, group), Groups(constants.Height, group), 1);
			});
	}

} // namespace Swim::Render
