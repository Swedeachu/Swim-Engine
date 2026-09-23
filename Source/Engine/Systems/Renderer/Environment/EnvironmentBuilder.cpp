#include "Engine/Systems/Renderer/Environment/EnvironmentBuilder.h"
#include "Engine/Systems/Renderer/Environment/CubeImage.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderCommandContext.h"

#include <algorithm>
#include <array>
#include <stdexcept>

namespace Swim::Render
{
	namespace
	{
		using S = Rhi::ResourceState;
		constexpr auto EnvironmentFormat = Rhi::Format::RGBA16Float;

		bool HasUsage(Rhi::TextureUsage usage, Rhi::TextureUsage required)
		{
			return (static_cast<std::uint32_t>(usage) & static_cast<std::uint32_t>(required)) == static_cast<std::uint32_t>(required);
		}

		bool IsCube(const Rhi::TextureDesc& desc)
		{
			return desc.Dimension == Rhi::TextureDimension::TextureCube && desc.ArrayLayers == Environment::CubeFaceCount &&
				desc.Extent.Width == desc.Extent.Height && desc.Extent.Depth == 1 && desc.Samples == Rhi::SampleCount::X1 &&
				Environment::IsPowerOfTwo(desc.Extent.Width);
		}

		std::uint32_t Groups(std::uint32_t size, std::uint32_t group)
		{
			return (size + group - 1) / group;
		}

		Rhi::DescriptorTable& CreateTable(RenderCommandContext& c, const EnvironmentProgram& program, const std::string& label,
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

		Rhi::TextureViewDesc FaceView(std::uint32_t mip, std::uint32_t face)
		{
			Rhi::TextureViewDesc view;
			view.Dimension = Rhi::TextureViewDimension::Texture2D;
			view.PixelFormat = EnvironmentFormat;
			view.BaseMipLevel = mip;
			view.BaseArrayLayer = face;
			return view;
		}

		template <typename T> void Push(Rhi::CommandList& list, const T& constants)
		{
			list.PushConstants(Rhi::ShaderStageMask::Compute, 0, std::as_bytes(std::span(&constants, 1)));
		}
	} // namespace

	EnvironmentBuilder::EnvironmentBuilder(EnvironmentBuilderDesc descInput) : desc(std::move(descInput))
	{
		for (const auto* program : { &desc.Sky, &desc.Downsample, &desc.Prefilter, &desc.Irradiance, &desc.BrdfLut })
		{
			if (!program->Pipeline || !program->Layout)
			{
				throw std::invalid_argument(desc.DebugName + " needs all five environment pipelines and layouts");
			}
		}
		if (!desc.Sampler)
		{
			throw std::invalid_argument(desc.DebugName + " needs the prefilter sampler");
		}
	}

	void EnvironmentBuilder::Validate(const EnvironmentMapDesc& map)
	{
		using Environment::IsPowerOfTwo;
		if (!IsPowerOfTwo(map.SourceSize) || map.SourceSize < 16 || !IsPowerOfTwo(map.PrefilteredSize) || map.PrefilteredSize < 4)
		{
			throw std::invalid_argument("Environment cube sizes must be powers of two (source >= 16, prefiltered >= 4)");
		}
		if (map.PrefilteredMipCount < 2 || map.PrefilteredMipCount > Environment::FullCubeMipCount(map.PrefilteredSize))
		{
			throw std::invalid_argument("Environment prefiltered cube needs 2..full-chain mips");
		}
		if (map.PrefilterSampleCount == 0 || map.PrefilterSampleCount > 4096)
		{
			throw std::invalid_argument("Environment prefilter sample count must be 1..4096");
		}
		if (!IsPowerOfTwo(map.IrradianceFaceSize) || map.IrradianceFaceSize < 4 || map.IrradianceFaceSize > map.SourceSize)
		{
			throw std::invalid_argument("Environment irradiance face size must be a power of two in 4..source size");
		}
	}

