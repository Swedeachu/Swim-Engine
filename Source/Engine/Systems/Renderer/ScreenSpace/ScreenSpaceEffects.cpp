#include "Engine/Systems/Renderer/ScreenSpace/ScreenSpaceEffects.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderCommandContext.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraphTransfers.h"

#include <array>
#include <span>
#include <stdexcept>
#include <vector>

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

		Rhi::DescriptorWrite TextureWrite(RenderCommandContext& c, std::uint32_t binding, GraphTexture texture, Rhi::Format format,
			Rhi::TextureAspect aspect = Rhi::TextureAspect::Automatic)
		{
			Rhi::TextureViewDesc view;
			view.PixelFormat = format;
			view.Aspect = aspect;
			Rhi::DescriptorWrite write{};
			write.Binding = binding;
			write.TextureResource = &c.CreateView(texture, view);
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

		template <std::size_t Count>
		void Dispatch(RenderCommandContext& c, const ScreenSpaceProgram& program, const std::string& label,
			const std::array<Rhi::DescriptorWrite, Count>& writes, std::uint32_t width, std::uint32_t height)
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
			const std::uint32_t group = ScreenSpaceThreadGroupSize;
			list.Dispatch((width + group - 1) / group, (height + group - 1) / group, 1);
		}

	} // namespace

	ScreenSpaceEffects::ScreenSpaceEffects(ScreenSpaceEffectsDesc descInput) : desc(std::move(descInput))
	{
		const bool partialReflection = (desc.Reflection.Pipeline == nullptr) != (desc.Reflection.Layout == nullptr);

		for (const auto* program : { &desc.AmbientOcclusion, &desc.Blur, &desc.Composite })
		{
			if (!program->Pipeline || !program->Layout)
			{
				throw std::invalid_argument(desc.DebugName + " needs the AO, blur and composite programs");
			}
		}

		if (partialReflection)
		{
			throw std::invalid_argument(desc.DebugName + " reflection program needs both its pipeline and layout");
		}
	}

	Rhi::TextureDesc ScreenSpaceEffects::OcclusionDesc(std::uint32_t width, std::uint32_t height)
	{
		Rhi::TextureDesc texture;
		texture.Extent = { width, height, 1 };
		texture.PixelFormat = Rhi::Format::R32Float;
		texture.Usage = Rhi::TextureUsage::Sampled | Rhi::TextureUsage::Storage | Rhi::TextureUsage::TransferSource;
		return texture;
	}

	Rhi::TextureDesc ScreenSpaceEffects::OutputDesc(std::uint32_t width, std::uint32_t height)
	{
		Rhi::TextureDesc texture;
		texture.Extent = { width, height, 1 };
		texture.PixelFormat = Rhi::Format::RGBA16Float;
		// ColorAttachment: particles are drawn over the composited frame.
		texture.Usage = Rhi::TextureUsage::Sampled | Rhi::TextureUsage::Storage | Rhi::TextureUsage::TransferSource |
			Rhi::TextureUsage::ColorAttachment;
		return texture;
	}

	ScreenSpaceGraphResources ScreenSpaceEffects::Record(RenderGraph& graph, const ScreenSpaceFrame& frame) const
	{
		const auto& name = desc.DebugName;
		const auto colorDesc = graph.GetDesc(frame.Color); // Copies: creating resources may reallocate the graph's list.
		const auto depthDesc = graph.GetDesc(frame.Depth);
		const auto normalDesc = graph.GetDesc(frame.Normal);
		const auto indirectDesc = graph.GetDesc(frame.Indirect);

		if (!IsPlain2D(colorDesc) || colorDesc.PixelFormat != Rhi::Format::RGBA16Float)
		{
			throw std::invalid_argument(name + " color must be a sampled single-sample 2D RGBA16Float texture");
		}

		const std::uint32_t width = colorDesc.Extent.Width;
		const std::uint32_t height = colorDesc.Extent.Height;
		const auto sameSize = [&](const Rhi::TextureDesc& other)
		{
			return IsPlain2D(other) && other.Extent.Width == width && other.Extent.Height == height;
		};
		const bool depthIsDepth = depthDesc.PixelFormat == Rhi::Format::D32Float;

		if (!sameSize(depthDesc) || (!depthIsDepth && depthDesc.PixelFormat != Rhi::Format::R32Float))
		{
			throw std::invalid_argument(name + " depth must be a color-sized sampled D32Float or R32Float texture");
		}

		if (!sameSize(normalDesc) || normalDesc.PixelFormat != Rhi::Format::RGBA16Float || !sameSize(indirectDesc) ||
			indirectDesc.PixelFormat != Rhi::Format::RGBA16Float)
		{
			throw std::invalid_argument(name + " normal and indirect must be color-sized sampled RGBA16Float textures");
		}

		ScreenSpaceGraphResources resources;
		resources.ParamsRecord = BuildScreenSpaceParams(frame.Settings, frame.View, width, height, frame.NoiseFrame);
		const bool ao = resources.ParamsRecord.AoEnabled != 0u;
		const bool fog = resources.ParamsRecord.FogEnabled != 0u;
		const bool ssr = resources.ParamsRecord.SsrEnabled != 0u;
		const bool probes = frame.Probes && frame.Probes->Count > 0;

		if (probes)
		{
			if (frame.Probes->Count > ScreenSpaceMaxProbes || frame.Probes->MipCount == 0)
			{
				throw std::invalid_argument(name + " probes: 1 .. ScreenSpaceMaxProbes records and at least one mip");
			}

			if (!desc.ProbeSampler)
			{
				throw std::invalid_argument(name + " probes need the probe sampler");
			}

			const auto idDesc = graph.GetDesc(frame.Probes->ObjectId);

			if (!sameSize(idDesc) || idDesc.PixelFormat != Rhi::Format::R32Float)
			{
				throw std::invalid_argument(name + " probes need a color-sized R32Float object id");
			}

			resources.ParamsRecord.ProbeCount = frame.Probes->Count;
			resources.ParamsRecord.ProbeMipCount = frame.Probes->MipCount;
		}

		if (ssr || probes)
		{
			if (ssr && !desc.Reflection.Pipeline)
			{
				throw std::invalid_argument(name + " reflections need the reflection program");
			}

			if (!frame.Reflectance || !frame.Specular)
			{
				throw std::invalid_argument(name + " reflections need the reflectance and specular inputs");
			}

			const auto reflectanceDesc = graph.GetDesc(*frame.Reflectance);
			const auto specularDesc = graph.GetDesc(*frame.Specular);

			if (!sameSize(reflectanceDesc) || reflectanceDesc.PixelFormat != Rhi::Format::RGBA16Float || !sameSize(specularDesc) ||
				specularDesc.PixelFormat != Rhi::Format::RGBA16Float)
			{
				throw std::invalid_argument(name + " reflectance and specular must be color-sized sampled RGBA16Float textures");
			}

			if (ssr && frame.BackDepth)
			{
				const auto backDesc = graph.GetDesc(*frame.BackDepth);

				if (!sameSize(backDesc) || backDesc.PixelFormat != Rhi::Format::D32Float)
				{
					throw std::invalid_argument(name + " back depth must be a color-sized sampled D32Float texture");
				}

				resources.ParamsRecord.SsrBackDepth = 1u;
			}

			if (ssr && frame.History && frame.Velocity)
			{
				const auto historyDesc = graph.GetDesc(*frame.History);
				const auto velocityDesc = graph.GetDesc(*frame.Velocity);

				if (!sameSize(historyDesc) || historyDesc.PixelFormat != Rhi::Format::RGBA16Float || !sameSize(velocityDesc) ||
					velocityDesc.PixelFormat != Rhi::Format::RG16Float)
				{
					throw std::invalid_argument(name + " reflection history must be color-sized RGBA16Float and velocity RG16Float");
				}

				resources.ParamsRecord.SsrHistory = 1u;
			}
		}

		if (!ao && !fog && !ssr && !probes)
		{
			resources.Output = frame.Color;
			resources.Passthrough = true;
			return resources;
		}

		const bool temporal = frame.ReflectionTemporal.has_value() && frame.Velocity.has_value() && (ssr || probes) &&
			desc.ReflectionTemporal.Pipeline != nullptr;

		if (temporal)
		{
			resources.ParamsRecord.ReflectionTemporal = 1u | (frame.ReflectionTemporal->Previous ? 2u : 0u);
			resources.ParamsRecord.ReflectionTemporalBlend = std::clamp(frame.ReflectionTemporal->Blend, 0.01f, 1.0f);
		}

		const auto params =
			graph.CreateUpload(std::as_bytes(std::span(&resources.ParamsRecord, 1)), name + " params", Rhi::BufferUsage::Storage, 16);
		resources.Params = params;
		const auto depth = frame.Depth;
		const auto depthFormat = depthIsDepth ? Rhi::Format::D32Float : Rhi::Format::R32Float;
		const auto depthAspect = depthIsDepth ? Rhi::TextureAspect::Depth : Rhi::TextureAspect::Automatic;

		GraphTexture visibility;

		if (ao)
		{
			const bool halfAo = resources.ParamsRecord.AoHalf != 0u;
			const std::uint32_t aoWidth = halfAo ? (width + 1u) / 2u : width;
			const std::uint32_t aoHeight = halfAo ? (height + 1u) / 2u : height;
			auto rawDesc = OcclusionDesc(aoWidth, aoHeight);
			const std::string rawName = name + " AO raw";
			rawDesc.DebugName = rawName;
			const auto raw = graph.CreateTexture(rawDesc);
			auto blurredDesc = OcclusionDesc(width, height);
			const std::string blurredName = name + " AO";
			blurredDesc.DebugName = blurredName;
			visibility = graph.CreateTexture(blurredDesc);
			resources.AmbientOcclusionRaw = raw;
			resources.AmbientOcclusion = visibility;
			const auto normal = frame.Normal;
			resources.AmbientOcclusionPass = graph.AddPass(
				name + " AO", Rhi::QueueType::Compute,
				[&](RenderGraphBuilder& b)
				{
					b.Read(depth, S::ShaderRead);
					b.Read(normal, S::ShaderRead);
					b.Read(params, S::ShaderRead);
					b.Write(raw, S::ShaderWrite);
				},
				[program = desc.AmbientOcclusion, label = name + " AO", depth, depthFormat, depthAspect, normal, params, raw, aoWidth,
					aoHeight](RenderCommandContext& c)
				{
					using B = ScreenSpaceAoBindings;
					const std::array<Rhi::DescriptorWrite, B::Count> writes{ TextureWrite(c, B::Depth, depth, depthFormat, depthAspect),
						TextureWrite(c, B::Normal, normal, Rhi::Format::RGBA16Float), BufferWrite(c, B::Params, params),
						TextureWrite(c, B::Output, raw, Rhi::Format::R32Float) };
					Dispatch(c, program, label, writes, aoWidth, aoHeight);
				});
			resources.BlurPass = graph.AddPass(
				name + " AO blur", Rhi::QueueType::Compute,
				[&](RenderGraphBuilder& b)
				{
					b.Read(raw, S::ShaderRead);
					b.Read(depth, S::ShaderRead);
					b.Read(params, S::ShaderRead);
					b.Write(visibility, S::ShaderWrite);
				},
				[program = desc.Blur, label = name + " AO blur", raw, depth, depthFormat, depthAspect, params, visibility, width, height](
					RenderCommandContext& c)
				{
					using B = ScreenSpaceBlurBindings;
					const std::array<Rhi::DescriptorWrite, B::Count> writes{ TextureWrite(c, B::Source, raw, Rhi::Format::R32Float),
						TextureWrite(c, B::Depth, depth, depthFormat, depthAspect), BufferWrite(c, B::Params, params),
						TextureWrite(c, B::Output, visibility, Rhi::Format::R32Float) };
					Dispatch(c, program, label, writes, width, height);
				});
		}
		else
		{
			// A 1x1 stand-in the composite never reads (AoEnabled = 0), so the table is complete.
			auto standInDesc = OcclusionDesc(1, 1);
			standInDesc.Usage = Rhi::TextureUsage::Sampled | Rhi::TextureUsage::TransferDestination;
			const std::string standInName = name + " AO stand-in";
			standInDesc.DebugName = standInName;
			visibility = graph.CreateTexture(standInDesc);
			const float one = 1.0f;
			AddTextureUpload(
				graph, name + " AO stand-in upload", std::as_bytes(std::span(&one, 1)), visibility, { 0, {}, {}, { 1, 1, 1 } });
		}

		// Reflections, or one 1x1 zero stand-in for the reflection, reflectance and specular.
		GraphTexture reflection;
		GraphTexture reflectance;
		GraphTexture specular;

		if (ssr || probes)
		{
			reflectance = *frame.Reflectance;
			specular = *frame.Specular;
		}

		if (ssr)
		{
			const bool half = resources.ParamsRecord.SsrHalf != 0u;
			const std::uint32_t traceWidth = half ? (width + 1u) / 2u : width;
			const std::uint32_t traceHeight = half ? (height + 1u) / 2u : height;
			auto reflectionDesc = OutputDesc(traceWidth, traceHeight);
			const std::string reflectionName = name + " reflections";
			reflectionDesc.DebugName = reflectionName;
			reflection = graph.CreateTexture(reflectionDesc);
			reflectance = *frame.Reflectance;
			specular = *frame.Specular;
			resources.Reflection = reflection;
			const auto normal = frame.Normal;
			const auto color = frame.Color;
			const auto indirect = frame.Indirect;
			const bool useHistory = resources.ParamsRecord.SsrHistory != 0u;
			const bool useBackDepth = resources.ParamsRecord.SsrBackDepth != 0u;
			GraphTexture backDepth;

			if (useBackDepth)
			{
				backDepth = *frame.BackDepth;
			}
			else
			{
				Rhi::TextureDesc standInDesc;
				standInDesc.Extent = { 1, 1, 1 };
				standInDesc.PixelFormat = Rhi::Format::R32Float;
				standInDesc.Usage = Rhi::TextureUsage::Sampled | Rhi::TextureUsage::TransferDestination;
				const std::string standInName = name + " back depth stand-in";
				standInDesc.DebugName = standInName;
				backDepth = graph.CreateTexture(standInDesc);
				const float zero = 0.0f;
				AddTextureUpload(graph, name + " back depth stand-in upload", std::as_bytes(std::span(&zero, 1)), backDepth,
					{ 0, {}, {}, { 1, 1, 1 } });
			}

			const auto backDepthFormat = useBackDepth ? Rhi::Format::D32Float : Rhi::Format::R32Float;
			const auto backDepthAspect = useBackDepth ? Rhi::TextureAspect::Depth : Rhi::TextureAspect::Automatic;
			const auto historyTexture = useHistory ? *frame.History : color;
			GraphTexture velocity;

			if (useHistory)
			{
				velocity = *frame.Velocity;
			}
			else
			{
				Rhi::TextureDesc standInDesc;
				standInDesc.Extent = { 1, 1, 1 };
				standInDesc.PixelFormat = Rhi::Format::RG16Float;
				standInDesc.Usage = Rhi::TextureUsage::Sampled | Rhi::TextureUsage::TransferDestination;
				const std::string standInName = name + " velocity stand-in";
				standInDesc.DebugName = standInName;
				velocity = graph.CreateTexture(standInDesc);
				const std::array<std::uint16_t, 2> zero{};
				AddTextureUpload(
					graph, name + " velocity stand-in upload", std::as_bytes(std::span(zero)), velocity, { 0, {}, {}, { 1, 1, 1 } });
			}

			resources.ReflectionPass = graph.AddPass(
				name + " reflections", Rhi::QueueType::Compute,
				[&](RenderGraphBuilder& b)
				{
					b.Read(depth, S::ShaderRead);
					b.Read(normal, S::ShaderRead);
					b.Read(color, S::ShaderRead);
					b.Read(indirect, S::ShaderRead);
					b.Read(visibility, S::ShaderRead);
					b.Read(params, S::ShaderRead);
					b.Read(velocity, S::ShaderRead);
					b.Read(backDepth, S::ShaderRead);

					if (useHistory)
					{
						b.Read(historyTexture, S::ShaderRead);
					}

					b.Write(reflection, S::ShaderWrite);
				},
				[program = desc.Reflection, label = name + " reflections", depth, depthFormat, depthAspect, normal, color, indirect,
					visibility, params, reflection, velocity, historyTexture, backDepth, backDepthFormat, backDepthAspect, traceWidth,
					traceHeight](RenderCommandContext& c)
				{
					using B = ScreenSpaceReflectionBindings;
					const std::array<Rhi::DescriptorWrite, B::Count> writes{ TextureWrite(c, B::Depth, depth, depthFormat, depthAspect),
						TextureWrite(c, B::Normal, normal, Rhi::Format::RGBA16Float),
						TextureWrite(c, B::Color, color, Rhi::Format::RGBA16Float),
						TextureWrite(c, B::Indirect, indirect, Rhi::Format::RGBA16Float),
						TextureWrite(c, B::Ao, visibility, Rhi::Format::R32Float), BufferWrite(c, B::Params, params),
						TextureWrite(c, B::Output, reflection, Rhi::Format::RGBA16Float),
						TextureWrite(c, B::Velocity, velocity, Rhi::Format::RG16Float),
						TextureWrite(c, B::History, historyTexture, Rhi::Format::RGBA16Float),
						TextureWrite(c, B::BackDepth, backDepth, backDepthFormat, backDepthAspect) };
					Dispatch(c, program, label, writes, traceWidth, traceHeight);
				});
		}
		else
		{
			// Never read by the composite (SsrEnabled = 0).
			auto standInDesc = OutputDesc(1, 1);
			standInDesc.Usage = Rhi::TextureUsage::Sampled | Rhi::TextureUsage::TransferDestination;
			const std::string standInName = name + " reflection stand-in";
			standInDesc.DebugName = standInName;
			reflection = graph.CreateTexture(standInDesc);

			if (!probes)
			{
				reflectance = reflection;
				specular = reflection;
			}

			const std::array<std::uint16_t, 4> zero{};
			AddTextureUpload(
				graph, name + " reflection stand-in upload", std::as_bytes(std::span(zero)), reflection, { 0, {}, {}, { 1, 1, 1 } });
		}

		// Probe inputs, or 1x1 stand-ins the composite never reads (ProbeCount = 0).
		GraphTexture probeCubes;
		GraphBuffer probeRecords;
		GraphTexture objectId;
		std::uint32_t probeLayers = 6u;
		std::uint32_t probeMips = 1;

		if (probes)
		{
			probeCubes = frame.Probes->Cubes;
			probeRecords = frame.Probes->Records;
			objectId = frame.Probes->ObjectId;
			probeLayers = graph.GetDesc(probeCubes).ArrayLayers;
			probeMips = frame.Probes->MipCount;
		}
		else
		{
			Rhi::TextureDesc cubeDesc;
			cubeDesc.Dimension = Rhi::TextureDimension::TextureCube;
			cubeDesc.Extent = { 1, 1, 1 };
			cubeDesc.ArrayLayers = 6u;
			cubeDesc.PixelFormat = Rhi::Format::RGBA16Float;
			cubeDesc.Usage = Rhi::TextureUsage::Sampled | Rhi::TextureUsage::TransferDestination;
			const std::string cubeName = name + " probe stand-in";
			cubeDesc.DebugName = cubeName;
			probeCubes = graph.CreateTexture(cubeDesc);
			const std::array<std::uint16_t, 4> zero{};

			for (std::uint32_t face = 0; face < 6u; ++face)
			{
				AddTextureUpload(graph, cubeName + " upload", std::as_bytes(std::span(zero)), probeCubes, { 0, { 0, face }, {}, { 1, 1, 1 } });
			}

			const std::array<std::byte, ScreenSpaceProbeRecordBytes> record{};
			probeRecords = graph.CreateUpload(record, name + " probe records stand-in", Rhi::BufferUsage::Storage, 16);
			Rhi::TextureDesc idDesc;
			idDesc.Extent = { 1, 1, 1 };
			idDesc.PixelFormat = Rhi::Format::R32Float;
			idDesc.Usage = Rhi::TextureUsage::Sampled | Rhi::TextureUsage::TransferDestination;
			const std::string idName = name + " object id stand-in";
			idDesc.DebugName = idName;
			objectId = graph.CreateTexture(idDesc);
			const float zeroId = 0.0f;
			AddTextureUpload(graph, idName + " upload", std::as_bytes(std::span(&zeroId, 1)), objectId, { 0, {}, {}, { 1, 1, 1 } });
		}

		if (!desc.ProbeSampler)
		{
			throw std::invalid_argument(name + " needs the probe sampler");
		}

		// The reflection term the temporal filter reads, or a 1x1 stand-in the composite never writes.
		GraphTexture reflectionTerm;

		if (temporal)
		{
			const auto& t = *frame.ReflectionTemporal;
			const auto nextDesc = graph.GetDesc(t.Next);

			if (!sameSize(nextDesc) || nextDesc.PixelFormat != Rhi::Format::RGBA16Float ||
				(t.Previous && (!sameSize(graph.GetDesc(*t.Previous)) || graph.GetDesc(*t.Previous).PixelFormat != Rhi::Format::RGBA16Float)))
			{
				throw std::invalid_argument(name + " reflection history must be color-sized RGBA16Float textures");
			}

			auto termDesc = OutputDesc(width, height);
			termDesc.Usage = Rhi::TextureUsage::Storage | Rhi::TextureUsage::Sampled;
			const std::string termName = name + " reflection term";
			termDesc.DebugName = termName;
			reflectionTerm = graph.CreateTexture(termDesc);
		}
		else
		{
			auto termDesc = OutputDesc(1, 1);
			termDesc.Usage = Rhi::TextureUsage::Storage | Rhi::TextureUsage::Sampled;
			const std::string termName = name + " reflection term stand-in";
			termDesc.DebugName = termName;
			reflectionTerm = graph.CreateTexture(termDesc);
		}

		auto outputDesc = OutputDesc(width, height);
		const std::string outputName = name + " output";
		outputDesc.DebugName = outputName;
		const auto output = graph.CreateTexture(outputDesc);
		resources.Output = output;
		const auto color = frame.Color;
		const auto indirect = frame.Indirect;
		const auto surfaceNormal = frame.Normal;
		resources.CompositePass = graph.AddPass(
			name + " composite", Rhi::QueueType::Compute,
			[&](RenderGraphBuilder& b)
			{
				b.Read(color, S::ShaderRead);
				b.Read(indirect, S::ShaderRead);
				b.Read(visibility, S::ShaderRead);
				b.Read(depth, S::ShaderRead);
				b.Read(params, S::ShaderRead);
				b.Read(reflection, S::ShaderRead);
				b.Read(surfaceNormal, S::ShaderRead);

				if (ssr || probes)
				{
					b.Read(reflectance, S::ShaderRead);
					b.Read(specular, S::ShaderRead);
				}

				b.Read(probeCubes, S::ShaderRead);
				b.Read(probeRecords, S::ShaderRead);
				b.Read(objectId, S::ShaderRead);
				b.Write(output, S::ShaderWrite);
				b.Write(reflectionTerm, S::ShaderWrite);
			},
			[program = desc.Composite, label = name + " composite", color, indirect, visibility, depth, depthFormat, depthAspect, params,
				output, reflection, reflectance, specular, surfaceNormal, probeCubes, probeRecords, objectId, reflectionTerm, probeLayers,
				probeMips, probeSampler = desc.ProbeSampler, width, height](RenderCommandContext& c)
			{
				using B = ScreenSpaceCompositeBindings;
				const std::array<Rhi::DescriptorWrite, B::Count> writes{ TextureWrite(c, B::Color, color, Rhi::Format::RGBA16Float),
					TextureWrite(c, B::Indirect, indirect, Rhi::Format::RGBA16Float),
					TextureWrite(c, B::Ao, visibility, Rhi::Format::R32Float), TextureWrite(c, B::Depth, depth, depthFormat, depthAspect),
					BufferWrite(c, B::Params, params), TextureWrite(c, B::Output, output, Rhi::Format::RGBA16Float),
					TextureWrite(c, B::Reflection, reflection, Rhi::Format::RGBA16Float),
					TextureWrite(c, B::Reflectance, reflectance, Rhi::Format::RGBA16Float),
					TextureWrite(c, B::Specular, specular, Rhi::Format::RGBA16Float),
					TextureWrite(c, B::Normal, surfaceNormal, Rhi::Format::RGBA16Float), Rhi::DescriptorWrite{},
					Rhi::DescriptorWrite{}, BufferWrite(c, B::ProbeRecords, probeRecords),
					TextureWrite(c, B::ObjectId, objectId, Rhi::Format::R32Float),
					TextureWrite(c, B::ReflectionTermOut, reflectionTerm, Rhi::Format::RGBA16Float) };
				auto completed = writes;
				Rhi::TextureViewDesc cubeView;
				cubeView.Dimension = Rhi::TextureViewDimension::TextureCubeArray;
				cubeView.PixelFormat = Rhi::Format::RGBA16Float;
				cubeView.MipLevelCount = probeMips;
				cubeView.ArrayLayerCount = probeLayers;
				completed[B::ProbeCubes].Binding = B::ProbeCubes;
				completed[B::ProbeCubes].TextureResource = &c.CreateView(probeCubes, cubeView);
				completed[B::ProbeSampler].Binding = B::ProbeSampler;
				completed[B::ProbeSampler].SamplerResource = probeSampler;
				Dispatch(c, program, label, completed, width, height);
			});

		if (temporal)
		{
			const auto& t = *frame.ReflectionTemporal;
			auto filteredDesc = OutputDesc(width, height);
			const std::string filteredName = name + " output (reflections filtered)";
			filteredDesc.DebugName = filteredName;
			const auto filtered = graph.CreateTexture(filteredDesc);
			const auto velocity = *frame.Velocity;
			const auto history = t.Previous.value_or(reflectionTerm); // Not read without a previous frame.
			const auto next = t.Next;
			resources.ReflectionTemporalPass = graph.AddPass(
				name + " reflection temporal", Rhi::QueueType::Compute,
				[&](RenderGraphBuilder& b)
				{
					b.Read(output, S::ShaderRead);
					b.Read(reflectionTerm, S::ShaderRead);
					b.Read(velocity, S::ShaderRead);

					if (t.Previous)
					{
						b.Read(history, S::ShaderRead);
					}

					b.Read(params, S::ShaderRead);
					b.Write(filtered, S::ShaderWrite);
					b.Write(next, S::ShaderWrite);
				},
				[program = desc.ReflectionTemporal, label = name + " reflection temporal", output, reflectionTerm, velocity, history, params,
					filtered, next, sampler = desc.ProbeSampler, width, height](RenderCommandContext& c)
				{
					using B = ScreenSpaceReflectionTemporalBindings;
					std::array<Rhi::DescriptorWrite, B::Count> writes{ TextureWrite(c, B::Color, output, Rhi::Format::RGBA16Float),
						TextureWrite(c, B::Term, reflectionTerm, Rhi::Format::RGBA16Float),
						TextureWrite(c, B::Velocity, velocity, Rhi::Format::RG16Float),
						TextureWrite(c, B::History, history, Rhi::Format::RGBA16Float), Rhi::DescriptorWrite{},
						BufferWrite(c, B::Params, params), TextureWrite(c, B::Output, filtered, Rhi::Format::RGBA16Float),
						TextureWrite(c, B::HistoryOut, next, Rhi::Format::RGBA16Float) };
					writes[B::Sampler].Binding = B::Sampler;
					writes[B::Sampler].SamplerResource = sampler;
					Dispatch(c, program, label, writes, width, height);
				});
			resources.Output = filtered;
		}

		return resources;
	}

} // namespace Swim::Render
