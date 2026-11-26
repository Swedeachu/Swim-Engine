#include "Engine/Systems/Renderer/UiRendering/UiRenderer.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderCommandContext.h"

#include <algorithm>
#include <stdexcept>

namespace Swim::Render
{

	namespace
	{

		using S = Rhi::ResourceState;

		bool IsSrgbFormat(Rhi::Format format)
		{
			return format == Rhi::Format::RGBA8UnormSrgb || format == Rhi::Format::BGRA8UnormSrgb;
		}

		bool HasUsage(Rhi::TextureUsage usage, Rhi::TextureUsage required)
		{
			return (static_cast<std::uint32_t>(usage) & static_cast<std::uint32_t>(required)) != 0;
		}

	} // namespace

	Rhi::GraphicsPipelineDesc UiRenderer::PipelineDesc(
		Rhi::Format colorFormat, Rhi::ShaderProgram& program, Rhi::PipelineLayout& layout, Rhi::Format depthFormat, UI::UiBlend blend)
	{
		if (colorFormat == Rhi::Format::Undefined || Rhi::IsDepthFormat(colorFormat))
		{
			throw std::invalid_argument("UI pipelines need a color format");
		}

		if (depthFormat != Rhi::Format::Undefined && depthFormat != Rhi::Format::D32Float)
		{
			throw std::invalid_argument("UI depth pipelines test D32Float scene depth");
		}

		// The desc holds spans, so each format's one-element array has static storage.
		static const auto formatStorage = []
		{
			std::array<std::array<Rhi::Format, 1>, static_cast<std::size_t>(Rhi::Format::BGR10A2Unorm) + 1> storage{};

			for (std::size_t i = 0; i < storage.size(); ++i)
			{
				storage[i][0] = static_cast<Rhi::Format>(i);
			}

			return storage;
		}();
		const auto formatIndex = static_cast<std::size_t>(colorFormat);

		if (formatIndex >= formatStorage.size())
		{
			throw std::invalid_argument("Unknown UI target format");
		}

		static constexpr std::array<Rhi::BlendAttachmentState, 1> Blend{ Rhi::BlendAttachmentState{ true, Rhi::BlendFactor::One,
			Rhi::BlendFactor::OneMinusSourceAlpha, Rhi::BlendOp::Add, Rhi::BlendFactor::One, Rhi::BlendFactor::OneMinusSourceAlpha,
			Rhi::BlendOp::Add, Rhi::ColorWriteMask::All } };
		static constexpr std::array<Rhi::BlendAttachmentState, 1> Invert{ Rhi::BlendAttachmentState{ true,
			Rhi::BlendFactor::OneMinusDestinationColor, Rhi::BlendFactor::OneMinusSourceColor, Rhi::BlendOp::Add, Rhi::BlendFactor::One,
			Rhi::BlendFactor::OneMinusSourceAlpha, Rhi::BlendOp::Add, Rhi::ColorWriteMask::All } };
		const bool invert = blend == UI::UiBlend::Invert;
		Rhi::GraphicsPipelineDesc pipeline{};
		pipeline.Program = &program;
		pipeline.Layout = &layout;
		pipeline.ColorFormats = formatStorage[formatIndex];
		pipeline.BlendAttachments = invert ? Invert : Blend;
		pipeline.DepthStencilFormat = depthFormat;
		pipeline.DepthStencil.DepthTest = depthFormat != Rhi::Format::Undefined;
		pipeline.DepthStencil.DepthWrite = false;
		pipeline.DepthStencil.DepthCompare = Rhi::CompareOp::GreaterEqual; // Canonical reverse-Z.
		pipeline.Raster.Cull = Rhi::CullMode::None;						   // Panels are two-sided.
		const bool tested = depthFormat != Rhi::Format::Undefined;
		pipeline.DebugName =
			invert ? (tested ? "UI invert quads (depth tested)" : "UI invert quads") : (tested ? "UI quads (depth tested)" : "UI quads");
		return pipeline;
	}

	UiRenderer::UiRenderer(UiRendererDesc descInput) : desc(std::move(descInput))
	{
		if (desc.MaxQuads == 0 || desc.MaxQuads > (1u << 24))
		{
			throw std::invalid_argument(desc.DebugName + " needs 1 .. 16M quads per frame");
		}
	}