	Rhi::TextureDesc EnvironmentBuilder::SourceCubeDesc(std::uint32_t size)
	{
		Rhi::TextureDesc texture;
		texture.Dimension = Rhi::TextureDimension::TextureCube;
		texture.Extent = { size, size, 1 };
		texture.PixelFormat = EnvironmentFormat;
		texture.Usage = Rhi::TextureUsage::Sampled | Rhi::TextureUsage::Storage | Rhi::TextureUsage::TransferSource;
		texture.MipLevels = Environment::EnvironmentSourceMipCount(size);
		texture.ArrayLayers = Environment::CubeFaceCount;
		return texture;
	}

	Rhi::TextureDesc EnvironmentBuilder::PrefilteredCubeDesc(const EnvironmentMapDesc& map)
	{
		auto texture = SourceCubeDesc(map.PrefilteredSize);
		texture.MipLevels = map.PrefilteredMipCount;
		return texture;
	}

	Rhi::TextureDesc EnvironmentBuilder::BrdfLutDesc(std::uint32_t size)
	{
		Rhi::TextureDesc texture;
		texture.Extent = { size, size, 1 };
		texture.PixelFormat = EnvironmentFormat;
		texture.Usage = Rhi::TextureUsage::Sampled | Rhi::TextureUsage::Storage | Rhi::TextureUsage::TransferSource;
		return texture;
	}

	Rhi::BufferDesc EnvironmentBuilder::IrradianceBufferDesc()
	{
		Rhi::BufferDesc buffer;
		buffer.Size = EnvironmentIrradianceBindings::OutputBytes;
		buffer.Usage = Rhi::BufferUsage::Storage | Rhi::BufferUsage::TransferSource;
		buffer.Memory = Rhi::MemoryPreference::DeviceLocal;
		return buffer;
	}

	GraphTexture EnvironmentBuilder::RecordSky(
		RenderGraph& graph, const Environment::ProceduralSky& sky, const EnvironmentMapDesc& map) const
	{
		std::vector<GraphPass> passes;
		return RecordSky(graph, sky, map, passes);
	}

	GraphTexture EnvironmentBuilder::RecordSky(
		RenderGraph& graph, const Environment::ProceduralSky& sky, const EnvironmentMapDesc& map, std::vector<GraphPass>& passes) const
	{
		Validate(map);
		auto sourceDesc = SourceCubeDesc(map.SourceSize);
		const std::string sourceName = desc.DebugName + " source";
		sourceDesc.DebugName = sourceName;
		const auto source = graph.CreateTexture(sourceDesc);
		const std::uint32_t size = map.SourceSize;
		std::array<Environment::ProceduralSkyConstants, Environment::CubeFaceCount> constants{};
		for (std::uint32_t face = 0; face < Environment::CubeFaceCount; ++face)
		{
			constants[face] = Environment::MakeProceduralSkyConstants(sky, face, size);
		}
		passes.push_back(graph.AddPass(
			desc.DebugName + " sky", Rhi::QueueType::Compute,
			[&](RenderGraphBuilder& b)
			{
				b.Write(source, S::ShaderWrite, { 0, 1, 0, Environment::CubeFaceCount });
			},
			[program = desc.Sky, label = desc.DebugName + " sky", source, size, constants](RenderCommandContext& c)
			{
				auto& list = c.Commands();
				list.BindComputePipeline(*program.Pipeline);
				for (std::uint32_t face = 0; face < Environment::CubeFaceCount; ++face)
				{
					Rhi::DescriptorWrite write{};
					write.Binding = EnvironmentSkyBindings::Destination;
					write.TextureResource = &c.CreateView(source, FaceView(0, face));
					list.BindDescriptorTable(program.Space, CreateTable(c, program, label, { &write, 1 }));
					Push(list, constants[face]);
					constexpr auto group = EnvironmentSkyBindings::ThreadGroupSize;
					list.Dispatch(Groups(size, group), Groups(size, group), 1);
				}
			}));
		const auto mips = RecordMips(graph, source);
		passes.insert(passes.end(), mips.begin(), mips.end());
		return source;
	}

