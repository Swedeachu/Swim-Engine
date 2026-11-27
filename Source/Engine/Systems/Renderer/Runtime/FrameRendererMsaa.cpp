#include "Engine/Systems/Renderer/Runtime/FrameRendererImpl.h"
#include "Engine/Logging/Log.h"

// Multisampled scene geometry (RenderSettings::MsaaSamples): the pipelines of the sky, depth
// prepass and opaque passes per sample count, and the resolve into the single-sample targets.
namespace Engine
{

	using namespace FrameRendererDetail;

	namespace
	{
		constexpr std::array<S::Format, 7> ResolveFormats{ R::ForwardPlusRenderer::ColorFormat, R::ForwardPlusRenderer::ObjectIdFormat,
			R::ForwardPlusRenderer::VelocityFormat, R::ForwardPlusRenderer::NormalFormat, R::ForwardPlusRenderer::IndirectFormat,
			R::ForwardPlusRenderer::ReflectanceFormat, R::ForwardPlusRenderer::SpecularFormat };
		constexpr std::array<S::BlendAttachmentState, 7> ResolveBlends{};
		constexpr std::array<S::Format, 1> SkyFormats{ R::ForwardPlusRenderer::ColorFormat };

		// The largest supported count (SampleCounts mask) at or below the wanted one.
		S::SampleCount LowerToSupported(std::uint32_t wanted, std::uint32_t supported)
		{
			for (std::uint32_t count = 16; count > 1; count /= 2)
			{
				if (count <= wanted && (supported & count) != 0)
				{
					return static_cast<S::SampleCount>(count);
				}
			}

			return S::SampleCount::X1;
		}
	} // namespace

	S::SampleCount FrameRenderer::Impl::MsaaSampleCount(const RenderSettings& settings)
	{
		const auto samples = LowerToSupported(settings.MsaaSamples, renderDevice.GetCapabilities().SampleCounts);
		const auto key = static_cast<std::uint32_t>(samples);

		if (samples == S::SampleCount::X1 || msaaPasses.contains(key))
		{
			return samples;
		}

		if (msaaFailed.contains(key))
		{
			return S::SampleCount::X1;
		}

		try
		{
			if (!shaders.Contains("ForwardDepthCoverage") || !shaders.Contains("ForwardMsaaResolve"))
			{
				throw std::runtime_error("the ForwardDepthCoverage and ForwardMsaaResolve shaders are missing");
			}

			const auto bindlessSpace = R::ForwardPlusBindlessSpace(desc.BindlessTextures, desc.BindlessSamplers);

			if (!forwardDepthCoverage.Program)
			{
				forwardDepthCoverage = LoadDrawProgram("ForwardDepthCoverage", &bindlessSpace);
			}

			if (!msaaResolve.Program)
			{
				msaaResolve = LoadDrawProgram("ForwardMsaaResolve", nullptr);
			}

			if (!msaaResolvePipeline)
			{
				S::GraphicsPipelineDesc resolveDesc{};
				resolveDesc.Program = msaaResolve.Program.get();
				resolveDesc.Layout = msaaResolve.Layout.get();
				resolveDesc.ColorFormats = ResolveFormats;
				resolveDesc.BlendAttachments = ResolveBlends;
				resolveDesc.DepthStencilFormat = R::CanonicalDepthFormat;
				resolveDesc.DepthStencil.DepthTest = true; // Always passes; on for the depth write.
				resolveDesc.DepthStencil.DepthWrite = true;
				resolveDesc.DepthStencil.DepthCompare = S::CompareOp::Always;
				resolveDesc.Raster.Cull = S::CullMode::None;
				resolveDesc.DebugName = "MSAA resolve";
				msaaResolvePipeline = Require(device.CreateGraphicsPipeline(resolveDesc), "MSAA resolve");
			}

			MsaaPasses passes;
			auto opaqueDesc =
				R::ForwardPlusRenderer::PipelineDesc(R::ForwardPlusBin::Opaque, *forwardOpaque.Program, *forwardOpaque.Layout);
			opaqueDesc.Samples = samples;
			opaqueDesc.AlphaToCoverage = true;
			opaqueDesc.DebugName = "Forward+ opaque (MSAA)";
			passes.Opaque = Require(device.CreateGraphicsPipeline(opaqueDesc), "multisampled Forward+ opaque");
			auto depthDesc = R::ForwardPlusRenderer::DepthPrepassPipelineDesc(*forwardDepthCoverage.Program, *forwardDepthCoverage.Layout);
			depthDesc.Samples = samples;
			depthDesc.AlphaToCoverage = true;
			depthDesc.DebugName = "Forward+ depth prepass (MSAA)";
			passes.Depth = Require(device.CreateGraphicsPipeline(depthDesc), "multisampled Forward+ depth prepass");
			auto prepassedDesc = R::ForwardPlusRenderer::PrepassedPipelineDesc(*forwardPrepassed.Program, *forwardPrepassed.Layout);
			prepassedDesc.Samples = samples;
			prepassedDesc.AlphaToCoverage = true;
			prepassedDesc.DebugName = "Forward+ prepassed opaque (MSAA)";
			passes.Prepassed = Require(device.CreateGraphicsPipeline(prepassedDesc), "multisampled Forward+ prepassed opaque");
			S::GraphicsPipelineDesc skyDesc{};
			skyDesc.Program = skyBackground.Program.get();
			skyDesc.Layout = skyBackground.Layout.get();
			skyDesc.ColorFormats = SkyFormats;
			skyDesc.DepthStencilFormat = R::CanonicalDepthFormat;
			skyDesc.DepthStencil.DepthTest = false;
			skyDesc.DepthStencil.DepthWrite = false;
			skyDesc.Raster.Cull = S::CullMode::None;
			skyDesc.Samples = samples;
			skyDesc.DebugName = "Sky background (MSAA)";
			passes.Sky = Require(device.CreateGraphicsPipeline(skyDesc), "multisampled sky background");
			// The opaque passes only: the back depth and the transparent layers draw at one
			// sample after the resolve, and deferred local lights are off when multisampled.
			R::ForwardPlusRendererDesc forwardDesc;
			forwardDesc.Opaque = { passes.Opaque.get(), forwardOpaque.Layout.get() };
			forwardDesc.Transparent = { forwardTransparentPipeline.get(), forwardTransparent.Layout.get() };
			forwardDesc.DepthPrepass = { passes.Depth.get(), forwardDepthCoverage.Layout.get() };
			forwardDesc.OpaquePrepassed = { passes.Prepassed.get(), forwardPrepassed.Layout.get() };
			forwardDesc.SortPipeline = sortProgram.Pipeline.get();
			forwardDesc.SortLayout = sortProgram.Layout.get();
			forwardDesc.DrawPath = drawPath;
			passes.Forward = std::make_unique<R::ForwardPlusRenderer>(device, forwardDesc);
			msaaPasses.emplace(key, std::move(passes));
			return samples;
		}
		catch (const std::exception& error)
		{
			msaaFailed.insert(key);
			Logging::Write(Logging::Level::Warning,
				"[Render] " + std::to_string(key) + "x MSAA is unavailable, drawing with one sample: " + error.what());
			return S::SampleCount::X1;
		}
	}