	std::optional<GraphPass> UiRenderer::Record(
		RenderGraph& graph, const UiRenderFrame& frame, const UiRenderProgram& program, Rhi::DescriptorTable& bindless)
	{
		const auto& name = desc.DebugName;

		if (!program.Pipeline || !program.Layout)
		{
			throw std::invalid_argument(name + " needs its pipeline and layout");
		}

		ValidateUiCompositionSettings(frame.Composition);
		const auto targetDesc = graph.GetDesc(frame.Target);

		if (targetDesc.Dimension != Rhi::TextureDimension::Texture2D || !HasUsage(targetDesc.Usage, Rhi::TextureUsage::ColorAttachment) ||
			Rhi::IsDepthFormat(targetDesc.PixelFormat) || targetDesc.Samples != Rhi::SampleCount::X1)
		{
			throw std::invalid_argument(name + " needs a single-sampled 2D color attachment target");
		}

		if (frame.Composition.Encoding == UiOutputEncoding::Srgb && IsSrgbFormat(targetDesc.PixelFormat))
		{
			throw std::invalid_argument(name + ": an *Srgb target encodes in hardware; use UiOutputEncoding::Linear");
		}

		if (frame.TargetMip >= targetDesc.MipLevels)
		{
			throw std::invalid_argument(name + " target mip is beyond the texture's mips");
		}

		const std::uint32_t mip = frame.TargetMip;
		const std::uint32_t width = std::max(targetDesc.Extent.Width >> mip, 1u);
		const std::uint32_t height = std::max(targetDesc.Extent.Height >> mip, 1u);
		const bool world = frame.ClipFromCanvas.has_value();

		if (world && (frame.OffsetX != 0.0f || frame.OffsetY != 0.0f))
		{
			throw std::invalid_argument(name + ": canvas matrices replace offsets (fold them into ClipFromCanvas)");
		}

		if (frame.Depth)
		{
			const auto depthDesc = graph.GetDesc(*frame.Depth);

			if (!world || !program.DepthPipeline || depthDesc.PixelFormat != Rhi::Format::D32Float ||
				!HasUsage(depthDesc.Usage, Rhi::TextureUsage::DepthStencilAttachment) || depthDesc.Extent.Width != width ||
				depthDesc.Extent.Height != height || depthDesc.Samples != Rhi::SampleCount::X1)
			{
				throw std::invalid_argument(
					name + " depth needs a canvas matrix, the depth pipeline and a single-sampled D32Float attachment of the drawn extent");
			}
		}

		const auto constants = world ? Ui::BuildCanvasDrawConstants(width, height, frame.Composition, *frame.ClipFromCanvas, frame.Opacity)
									 : Ui::BuildDrawConstants(width, height, frame.Composition, frame.Opacity);

		auto constantsWithGlint = constants;
		constantsWithGlint.Glint[0] = std::isfinite(frame.Glint.Time) ? frame.Glint.Time : 0.0f;
		constantsWithGlint.Glint[1] = float(frame.Glint.Sampler);
		Ui::QuadBuildDesc build;
		build.DpiScale = frame.DpiScale;
		build.OffsetX = frame.OffsetX;
		build.OffsetY = frame.OffsetY;

		if (frame.Atlas)
		{
			build.AtlasPageSize = frame.Atlas->PageSize;
			build.AtlasTextures = frame.Atlas->TextureIndices;
			build.AtlasSampler = frame.Atlas->SamplerIndex;
			build.AtlasPointSampler = frame.Atlas->PointSamplerIndex;
		}

		build.Glint = &frame.Glint;

		Ui::QuadBuildStats built;
		lastQuads = Ui::BuildQuads(frame.Paint, build, &built);
		lastConstants = constantsWithGlint;
		const auto runs = Ui::BuildDrawRuns(lastQuads);
		stats = { static_cast<std::uint32_t>(lastQuads.size()), built.Solids, built.Glyphs, built.Images, built.Culled,
			static_cast<std::uint32_t>(runs.size()) };

		if (lastQuads.size() > desc.MaxQuads)
		{
			throw std::length_error(name + " exceeds MaxQuads");
		}

		const bool inverts = std::any_of(runs.begin(), runs.end(),
			[](const Ui::DrawRun& run)
			{
				return run.Blend == UiQuadBlendInvert;
			});

		if (inverts && !(frame.Depth ? program.InvertDepthPipeline : program.InvertPipeline))
		{
			throw std::invalid_argument(name + " has inverting images but no invert pipeline");
		}

		if (lastQuads.empty() && !frame.Clear)
		{
			return std::nullopt;
		}

		std::optional<GraphBuffer> quads;

		if (!lastQuads.empty())
		{
			quads = graph.CreateUpload(std::as_bytes(std::span(lastQuads)), name + " quads", Rhi::BufferUsage::Storage, 16);
		}

		std::vector<GraphTexture> sampled;

		if (frame.Atlas)
		{
			sampled.insert(sampled.end(), frame.Atlas->Pages.begin(), frame.Atlas->Pages.end());
		}

		sampled.insert(sampled.end(), frame.Images.begin(), frame.Images.end());
		const auto target = frame.Target;
		const auto depth = frame.Depth;
		const bool clear = frame.Clear;
		const auto clearColor = frame.ClearColor;
		const auto count = static_cast<std::uint32_t>(lastQuads.size());
		const auto format = targetDesc.PixelFormat;
		const Rhi::TextureSubresourceRange range{ mip, 1, 0, 1 };
		return graph.AddPass(
			name + " draw", Rhi::QueueType::Graphics,
			[&](RenderGraphBuilder& b)
			{
				if (clear)
				{
					b.Write(target, S::ColorAttachment, range);
				}
				else
				{
					b.ReadWrite(target, S::ColorAttachment, range);
				}

				if (depth)
				{
					// The attachment layout is the writable one; the pipeline never writes depth.
					b.ReadWrite(*depth, S::DepthStencilWrite);
				}

				if (quads)
				{
					b.Read(*quads, S::ShaderRead);
				}

				for (const auto texture : sampled)
				{
					b.Read(texture, S::ShaderRead);
				}
			},
			[program, label = name + " draw", target, depth, quads, clear, clearColor, constants = constantsWithGlint, count, runs,
				bindlessTable = &bindless, width, height, format, mip](RenderCommandContext& c)
			{
				Rhi::TextureViewDesc colorView;
				colorView.PixelFormat = format;
				colorView.BaseMipLevel = mip;
				std::array<Rhi::RenderingAttachmentDesc, 1> colors{};
				colors[0].View = &c.CreateView(target, colorView);
				colors[0].Load = clear ? Rhi::LoadOp::Clear : Rhi::LoadOp::Load;
				colors[0].Clear.Value = clearColor;
				std::optional<Rhi::DepthStencilAttachmentDesc> depthAttachment;

				if (depth)
				{
					Rhi::TextureViewDesc depthView;
					depthView.PixelFormat = Rhi::Format::D32Float;
					depthAttachment = Rhi::DepthStencilAttachmentDesc{ &c.CreateView(*depth, depthView), Rhi::LoadOp::Load,
						Rhi::StoreOp::Store, 0.0f, 0 };
				}

				auto& list = c.Commands();
				list.BeginRendering({ colors, depthAttachment ? &*depthAttachment : nullptr, { width, height } });

				if (count > 0)
				{
					auto table = c.CreateDescriptorTable({ program.Layout, 0, 0, label });

					if (!table)
					{
						throw std::runtime_error(label + " descriptor table could not be created");
					}

					const auto range = c.GetRange(*quads);
					Rhi::DescriptorWrite write{};
					write.Binding = UiRenderBindings::Quads;
					write.BufferResource = range.Buffer;
					write.BufferOffset = range.Offset;
					write.BufferRange = range.Size;
					table->Write({ &write, 1 });
					auto& retained = static_cast<Rhi::DescriptorTable&>(c.Retain(std::move(table)));
					list.SetViewport({ 0, 0, float(width), float(height) });
					list.SetScissor({ 0, 0, width, height });
					for (const auto& run : runs)
					{
						// The shader reads gl_InstanceIndex, which includes the first instance.
						const bool invert = run.Blend == UiQuadBlendInvert;
						list.BindGraphicsPipeline(depth ? (invert ? *program.InvertDepthPipeline : *program.DepthPipeline)
														: (invert ? *program.InvertPipeline : *program.Pipeline));
						list.BindDescriptorTable(0, retained);
						list.BindDescriptorTable(UiRenderBindings::BindlessSpace, *bindlessTable);
						list.PushConstants(
							Rhi::ShaderStageMask::Vertex | Rhi::ShaderStageMask::Fragment, 0, std::as_bytes(std::span(&constants, 1)));
						list.Draw(6, run.Count, 0, run.First);
					}
				}

				list.EndRendering();
			});
	}

} // namespace Swim::Render