	std::vector<GraphPass> EnvironmentBuilder::RecordMips(RenderGraph& graph, GraphTexture cube) const
	{
		const auto cubeDesc = graph.GetDesc(cube); // A copy: later graph calls may reallocate.
		if (!IsCube(cubeDesc) || cubeDesc.PixelFormat != EnvironmentFormat ||
			!HasUsage(cubeDesc.Usage, Rhi::TextureUsage::Sampled | Rhi::TextureUsage::Storage) ||
			cubeDesc.MipLevels > Environment::FullCubeMipCount(cubeDesc.Extent.Width))
		{
			throw std::invalid_argument(
				desc.DebugName + " mips need a square power-of-two RGBA16Float cube with Sampled and Storage usage");
		}
		std::vector<GraphPass> passes;
		for (std::uint32_t mip = 1; mip < cubeDesc.MipLevels; ++mip)
		{
			const std::uint32_t size = std::max(cubeDesc.Extent.Width >> mip, 1u);
			const std::array<std::uint32_t, 4> constants{ size, 0, 0, 0 };
			const std::string label = desc.DebugName + " mip " + std::to_string(mip);
			passes.push_back(graph.AddPass(
				label, Rhi::QueueType::Compute,
				[&](RenderGraphBuilder& b)
				{
					b.Read(cube, S::ShaderRead, { mip - 1, 1, 0, Environment::CubeFaceCount });
					b.Write(cube, S::ShaderWrite, { mip, 1, 0, Environment::CubeFaceCount });
				},
				[program = desc.Downsample, label, cube, mip, size, constants](RenderCommandContext& c)
				{
					auto& list = c.Commands();
					list.BindComputePipeline(*program.Pipeline);
					for (std::uint32_t face = 0; face < Environment::CubeFaceCount; ++face)
					{
						std::array<Rhi::DescriptorWrite, 2> writes{};
						writes[0].Binding = EnvironmentDownsampleBindings::Source;
						writes[0].TextureResource = &c.CreateView(cube, FaceView(mip - 1, face));
						writes[1].Binding = EnvironmentDownsampleBindings::Destination;
						writes[1].TextureResource = &c.CreateView(cube, FaceView(mip, face));
						list.BindDescriptorTable(program.Space, CreateTable(c, program, label, writes));
						Push(list, constants);
						constexpr auto group = EnvironmentDownsampleBindings::ThreadGroupSize;
						list.Dispatch(Groups(size, group), Groups(size, group), 1);
					}
				}));
		}
		return passes;
	}

