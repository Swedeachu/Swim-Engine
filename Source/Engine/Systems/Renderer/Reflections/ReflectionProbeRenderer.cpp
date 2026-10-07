#include "Engine/Systems/Renderer/Reflections/ReflectionProbeRenderer.h"
#include "Engine/Systems/Renderer/Environment/CubeImage.h"
#include "Engine/Systems/Renderer/Environment/EnvironmentMath.h"
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

		Rhi::TextureViewDesc FaceView(std::uint32_t mip, std::uint32_t layer)
		{
			Rhi::TextureViewDesc view;
			view.Dimension = Rhi::TextureViewDimension::Texture2D;
			view.PixelFormat = AtlasFormat;
			view.BaseMipLevel = mip;
			view.BaseArrayLayer = layer;
			return view;
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

		template <typename T> void Push(Rhi::CommandList& list, const T& constants)
		{
			list.PushConstants(Rhi::ShaderStageMask::Compute, 0, std::as_bytes(std::span(&constants, 1)));
		}

		struct ResolveConstants
		{
			std::uint32_t Size = 0;
			std::uint32_t Mode = 0;
			float Near = 0.0f;
			std::uint32_t Face = 0;
			std::uint32_t Environment = 0;
			float EnvironmentScale = 1.0f;
			float EnvironmentRotation = 0.0f;
			std::uint32_t Reserved = 0;
			float Position[4] = {}; // The probe (capture) position.
			std::uint32_t ProbeCount = 0;
			std::uint32_t ProbeMips = 1;
			std::uint32_t Reserved0 = 0;
			std::uint32_t Reserved1 = 0;
		};

		Rhi::TextureViewDesc CubeArrayView(std::uint32_t layers, std::uint32_t mips)
		{
			Rhi::TextureViewDesc view;
			view.Dimension = Rhi::TextureViewDimension::TextureCubeArray;
			view.PixelFormat = AtlasFormat;
			view.MipLevelCount = mips;
			view.ArrayLayerCount = layers;
			return view;
		}

		Rhi::TextureViewDesc View(Rhi::Format format, Rhi::TextureAspect aspect = Rhi::TextureAspect::Automatic)
		{
			Rhi::TextureViewDesc view;
			view.PixelFormat = format;
			view.Aspect = aspect;
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

		static_assert(sizeof(ResolveConstants) == ReflectionProbeResolveBindings::PushConstantBytes);

	} // namespace

	ReflectionProbeRenderer::ReflectionProbeRenderer(Rhi::Device& deviceValue, ReflectionProbeRendererDesc descValue)
		: device(deviceValue), desc(std::move(descValue))
	{
		for (const auto* program : { &desc.Resolve, &desc.Downsample, &desc.Prefilter })
		{
			if (!program->Pipeline || !program->Layout)
			{
				throw std::invalid_argument(desc.DebugName + " needs the resolve, downsample and prefilter programs");
			}
		}

		if (!desc.Sampler)
		{
			throw std::invalid_argument(desc.DebugName + " needs a sampler");
		}
	}

	Rhi::TextureDesc ReflectionProbeRenderer::AtlasDesc(std::uint32_t size, std::uint32_t probes)
	{
		Rhi::TextureDesc texture;
		texture.Dimension = Rhi::TextureDimension::TextureCube;
		texture.Extent = { size, size, 1 };
		texture.PixelFormat = AtlasFormat;
		texture.Usage = Rhi::TextureUsage::Sampled | Rhi::TextureUsage::Storage;
		texture.MipLevels = Environment::EnvironmentSourceMipCount(size);
		texture.ArrayLayers = Environment::CubeFaceCount * probes;
		return texture;
	}

	bool ReflectionProbeRenderer::Ensure(std::uint32_t size, std::uint32_t probes)
	{
		if (!Environment::IsPowerOfTwo(size) || size < 16 || size > 512 || probes == 0 || probes > MaxReflectionProbes)
		{
			throw std::invalid_argument(desc.DebugName + " needs a power-of-two resolution in 16..512 and 1..16 probes");
		}

		if (source && size == resolution && probes == maxProbes)
		{
			return false;
		}

		auto sourceDesc = AtlasDesc(size, probes);
		const std::string sourceName = desc.DebugName + " source";
		sourceDesc.DebugName = sourceName;
		auto prefilteredDesc = AtlasDesc(size, probes);
		const std::string prefilteredName = desc.DebugName + " prefiltered";
		prefilteredDesc.DebugName = prefilteredName;
		source = device.CreateTexture(sourceDesc);
		prefiltered = device.CreateTexture(prefilteredDesc);

		if (!source || !prefiltered)
		{
			throw std::runtime_error(desc.DebugName + ": cannot create the probe atlases");
		}

		resolution = size;
		maxProbes = probes;
		mipCount = sourceDesc.MipLevels;
		initialized = false;
		return true;
	}

	ReflectionProbeRenderer::Atlases ReflectionProbeRenderer::Import(RenderGraph& graph)
	{
		if (!source)
		{
			throw std::logic_error(desc.DebugName + ": Ensure before Import");
		}

		Atlases atlases;
		{
			Rhi::TextureDesc cubeDesc;
			cubeDesc.Dimension = Rhi::TextureDimension::TextureCube;
			cubeDesc.Extent = { 1, 1, 1 };
			cubeDesc.ArrayLayers = Environment::CubeFaceCount;
			cubeDesc.PixelFormat = AtlasFormat;
			cubeDesc.Usage = Rhi::TextureUsage::Sampled | Rhi::TextureUsage::TransferDestination;
			const std::string cubeName = desc.DebugName + " environment stand-in";
			cubeDesc.DebugName = cubeName;
			atlases.EnvironmentStandIn = graph.CreateTexture(cubeDesc);
			const std::array<std::uint16_t, 4> zero{};

			for (std::uint32_t face = 0; face < Environment::CubeFaceCount; ++face)
			{
				AddTextureUpload(graph, cubeName + " upload", std::as_bytes(std::span(zero)), atlases.EnvironmentStandIn,
					{ 0, { 0, face }, {}, { 1, 1, 1 } });
			}
		}
		const std::array<std::byte, 32> zeroRecord{};
		atlases.ProbeRecordStandIn =
			graph.CreateUpload(zeroRecord, desc.DebugName + " record stand-in", Rhi::BufferUsage::Storage, 16);
		const auto state = initialized ? S::ShaderRead : S::Undefined;
		atlases.Source = graph.ImportTexture(*source, state);
		atlases.Prefiltered = graph.ImportTexture(*prefiltered, state);

		if (!initialized)
		{
			// Every face of every mip of both atlases starts defined (sky distance, black), so
			// the cube-array view shading binds never samples an undefined layer.
			const auto layers = Environment::CubeFaceCount * maxProbes;
			// 1x1 stand-ins for the color and depth inputs clear mode never reads.
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
			const auto colorStandIn = standIn(AtlasFormat, std::as_bytes(std::span(zeroColor)), "Reflection probe clear color");
			const auto depthStandIn = standIn(Rhi::Format::R32Float, std::as_bytes(std::span(&zeroDepth, 1)), "Reflection probe clear depth");

			for (const auto atlas : { atlases.Source, atlases.Prefiltered })
			{
				graph.AddPass(
					desc.DebugName + " clear", Rhi::QueueType::Compute,
					[&](RenderGraphBuilder& b)
					{
						b.Read(colorStandIn, S::ShaderRead);
						b.Read(depthStandIn, S::ShaderRead);
						b.Read(atlases.EnvironmentStandIn, S::ShaderRead);
						b.Read(atlases.ProbeRecordStandIn, S::ShaderRead);
						b.Write(atlas, S::ShaderWrite);
					},
					[program = desc.Resolve, label = desc.DebugName + " clear", atlas, colorStandIn, depthStandIn,
						environment = atlases.EnvironmentStandIn, records = atlases.ProbeRecordStandIn, sampler = desc.Sampler, layers,
						mips = mipCount, size = resolution](RenderCommandContext& c)
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
						auto& probeResource = c.CreateView(environment, CubeArrayView(Environment::CubeFaceCount, 1));

						for (std::uint32_t mip = 0; mip < mips; ++mip)
						{
							const std::uint32_t mipSize = std::max(size >> mip, 1u);

							for (std::uint32_t layer = 0; layer < layers; ++layer)
							{
								using B = ReflectionProbeResolveBindings;
								std::array<Rhi::DescriptorWrite, B::Count> writes{};
								writes[B::Normal] = Write(B::Normal, colorResource);
								writes[B::Reflectance] = Write(B::Reflectance, colorResource);
								writes[B::Specular] = Write(B::Specular, colorResource);
								writes[B::ObjectId] = Write(B::ObjectId, depthResource);
								writes[B::ProbeCubes] = Write(B::ProbeCubes, probeResource);
								writes[B::ProbeRecords] = BufferWrite(c, B::ProbeRecords, records);
								writes[0].Binding = ReflectionProbeResolveBindings::Color;
								writes[0].TextureResource = &colorResource;
								writes[1].Binding = ReflectionProbeResolveBindings::Depth;
								writes[1].TextureResource = &depthResource;
								writes[2].Binding = ReflectionProbeResolveBindings::Destination;
								writes[2].TextureResource = &c.CreateView(atlas, FaceView(mip, layer));
								writes[3].Binding = ReflectionProbeResolveBindings::Environment;
								writes[3].TextureResource = &environmentResource;
								writes[4].Binding = ReflectionProbeResolveBindings::Sampler;
								writes[4].SamplerResource = sampler;
								list.BindDescriptorTable(program.Space, Table(c, program, label, writes));
								ResolveConstants constants;
								constants.Size = mipSize;
								constants.Mode = 1u;
								Push(list, constants);
								constexpr auto group = ReflectionProbeResolveBindings::ThreadGroupSize;
								list.Dispatch(Groups(mipSize, group), Groups(mipSize, group), 1);
							}
						}

					});
			}

			initialized = true;
		}

		return atlases;
	}

	GraphPass ReflectionProbeRenderer::RecordResolve(RenderGraph& graph, const Atlases& atlases, std::uint32_t slot, std::uint32_t face,
		const ReflectionCaptureTargets& targets, const std::array<float, 3>& position, float nearPlane, const CaptureSky& sky,
		const std::optional<ReflectionCaptureProbes>& probes) const
	{
		if (slot >= maxProbes || face >= Environment::CubeFaceCount)
		{
			throw std::invalid_argument(desc.DebugName + ": slot or face out of range");
		}

		const auto matches = [&](GraphTexture texture, Rhi::Format format)
		{
			const auto& d = graph.GetDesc(texture);
			return d.Extent.Width == resolution && d.Extent.Height == resolution && d.PixelFormat == format;
		};

		if (!matches(targets.Color, AtlasFormat) || !matches(targets.Depth, Rhi::Format::D32Float) ||
			!matches(targets.Normal, Rhi::Format::RGBA16Float) || !matches(targets.Reflectance, Rhi::Format::RGBA16Float) ||
			!matches(targets.Specular, Rhi::Format::RGBA16Float) || !matches(targets.ObjectId, Rhi::Format::R32Float))
		{
			throw std::invalid_argument(desc.DebugName + ": a capture must be Resolution^2 Forward+ targets (RGBA16Float color, D32Float depth)");
		}

		const std::uint32_t layer = slot * Environment::CubeFaceCount + face;
		const auto target = atlases.Source;
		const auto environment = sky.Environment.value_or(atlases.EnvironmentStandIn);
		const std::uint32_t environmentMips = graph.GetDesc(environment).MipLevels;
		// The probes reflective texels reflect: the prefiltered atlas as it stands (this frame's
		// filters come after the captures).
		const bool useProbes = probes.has_value() && probes->Count > 0;
		const auto probeRecords = useProbes ? probes->Records : atlases.ProbeRecordStandIn;
		const auto probeCubes = useProbes ? probes->Cubes.value_or(atlases.Prefiltered) : atlases.EnvironmentStandIn;
		const std::uint32_t probeLayers = graph.GetDesc(probeCubes).ArrayLayers;
		const std::uint32_t probeMips = useProbes ? graph.GetDesc(probeCubes).MipLevels : 1u;
		ResolveConstants constants;
		constants.Size = resolution;
		constants.Near = nearPlane;
		constants.Face = face;
		constants.Environment = sky.Environment ? 1u : 0u;
		constants.EnvironmentScale = sky.Scale;
		constants.EnvironmentRotation = sky.Rotation;
		constants.Position[0] = position[0];
		constants.Position[1] = position[1];
		constants.Position[2] = position[2];
		constants.ProbeCount = useProbes ? probes->Count : 0u;
		constants.ProbeMips = probeMips;
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
				b.Read(probeRecords, S::ShaderRead);

				if (probeCubes != environment)
				{
					b.Read(probeCubes, S::ShaderRead);
				}

				b.Write(target, S::ShaderWrite, { 0, 1, layer, 1 });
			},
			[program = desc.Resolve, sampler = desc.Sampler, label, targets, environment, environmentMips, probeCubes, probeRecords, probeLayers,
				probeMips, target, layer, size = resolution, constants](RenderCommandContext& c)
			{
				using B = ReflectionProbeResolveBindings;
				std::array<Rhi::DescriptorWrite, B::Count> writes{};
				writes[B::Color] = Write(B::Color, c.CreateView(targets.Color, View(AtlasFormat)));
				writes[B::Depth] = Write(B::Depth, c.CreateView(targets.Depth, View(Rhi::Format::D32Float, Rhi::TextureAspect::Depth)));
				writes[B::Destination] = Write(B::Destination, c.CreateView(target, FaceView(0, layer)));
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
				Push(list, constants);
				constexpr auto group = B::ThreadGroupSize;
				list.Dispatch(Groups(size, group), Groups(size, group), 1);
			});
	}

	void ReflectionProbeRenderer::RecordFilter(
		RenderGraph& graph, const Atlases& atlases, std::uint32_t slot, std::uint32_t sampleCount) const
	{
		if (slot >= maxProbes)
		{
			throw std::invalid_argument(desc.DebugName + ": slot out of range");
		}

		const std::uint32_t base = slot * Environment::CubeFaceCount;
		const auto source = atlases.Source;
		const auto destination = atlases.Prefiltered;
		// Source mips 1..: 2x2 box averages (radiance and distance alike).
		for (std::uint32_t mip = 1; mip < mipCount; ++mip)
		{
			const std::uint32_t size = std::max(resolution >> mip, 1u);
			const std::string label = desc.DebugName + " mip " + std::to_string(mip);
			graph.AddPass(
				label, Rhi::QueueType::Compute,
				[&](RenderGraphBuilder& b)
				{
					b.Read(source, S::ShaderRead, { mip - 1, 1, base, Environment::CubeFaceCount });
					b.Write(source, S::ShaderWrite, { mip, 1, base, Environment::CubeFaceCount });
				},
				[program = desc.Downsample, label, source, base, mip, size](RenderCommandContext& c)
				{
					auto& list = c.Commands();
					list.BindComputePipeline(*program.Pipeline);

					for (std::uint32_t face = 0; face < Environment::CubeFaceCount; ++face)
					{
						std::array<Rhi::DescriptorWrite, 2> writes{};
						writes[0].Binding = EnvironmentDownsampleBindings::Source;
						writes[0].TextureResource = &c.CreateView(source, FaceView(mip - 1, base + face));
						writes[1].Binding = EnvironmentDownsampleBindings::Destination;
						writes[1].TextureResource = &c.CreateView(source, FaceView(mip, base + face));
						list.BindDescriptorTable(program.Space, Table(c, program, label, writes));
						Push(list, std::array<std::uint32_t, 4>{ size, 0, 0, 0 });
						constexpr auto group = EnvironmentDownsampleBindings::ThreadGroupSize;
						list.Dispatch(Groups(size, group), Groups(size, group), 1);
					}

				});
		}

		// Prefiltered mips: GGX per roughness, alpha = the captured distance.
		Rhi::TextureViewDesc cubeView;
		cubeView.Dimension = Rhi::TextureViewDimension::TextureCube;
		cubeView.PixelFormat = AtlasFormat;
		cubeView.MipLevelCount = mipCount;
		cubeView.BaseArrayLayer = base;
		cubeView.ArrayLayerCount = Environment::CubeFaceCount;
		const std::string label = desc.DebugName + " prefilter";
		graph.AddPass(
			label, Rhi::QueueType::Compute,
			[&](RenderGraphBuilder& b)
			{
				b.Read(source, S::ShaderRead, { 0, mipCount, base, Environment::CubeFaceCount });
				b.Write(destination, S::ShaderWrite, { 0, mipCount, base, Environment::CubeFaceCount });
			},
			[program = desc.Prefilter, sampler = desc.Sampler, label, source, destination, cubeView, base, mips = mipCount,
				size = resolution, sampleCount](RenderCommandContext& c)
			{
				auto& list = c.Commands();
				list.BindComputePipeline(*program.Pipeline);
				auto& sourceView = c.CreateView(source, cubeView);

				for (std::uint32_t mip = 0; mip < mips; ++mip)
				{
					const std::uint32_t mipSize = std::max(size >> mip, 1u);

					for (std::uint32_t face = 0; face < Environment::CubeFaceCount; ++face)
					{
						std::array<Rhi::DescriptorWrite, 3> writes{};
						writes[0].Binding = EnvironmentPrefilterBindings::Source;
						writes[0].TextureResource = &sourceView;
						writes[1].Binding = EnvironmentPrefilterBindings::Sampler;
						writes[1].SamplerResource = sampler;
						writes[2].Binding = EnvironmentPrefilterBindings::Destination;
						writes[2].TextureResource = &c.CreateView(destination, FaceView(mip, base + face));
						list.BindDescriptorTable(program.Space, Table(c, program, label, writes));
						const EnvironmentPrefilterConstants constants{ face, mipSize, Environment::PrefilterMipRoughness(mip, mips),
							sampleCount, size, mips, {} };
						Push(list, constants);
						constexpr auto group = EnvironmentPrefilterBindings::ThreadGroupSize;
						list.Dispatch(Groups(mipSize, group), Groups(mipSize, group), 1);
					}
				}

			});
	}

} // namespace Swim::Render
