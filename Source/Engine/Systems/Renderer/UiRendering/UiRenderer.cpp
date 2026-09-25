#include "Engine/Systems/Renderer/UiRendering/UiRenderer.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderCommandContext.h"

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

	Rhi::GraphicsPipelineDesc UiRenderer::PipelineDesc(Rhi::Format colorFormat, Rhi::ShaderProgram& program, Rhi::PipelineLayout& layout)
	{
		if (colorFormat == Rhi::Format::Undefined || Rhi::IsDepthFormat(colorFormat))
		{
			throw std::invalid_argument("UI pipelines need a color format");
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
		Rhi::GraphicsPipelineDesc pipeline{};
		pipeline.Program = &program;
		pipeline.Layout = &layout;
		pipeline.ColorFormats = formatStorage[formatIndex];
		pipeline.BlendAttachments = Blend;
		pipeline.DepthStencilFormat = Rhi::Format::Undefined;
		pipeline.DepthStencil.DepthTest = false;
		pipeline.DepthStencil.DepthWrite = false;
		pipeline.Raster.Cull = Rhi::CullMode::None;
		pipeline.DebugName = "UI quads";
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
		const std::uint32_t width = targetDesc.Extent.Width;
		const std::uint32_t height = targetDesc.Extent.Height;

		Ui::QuadBuildDesc build;
		build.DpiScale = frame.DpiScale;
		build.OffsetX = frame.OffsetX;
		build.OffsetY = frame.OffsetY;
		if (frame.Atlas)
		{
			build.AtlasPageSize = frame.Atlas->PageSize;
			build.AtlasTextures = frame.Atlas->TextureIndices;
			build.AtlasSampler = frame.Atlas->SamplerIndex;
		}
		Ui::QuadBuildStats built;
		lastQuads = Ui::BuildQuads(frame.Paint, build, &built);
		lastConstants = Ui::BuildDrawConstants(width, height, frame.Composition);
		stats = { static_cast<std::uint32_t>(lastQuads.size()), built.Solids, built.Glyphs, built.Images, built.Culled };
		if (lastQuads.size() > desc.MaxQuads)
		{
			throw std::length_error(name + " exceeds MaxQuads");
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
		const bool clear = frame.Clear;
		const auto clearColor = frame.ClearColor;
		const auto constants = lastConstants;
		const auto count = static_cast<std::uint32_t>(lastQuads.size());
		return graph.AddPass(
			name + " draw", Rhi::QueueType::Graphics,
			[&](RenderGraphBuilder& b)
			{
				if (clear)
				{
					b.Write(target, S::ColorAttachment);
				}
				else
				{
					b.ReadWrite(target, S::ColorAttachment);
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
			[program, label = name + " draw", target, quads, clear, clearColor, constants, count, bindlessTable = &bindless, width, height](
				RenderCommandContext& c)
			{
				std::array<Rhi::RenderingAttachmentDesc, 1> colors{};
				colors[0].View = &c.CreateView(target);
				colors[0].Load = clear ? Rhi::LoadOp::Clear : Rhi::LoadOp::Load;
				colors[0].Clear.Value = clearColor;
				auto& list = c.Commands();
				list.BeginRendering({ colors, nullptr, { width, height } });
				if (count > 0)
				{
					auto table = c.Device().CreateDescriptorTable({ program.Layout, 0, 0, label });
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
					list.BindGraphicsPipeline(*program.Pipeline);
					list.BindDescriptorTable(0, retained);
					list.BindDescriptorTable(UiRenderBindings::BindlessSpace, *bindlessTable);
					list.PushConstants(
						Rhi::ShaderStageMask::Vertex | Rhi::ShaderStageMask::Fragment, 0, std::as_bytes(std::span(&constants, 1)));
					list.Draw(6, count, 0, 0);
				}
				list.EndRendering();
			});
	}
} // namespace Swim::Render