	EnvironmentGraphResources EnvironmentBuilder::RecordFromSource(
		RenderGraph& graph, GraphTexture source, const EnvironmentMapDesc& map, const EnvironmentTargets& targets) const
	{
		Validate(map);
		const auto sourceDesc = graph.GetDesc(source);
		if (!IsCube(sourceDesc) || sourceDesc.PixelFormat != EnvironmentFormat || !HasUsage(sourceDesc.Usage, Rhi::TextureUsage::Sampled) ||
			sourceDesc.Extent.Width != map.SourceSize || sourceDesc.MipLevels != Environment::EnvironmentSourceMipCount(map.SourceSize))
		{
			throw std::invalid_argument(desc.DebugName + " source must be a sampled RGBA16Float cube of the map's size with its mip chain");
		}

		EnvironmentGraphResources resources;
		resources.Source = source;
		resources.SourceSize = map.SourceSize;
		resources.SourceMipCount = sourceDesc.MipLevels;
		resources.PrefilteredSize = map.PrefilteredSize;
		resources.PrefilteredMipCount = map.PrefilteredMipCount;
		auto prefilteredDesc = PrefilteredCubeDesc(map);
		if (targets.Prefiltered)
		{
			const auto target = graph.GetDesc(*targets.Prefiltered);
			if (!IsCube(target) || target.PixelFormat != EnvironmentFormat || target.Extent.Width != map.PrefilteredSize ||
				target.MipLevels != map.PrefilteredMipCount || !HasUsage(target.Usage, Rhi::TextureUsage::Storage))
			{
				throw std::invalid_argument(desc.DebugName + " prefiltered target does not match the map desc");
			}
			resources.Prefiltered = *targets.Prefiltered;
		}
		else
		{
			const std::string name = desc.DebugName + " prefiltered";
			prefilteredDesc.DebugName = name;
			resources.Prefiltered = graph.CreateTexture(prefilteredDesc);
		}
		if (targets.Irradiance)
		{
			const auto target = graph.GetDesc(*targets.Irradiance);
			if (target.Size < EnvironmentIrradianceBindings::OutputBytes ||
				(static_cast<std::uint32_t>(target.Usage) & static_cast<std::uint32_t>(Rhi::BufferUsage::Storage)) == 0)
			{
				throw std::invalid_argument(desc.DebugName + " irradiance target must be a storage buffer of at least 144 bytes");
			}
			resources.Irradiance = *targets.Irradiance;
		}
		else
		{
			auto bufferDesc = IrradianceBufferDesc();
			const std::string name = desc.DebugName + " irradiance";
			bufferDesc.DebugName = name;
			resources.Irradiance = graph.CreateBuffer(bufferDesc);
		}

		// Prefilter: one pass per destination mip, one dispatch per face.
		Rhi::TextureViewDesc cubeView;
		cubeView.Dimension = Rhi::TextureViewDimension::TextureCube;
		cubeView.PixelFormat = EnvironmentFormat;
		cubeView.MipLevelCount = sourceDesc.MipLevels;
		cubeView.ArrayLayerCount = Environment::CubeFaceCount;
		for (std::uint32_t mip = 0; mip < map.PrefilteredMipCount; ++mip)
		{
			const std::uint32_t size = std::max(map.PrefilteredSize >> mip, 1u);
			std::array<EnvironmentPrefilterConstants, Environment::CubeFaceCount> constants{};
			for (std::uint32_t face = 0; face < Environment::CubeFaceCount; ++face)
			{
				constants[face] = { face, size, Environment::PrefilterMipRoughness(mip, map.PrefilteredMipCount), map.PrefilterSampleCount,
					map.SourceSize, sourceDesc.MipLevels, {} };
			}
			const std::string label = desc.DebugName + " prefilter mip " + std::to_string(mip);
			resources.Passes.push_back(graph.AddPass(
				label, Rhi::QueueType::Compute,
				[&](RenderGraphBuilder& b)
				{
					b.Read(source, S::ShaderRead);
					b.Write(resources.Prefiltered, S::ShaderWrite, { mip, 1, 0, Environment::CubeFaceCount });
				},
				[program = desc.Prefilter, sampler = desc.Sampler, label, source, cubeView, destination = resources.Prefiltered, mip, size,
					constants](RenderCommandContext& c)
				{
					auto& list = c.Commands();
					list.BindComputePipeline(*program.Pipeline);
					auto& sourceView = c.CreateView(source, cubeView);
					for (std::uint32_t face = 0; face < Environment::CubeFaceCount; ++face)
					{
						std::array<Rhi::DescriptorWrite, 3> writes{};
						writes[0].Binding = EnvironmentPrefilterBindings::Source;
						writes[0].TextureResource = &sourceView;
						writes[1].Binding = EnvironmentPrefilterBindings::Sampler;
						writes[1].SamplerResource = sampler;
						writes[2].Binding = EnvironmentPrefilterBindings::Destination;
						writes[2].TextureResource = &c.CreateView(destination, FaceView(mip, face));
						list.BindDescriptorTable(program.Space, CreateTable(c, program, label, writes));
						Push(list, constants[face]);
						constexpr auto group = EnvironmentPrefilterBindings::ThreadGroupSize;
						list.Dispatch(Groups(size, group), Groups(size, group), 1);
					}
				}));
		}

		// Irradiance: one group projects the six faces of the chosen source mip.
		std::uint32_t irradianceMip = 0;
		while ((map.SourceSize >> irradianceMip) > map.IrradianceFaceSize)
		{
			++irradianceMip;
		}
		if (irradianceMip >= sourceDesc.MipLevels)
		{
			throw std::invalid_argument(desc.DebugName + " irradiance face size is below the source chain");
		}
		Rhi::TextureViewDesc arrayView;
		arrayView.Dimension = Rhi::TextureViewDimension::Texture2DArray;
		arrayView.PixelFormat = EnvironmentFormat;
		arrayView.BaseMipLevel = irradianceMip;
		arrayView.ArrayLayerCount = Environment::CubeFaceCount;
		const std::array<std::uint32_t, 4> irradianceConstants{ map.IrradianceFaceSize, 0, 0, 0 };
		const std::string irradianceLabel = desc.DebugName + " irradiance";
		resources.Passes.push_back(graph.AddPass(
			irradianceLabel, Rhi::QueueType::Compute,
			[&](RenderGraphBuilder& b)
			{
				b.Read(source, S::ShaderRead, { irradianceMip, 1, 0, Environment::CubeFaceCount });
				b.Write(resources.Irradiance, S::ShaderWrite);
			},
			[program = desc.Irradiance, label = irradianceLabel, source, arrayView, output = resources.Irradiance, irradianceConstants](
				RenderCommandContext& c)
			{
				std::array<Rhi::DescriptorWrite, 2> writes{};
				writes[0].Binding = EnvironmentIrradianceBindings::Source;
				writes[0].TextureResource = &c.CreateView(source, arrayView);
				const auto range = c.GetRange(output);
				writes[1].Binding = EnvironmentIrradianceBindings::Output;
				writes[1].BufferResource = range.Buffer;
				writes[1].BufferOffset = range.Offset;
				writes[1].BufferRange = EnvironmentIrradianceBindings::OutputBytes;
				auto& list = c.Commands();
				list.BindComputePipeline(*program.Pipeline);
				list.BindDescriptorTable(program.Space, CreateTable(c, program, label, writes));
				Push(list, irradianceConstants);
				list.Dispatch(1, 1, 1);
			}));
		return resources;
	}