	void FrameRenderer::Impl::RecordMsaaResolve(R::RenderGraph& graph, const R::ForwardPlusTargets& source,
		const R::ForwardPlusTargets& target, std::uint32_t width, std::uint32_t height)
	{
		const std::array<R::GraphTexture, 8> inputs{ source.Color, source.ObjectId, *source.Velocity, *source.Normal, *source.Indirect,
			*source.Reflectance, *source.Specular, source.Depth };
		const std::array<R::GraphTexture, 7> outputs{ target.Color, target.ObjectId, *target.Velocity, *target.Normal, *target.Indirect,
			*target.Reflectance, *target.Specular };
		const auto depth = target.Depth;
		auto* pipeline = msaaResolvePipeline.get();
		auto* layout = msaaResolve.Layout.get();
		graph.AddPass(
			"MSAA resolve", S::QueueType::Graphics,
			[&](R::RenderGraphBuilder& b)
			{
				for (const auto texture : inputs)
				{
					b.Read(texture, ResourceState::ShaderRead);
				}

				for (const auto texture : outputs)
				{
					b.Write(texture, ResourceState::ColorAttachment);
				}

				b.Write(depth, ResourceState::DepthStencilWrite);
			},
			[inputs, outputs, depth, pipeline, layout, width, height](R::RenderCommandContext& c)
			{
				auto table = c.CreateDescriptorTable({ layout, 0, 0, "MSAA resolve" });

				if (!table)
				{
					throw std::runtime_error("FrameRenderer: cannot create the MSAA resolve descriptor table");
				}

				std::array<S::DescriptorWrite, 8> writes{};
				S::TextureViewDesc depthRead;
				depthRead.PixelFormat = R::CanonicalDepthFormat;
				depthRead.Aspect = S::TextureAspect::Depth;

				for (std::uint32_t i = 0; i < writes.size(); ++i)
				{
					writes[i].Binding = i;
					writes[i].TextureResource = i == 7 ? &c.CreateView(inputs[i], depthRead) : &c.CreateView(inputs[i]);
				}

				table->Write(writes);
				auto& retained = static_cast<S::DescriptorTable&>(c.Retain(std::move(table)));
				std::array<S::RenderingAttachmentDesc, 7> colors{};

				for (std::size_t i = 0; i < colors.size(); ++i)
				{
					colors[i].View = &c.CreateView(outputs[i]);
					colors[i].Load = S::LoadOp::Discard;
				}

				S::TextureViewDesc depthView;
				depthView.PixelFormat = R::CanonicalDepthFormat;
				const S::DepthStencilAttachmentDesc depthAttachment{ &c.CreateView(depth, depthView), S::LoadOp::Discard, S::StoreOp::Store,
					R::DepthClearValue(R::CanonicalDepthConvention), 0 };
				auto& list = c.Commands();
				list.BeginRendering({ colors, &depthAttachment, { width, height } });
				list.BindGraphicsPipeline(*pipeline);
				list.BindDescriptorTable(0, retained);
				list.SetViewport({ 0, 0, float(width), float(height) });
				list.SetScissor({ 0, 0, width, height });
				list.Draw(3);
				list.EndRendering();
			});
	}

} // namespace Engine
