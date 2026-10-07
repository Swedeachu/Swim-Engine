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
			std::uint32_t Scale = 1; // Rendered texels per stored texel (each axis).
			float FrustumScale[4] = {}; // P00, P11, P02, P12.
			float Right[4] = {};
			float Up[4] = {};
			float Forward[4] = {};
			float Position[4] = {}; // The capture position.
			std::uint32_t ProbeCount = 0;
			std::uint32_t ProbeMips = 1;
			std::uint32_t Reserved0 = 0;
			std::uint32_t Reserved1 = 0;
		};

		static_assert(sizeof(ResolveConstants) == PlanarReflectionResolveBindings::PushConstantBytes);

		Rhi::TextureViewDesc View(Rhi::Format format, Rhi::TextureAspect aspect = Rhi::TextureAspect::Automatic)
		{
			Rhi::TextureViewDesc view;
			view.PixelFormat = format;
			view.Aspect = aspect;
			return view;
		}

		Rhi::TextureViewDesc CubeArrayView(std::uint32_t layers, std::uint32_t mips)
		{
			Rhi::TextureViewDesc view;
			view.Dimension = Rhi::TextureViewDimension::TextureCubeArray;
			view.PixelFormat = AtlasFormat;
			view.MipLevelCount = mips;
			view.ArrayLayerCount = layers;
			return view;
		}

		Rhi::DescriptorWrite Write(std::uint32_t binding, Rhi::TextureView& view)
		{
			Rhi::DescriptorWrite write{};
			write.Binding = binding;
			write.TextureResource = &view;
			return write;
		}

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
			throw std::invalid_argument(desc.DebugName + " needs a power-of-two resolution in 64..2048 and 1..MaxPlanarReflections layers");
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

			// The probe stand-ins (ProbeCount = 0: never read, but bound).
			auto arrayDesc = cubeDesc;
			const std::string arrayName = desc.DebugName + " probe stand-in";
			arrayDesc.DebugName = arrayName;
			result.ProbeStandIn = graph.CreateTexture(arrayDesc);

			for (std::uint32_t face = 0; face < Environment::CubeFaceCount; ++face)
			{
				AddTextureUpload(graph, arrayName + " upload", std::as_bytes(std::span(zero)), result.ProbeStandIn, { 0, { 0, face }, {}, { 1, 1, 1 } });
			}

			const std::array<std::byte, 32> record{};
			result.ProbeRecordStandIn = graph.CreateUpload(record, desc.DebugName + " probe record stand-in", Rhi::BufferUsage::Storage, 16);
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
			const auto probes = result.ProbeStandIn;
			const auto records = result.ProbeRecordStandIn;
			graph.AddPass(
				desc.DebugName + " clear", Rhi::QueueType::Compute,
				[&](RenderGraphBuilder& b)
				{
					b.Read(colorStandIn, S::ShaderRead);
					b.Read(depthStandIn, S::ShaderRead);
					b.Read(environment, S::ShaderRead);
					b.Read(probes, S::ShaderRead);
					b.Read(records, S::ShaderRead);
					b.Write(texture, S::ShaderWrite);
				},
				[program = desc.Resolve, label = desc.DebugName + " clear", texture, colorStandIn, depthStandIn, environment, probes, records,
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
					auto& probeResource = c.CreateView(probes, CubeArrayView(Environment::CubeFaceCount, 1));

					for (std::uint32_t layer = 0; layer < count; ++layer)
					{
						using B = PlanarReflectionResolveBindings;
						std::array<Rhi::DescriptorWrite, B::Count> writes{};
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
						writes[B::Normal] = Write(B::Normal, colorResource);
						writes[B::Reflectance] = Write(B::Reflectance, colorResource);
						writes[B::Specular] = Write(B::Specular, colorResource);
						writes[B::ObjectId] = Write(B::ObjectId, depthResource);
						writes[B::ProbeCubes] = Write(B::ProbeCubes, probeResource);
						writes[B::ProbeRecords] = BufferWrite(c, B::ProbeRecords, records);
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
		const PlanarCaptureTargets& targets, const ReflectionProbeCaptureSky& sky, const std::optional<PlanarCaptureProbes>& probes) const
	{
		if (capture.Slot >= layers || capture.Width == 0 || capture.Height == 0 || capture.Width > resolution || capture.Height > resolution)
		{
			throw std::invalid_argument(desc.DebugName + ": capture slot or size out of range");
		}

		const std::uint32_t scale = capture.RenderWidth / capture.Width;

		if (scale < 1 || scale > PlanarReflectionResolveBindings::MaxSupersample || capture.RenderWidth != capture.Width * scale ||
			capture.RenderHeight != capture.Height * scale)
		{
			throw std::invalid_argument(desc.DebugName + ": the rendered size must be the stored size times 1 .. 4");
		}

		const auto matches = [&](GraphTexture texture, Rhi::Format format)
		{
			const auto& d = graph.GetDesc(texture);
			return d.Extent.Width == capture.RenderWidth && d.Extent.Height == capture.RenderHeight && d.PixelFormat == format;
		};

		if (!matches(targets.Color, AtlasFormat) || !matches(targets.Depth, Rhi::Format::D32Float) ||
			!matches(targets.Normal, Rhi::Format::RGBA16Float) || !matches(targets.Reflectance, Rhi::Format::RGBA16Float) ||
			!matches(targets.Specular, Rhi::Format::RGBA16Float) || !matches(targets.ObjectId, Rhi::Format::R32Float))
		{
			throw std::invalid_argument(desc.DebugName + ": capture targets must be RenderWidth x RenderHeight Forward+ targets");
		}

		const auto layer = capture.Slot;
		const auto texture = target.Texture;
		const auto environment = sky.Environment.value_or(target.EnvironmentStandIn);
		const std::uint32_t environmentMips = graph.GetDesc(environment).MipLevels;
		const bool useProbes = probes.has_value() && probes->Count > 0 && probes->Cubes.has_value();
		const auto probeCubes = useProbes ? *probes->Cubes : target.ProbeStandIn;
		const auto probeRecords = useProbes ? probes->Records : target.ProbeRecordStandIn;
		const std::uint32_t probeLayers = graph.GetDesc(probeCubes).ArrayLayers;
		const std::uint32_t probeMips = useProbes ? std::max(probes->MipCount, 1u) : 1u;
		ResolveConstants constants;
		constants.Width = capture.Width;
		constants.Height = capture.Height;
		constants.Environment = sky.Environment ? 1u : 0u;
		constants.NearClip = capture.NearClip;
		constants.EnvironmentScale = sky.Scale;
		constants.EnvironmentRotation = sky.Rotation;
		constants.Scale = scale;
		constants.ProbeCount = useProbes ? probes->Count : 0u;
		constants.ProbeMips = probeMips;

		for (int c = 0; c < 4; ++c)
		{
			constants.FrustumScale[c] = capture.FrustumScale[c];
		}

		for (int c = 0; c < 3; ++c)
		{
			constants.Right[c] = capture.Right[c];
			constants.Up[c] = capture.Up[c];
			constants.Forward[c] = capture.Forward[c];
			constants.Position[c] = capture.Position[c];
		}

		const std::string label = desc.DebugName + " resolve";
		return graph.AddPass(
			label, Rhi::QueueType::Compute,
			[&](RenderGraphBuilder& b)
			{
				b.Read(targets.Color, S::ShaderRead);
				b.Read(targets.Depth, S::ShaderRead);
				b.Read(targets.Normal, S::ShaderRead);
				b.Read(targets.Reflectance, S::ShaderRead);
				b.Read(targets.Specular, S::ShaderRead);
				b.Read(targets.ObjectId, S::ShaderRead);
				b.Read(environment, S::ShaderRead);
				b.Read(probeCubes, S::ShaderRead);
				b.Read(probeRecords, S::ShaderRead);
				b.Write(texture, S::ShaderWrite, { 0, 1, layer, 1 });
			},
			[program = desc.Resolve, sampler = desc.Sampler, label, targets, environment, environmentMips, probeCubes, probeRecords, probeLayers,
				probeMips, texture, layer, constants](RenderCommandContext& c)
			{
				using B = PlanarReflectionResolveBindings;
				std::array<Rhi::DescriptorWrite, B::Count> writes{};
				writes[B::Color] = Write(B::Color, c.CreateView(targets.Color, View(AtlasFormat)));
				writes[B::Depth] = Write(B::Depth, c.CreateView(targets.Depth, View(Rhi::Format::D32Float, Rhi::TextureAspect::Depth)));
				writes[B::Destination] = Write(B::Destination, c.CreateView(texture, LayerView(layer)));
				Rhi::TextureViewDesc cubeView;
				cubeView.Dimension = Rhi::TextureViewDimension::TextureCube;
				cubeView.PixelFormat = AtlasFormat;
				cubeView.MipLevelCount = environmentMips;
				cubeView.ArrayLayerCount = Environment::CubeFaceCount;
				writes[B::Environment] = Write(B::Environment, c.CreateView(environment, cubeView));
				writes[B::Sampler].Binding = B::Sampler;
				writes[B::Sampler].SamplerResource = sampler;
				writes[B::Normal] = Write(B::Normal, c.CreateView(targets.Normal, View(Rhi::Format::RGBA16Float)));
				writes[B::Reflectance] = Write(B::Reflectance, c.CreateView(targets.Reflectance, View(Rhi::Format::RGBA16Float)));
				writes[B::Specular] = Write(B::Specular, c.CreateView(targets.Specular, View(Rhi::Format::RGBA16Float)));
				writes[B::ObjectId] = Write(B::ObjectId, c.CreateView(targets.ObjectId, View(Rhi::Format::R32Float)));
				writes[B::ProbeCubes] = Write(B::ProbeCubes, c.CreateView(probeCubes, CubeArrayView(probeLayers, probeMips)));
				writes[B::ProbeRecords] = BufferWrite(c, B::ProbeRecords, probeRecords);
				auto& list = c.Commands();
				list.BindComputePipeline(*program.Pipeline);
				list.BindDescriptorTable(program.Space, Table(c, program, label, writes));
				list.PushConstants(Rhi::ShaderStageMask::Compute, 0, std::as_bytes(std::span(&constants, 1)));
				constexpr auto group = B::ThreadGroupSize;
				list.Dispatch(Groups(constants.Width, group), Groups(constants.Height, group), 1);
			});
	}

} // namespace Swim::Render