	EnvironmentGraphResources EnvironmentBuilder::Record(
		RenderGraph& graph, const Environment::ProceduralSky& sky, const EnvironmentMapDesc& map, const EnvironmentTargets& targets) const
	{
		std::vector<GraphPass> passes;
		const auto source = RecordSky(graph, sky, map, passes);
		auto resources = RecordFromSource(graph, source, map, targets);
		passes.insert(passes.end(), resources.Passes.begin(), resources.Passes.end());
		resources.Passes = std::move(passes);
		return resources;
	}

	GraphTexture EnvironmentBuilder::RecordBrdfLut(
		RenderGraph& graph, std::uint32_t size, std::uint32_t sampleCount, std::optional<GraphTexture> target) const
	{
		if (size < 4 || size > 1024 || sampleCount == 0 || sampleCount > 65536)
		{
			throw std::invalid_argument(desc.DebugName + " BRDF LUT needs a size in 4..1024 and 1..65536 samples");
		}
		GraphTexture lut;
		if (target)
		{
			const auto targetDesc = graph.GetDesc(*target);
			if (targetDesc.Dimension != Rhi::TextureDimension::Texture2D || targetDesc.Extent.Width != size ||
				targetDesc.Extent.Height != size || targetDesc.PixelFormat != EnvironmentFormat ||
				!HasUsage(targetDesc.Usage, Rhi::TextureUsage::Storage))
			{
				throw std::invalid_argument(desc.DebugName + " BRDF LUT target must be a size x size RGBA16Float storage texture");
			}
			lut = *target;
		}
		else
		{
			auto lutDesc = BrdfLutDesc(size);
			const std::string name = desc.DebugName + " BRDF LUT";
			lutDesc.DebugName = name;
			lut = graph.CreateTexture(lutDesc);
		}
		const std::array<std::uint32_t, 4> constants{ size, sampleCount, 0, 0 };
		graph.AddPass(
			desc.DebugName + " BRDF LUT", Rhi::QueueType::Compute,
			[&](RenderGraphBuilder& b)
			{
				b.Write(lut, S::ShaderWrite, { 0, 1, 0, 1 });
			},
			[program = desc.BrdfLut, label = desc.DebugName + " BRDF LUT", lut, size, constants](RenderCommandContext& c)
			{
				Rhi::TextureViewDesc view;
				view.PixelFormat = EnvironmentFormat;
				Rhi::DescriptorWrite write{};
				write.Binding = EnvironmentBrdfLutBindings::Destination;
				write.TextureResource = &c.CreateView(lut, view);
				auto& list = c.Commands();
				list.BindComputePipeline(*program.Pipeline);
				list.BindDescriptorTable(program.Space, CreateTable(c, program, label, { &write, 1 }));
				Push(list, constants);
				constexpr auto group = EnvironmentBrdfLutBindings::ThreadGroupSize;
				list.Dispatch(Groups(size, group), Groups(size, group), 1);
			});
		return lut;
	}
} // namespace Swim::Render
