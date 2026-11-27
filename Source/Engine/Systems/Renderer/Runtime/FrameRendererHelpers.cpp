#include "Engine/Systems/Renderer/Runtime/FrameRendererImpl.h"

namespace Engine
{

	namespace
	{

		// Presentation push constants (Present.slang).
		struct PresentConstants
		{
			std::uint32_t DecodeSrgb = 0;
			std::uint32_t Reserved[3] = {};
		};

		// ScreenAntiAliasing.slang's push constants.
		struct ScreenAaConstants
		{
			std::uint32_t Size[2];
			float InvSize[2];
			float Threshold;
			float Strength;
			std::uint32_t Crisp;
			std::uint32_t Reserved;
		};

		static_assert(sizeof(ScreenAaConstants) == 32);

		// ScreenBlur.slang's push constants.
		struct ScreenBlurConstants
		{
			std::uint32_t OutputSize[2];
			float SourceInvSize[2];
			float Offset;
			float Mix;
			std::uint32_t Decode;
			std::uint32_t RegionCount;
			float Regions[4][4];
		};

		static_assert(sizeof(ScreenBlurConstants) == 96);

	} // namespace

	namespace FrameRendererDetail
	{

		// Draws source (the display-referred frame) into target, decoding sRGB for an sRGB target.
		void AddPresentPass(R::RenderGraph& graph, R::GraphTexture source, R::GraphTexture target, S::GraphicsPipeline& pipelineRef,
			S::PipelineLayout& layoutRef, S::Sampler& samplerRef, bool srgb, std::uint32_t width, std::uint32_t height)
		{
			auto* pipeline = &pipelineRef;
			auto* layout = &layoutRef;
			auto* sampler = &samplerRef;
			const PresentConstants constants{ srgb ? 1u : 0u, {} };
			graph.AddPass(
				"Present", S::QueueType::Graphics,
				[&](R::RenderGraphBuilder& b)
				{
					b.Read(source, ResourceState::ShaderRead);
					b.Write(target, ResourceState::ColorAttachment);
				},
				[source, target, pipeline, layout, sampler, constants, width, height](R::RenderCommandContext& c)
				{
					auto& sourceView = c.CreateView(source);
					auto& targetView = c.CreateView(target);
					auto table = c.CreateDescriptorTable({ layout, 0, 0, "Present source" });

					if (!table)
					{
						throw std::runtime_error("FrameRenderer: cannot create the present descriptor table");
					}

					std::array<S::DescriptorWrite, 2> writes{};
					writes[0].Binding = 0;
					writes[0].TextureResource = &sourceView;
					writes[1].Binding = 1;
					writes[1].SamplerResource = sampler;
					table->Write(writes);
					auto& retained = static_cast<S::DescriptorTable&>(c.Retain(std::move(table)));
					S::RenderingAttachmentDesc attachment{};
					attachment.View = &targetView;
					attachment.Load = S::LoadOp::Discard;
					auto& list = c.Commands();
					list.BeginRendering({ { &attachment, 1 }, nullptr, { width, height } });
					list.BindGraphicsPipeline(*pipeline);
					list.BindDescriptorTable(0, retained);
					list.SetViewport({ 0, 0, float(width), float(height) });
					list.SetScissor({ 0, 0, width, height });
					list.PushConstants(
						S::ShaderStageMask::Vertex | S::ShaderStageMask::Fragment, 0, std::as_bytes(std::span(&constants, 1)));
					list.Draw(3);
					list.EndRendering();
				});
		}

		bool IsSrgbFormat(S::Format format)
		{
			return format == S::Format::RGBA8UnormSrgb || format == S::Format::BGRA8UnormSrgb;
		}

		std::array<float, 3> Normalized(std::array<float, 3> v)
		{
			const float length = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);

			if (length <= 1e-12f || !std::isfinite(length))
			{
				return { 0, 0, -1 };
			}

			return { v[0] / length, v[1] / length, v[2] / length };
		}

		bool SameSky(const R::Environment::ProceduralSky& a, const R::Environment::ProceduralSky& b)
		{
			return a.ZenithColor == b.ZenithColor && a.HorizonColor == b.HorizonColor && a.GroundColor == b.GroundColor &&
				a.GroundFalloff == b.GroundFalloff && a.SunDirection == b.SunDirection && a.SunColor == b.SunColor &&
				a.SunSharpness == b.SunSharpness && a.Intensity == b.Intensity;
		}

		R::GraphTexture RecordScreenAntiAliasing(R::RenderGraph& graph, const RenderFeatureView& view, const RenderSettings& settings,
			R::GraphTexture color, R::GraphTexture depth, std::optional<R::GraphTexture> normal, RenderFeatureContext::Services services)
		{
			ScreenAaConstants constants{};
			constants.Size[0] = view.Width;
			constants.Size[1] = view.Height;
			constants.InvSize[0] = 1.0f / float(std::max(view.Width, 1u));
			constants.InvSize[1] = 1.0f / float(std::max(view.Height, 1u));
			constants.Strength = settings.Sharpening;
			constants.Crisp = settings.CrispAntiAliasing && normal ? 1u : 0u;
			// Without a normal target the gate is off; the colour stands in for the binding.
			const auto normals = normal ? *normal : color;
			RenderFeatureContext context(graph, RenderFeatureStage::AfterPostProcess, view, settings, color, color, std::move(services));
			const auto width = view.Width;
			const auto height = view.Height;

			if (settings.AntiAliasing == AntiAliasingMode::Fxaa)
			{
				constants.Threshold = 0.166f;
				const auto output = context.CreateColorTarget("FXAA");
				context.Compute("ScreenAaFxaa")
					.Texture("Color", color)
					.Texture("Depth", depth)
					.Texture("Normal", normals)
					.Sampler("LinearClamp")
					.Storage("Output", output)
					.Constants(constants)
					.Dispatch(width, height);
				color = output;
			}
			else if (settings.AntiAliasing == AntiAliasingMode::Smaa1x)
			{
				constants.Threshold = 0.15f;
				const auto edges = context.CreateTexture(S::Format::RGBA8Unorm, width, height, "AA edges");
				const auto weights = context.CreateTexture(S::Format::RGBA16Float, width, height, "AA weights");
				const auto output = context.CreateColorTarget("Morphological AA");
				context.Compute("ScreenAaEdges")
					.Texture("Color", color)
					.Texture("Depth", depth)
					.Texture("Normal", normals)
					.Storage("Edges", edges)
					.Constants(constants)
					.Dispatch(width, height);
				context.Compute("ScreenAaWeights")
					.Texture("Edges", edges)
					.Storage("Weights", weights)
					.Constants(constants)
					.Dispatch(width, height);
				context.Compute("ScreenAaBlend")
					.Texture("Color", color)
					.Texture("Weights", weights)
					.Storage("Output", output)
					.Constants(constants)
					.Dispatch(width, height);
				color = output;
			}

			if (settings.Sharpening > 0.0f)
			{
				const auto output = context.CreateColorTarget("Sharpen");
				context.Compute("ScreenSharpen")
					.Texture("Color", color)
					.Storage("Output", output)
					.Constants(constants)
					.Dispatch(width, height);
				color = output;
			}

			return color;
		}

		std::uint32_t ScreenBlurLevels(const RenderSettings& settings, std::uint32_t height, float* offset)
		{
			const float strength = settings.Blur.Strength;

			if (!(strength > 0.0f) || height == 0)
			{
				return 0;
			}

			// About 48 pixels of radius at full strength at 1080p. Each level halves the frame; the
			// tap offset grows within a level so the radius rises smoothly with the strength.
			const float radius = strength * 48.0f * float(height) / 1080.0f;
			const float steps = std::log2(1.0f + std::max(radius, 0.0f) * 0.5f);
			auto levels = static_cast<std::uint32_t>(std::clamp(std::ceil(steps), 1.0f, 6.0f));

			while (levels > 1 && (height >> levels) < 2)
			{
				--levels;
			}

			if (offset)
			{
				*offset = 0.5f + 0.75f * std::clamp(steps - float(levels - 1), 0.0f, 1.0f);
			}

			return levels;
		}

		R::GraphTexture RecordScreenBlur(R::RenderGraph& graph, const RenderFeatureView& view, const RenderSettings& settings,
			R::GraphTexture color, RenderFeatureContext::Services services)
		{
			float offset = 1.0f;
			const auto levels = ScreenBlurLevels(settings, view.Height, &offset);

			if (levels == 0 || view.Width == 0)
			{
				return color;
			}

			RenderFeatureContext context(graph, RenderFeatureStage::AfterPostProcess, view, settings, color, color, std::move(services));
			const auto levelSize = [&](std::uint32_t level)
			{
				return std::array<std::uint32_t, 2>{ std::max(view.Width >> level, 1u), std::max(view.Height >> level, 1u) };
			};
			ScreenBlurConstants constants{};
			constants.Offset = offset;
			std::vector<R::GraphTexture> down;
			down.reserve(levels);
			R::GraphTexture source = color;
			auto sourceSize = levelSize(0);

			for (std::uint32_t level = 1; level <= levels; ++level)
			{
				const auto size = levelSize(level);
				const auto output = context.CreateTexture(S::Format::RGBA16Float, size[0], size[1], "Blur down");
				constants.OutputSize[0] = size[0];
				constants.OutputSize[1] = size[1];
				constants.SourceInvSize[0] = 1.0f / float(sourceSize[0]);
				constants.SourceInvSize[1] = 1.0f / float(sourceSize[1]);
				constants.Decode = level == 1 ? 1u : 0u;
				context.Compute("ScreenBlurDown")
					.Texture("Source", source)
					.Sampler("LinearClamp")
					.Storage("Output", output)
					.Constants(constants)
					.Dispatch(size[0], size[1]);
				down.push_back(output);
				source = output;
				sourceSize = size;
			}

			constants.Decode = 0;

			for (std::uint32_t level = levels - 1; level >= 1; --level)
			{
				const auto size = levelSize(level);
				const auto output = context.CreateTexture(S::Format::RGBA16Float, size[0], size[1], "Blur up");
				constants.OutputSize[0] = size[0];
				constants.OutputSize[1] = size[1];
				constants.SourceInvSize[0] = 1.0f / float(sourceSize[0]);
				constants.SourceInvSize[1] = 1.0f / float(sourceSize[1]);
				context.Compute("ScreenBlurUp")
					.Texture("Source", source)
					.Sampler("LinearClamp")
					.Storage("Output", output)
					.Constants(constants)
					.Dispatch(size[0], size[1]);
				source = output;
				sourceSize = size;
			}

			const auto output = context.CreateColorTarget("Blur");
			constants.OutputSize[0] = view.Width;
			constants.OutputSize[1] = view.Height;
			constants.SourceInvSize[0] = 1.0f / float(sourceSize[0]);
			constants.SourceInvSize[1] = 1.0f / float(sourceSize[1]);
			// Small strengths fade the blur in instead of jumping to the first level's radius.
			constants.Mix = std::clamp(settings.Blur.Strength * 20.0f, 0.0f, 1.0f);
			constants.RegionCount = std::min<std::uint32_t>(settings.Blur.RegionCount, 4u);

			for (std::uint32_t r = 0; r < constants.RegionCount; ++r)
			{
				for (std::uint32_t e = 0; e < 4; ++e)
				{
					const float edge = settings.Blur.Regions[r][e];
					constants.Regions[r][e] = std::isfinite(edge) ? edge : 0.0f;
				}
			}

			context.Compute("ScreenBlurComposite")
				.Texture("Source", color)
				.Sampler("LinearClamp")
				.Texture("Blurred", source)
				.Storage("Output", output)
				.Constants(constants)
				.Dispatch(view.Width, view.Height);
			return output;
		}

		R::GraphTexture CreateTarget(R::RenderGraph& graph, std::uint32_t width, std::uint32_t height, const char* prefix, S::Format format,
			S::TextureUsage usage, const char* name)
		{
			S::TextureDesc textureDesc;
			textureDesc.Extent = { width, height, 1 };
			textureDesc.PixelFormat = format;
			textureDesc.Usage = usage;
			const std::string debugName = std::string(prefix) + name;
			textureDesc.DebugName = debugName;
			return graph.CreateTexture(textureDesc);
		}

		R::ForwardPlusTargets CreateForwardTargets(
			R::RenderGraph& graph, std::uint32_t targetWidth, std::uint32_t targetHeight, const char* prefix)
		{
			const auto attachmentSampled = S::TextureUsage::ColorAttachment | S::TextureUsage::Sampled;
			R::ForwardPlusTargets set;
			// Storage: the deferred local lights add to it in a compute pass.
			set.Color = CreateTarget(graph, targetWidth, targetHeight, prefix, R::ForwardPlusRenderer::ColorFormat,
				attachmentSampled | S::TextureUsage::TransferSource | S::TextureUsage::Storage, "Scene color");
			set.ObjectId = CreateTarget(graph, targetWidth, targetHeight, prefix, R::ForwardPlusRenderer::ObjectIdFormat,
				attachmentSampled | S::TextureUsage::TransferSource, "Object id");
			set.Depth = CreateTarget(graph, targetWidth, targetHeight, prefix, R::CanonicalDepthFormat,
				S::TextureUsage::DepthStencilAttachment | S::TextureUsage::Sampled, "Scene depth");
			set.Velocity = CreateTarget(
				graph, targetWidth, targetHeight, prefix, R::ForwardPlusRenderer::VelocityFormat, attachmentSampled, "Velocity");
			set.Normal =
				CreateTarget(graph, targetWidth, targetHeight, prefix, R::ForwardPlusRenderer::NormalFormat, attachmentSampled, "Normal");
			set.Indirect = CreateTarget(
				graph, targetWidth, targetHeight, prefix, R::ForwardPlusRenderer::IndirectFormat, attachmentSampled, "Indirect");
			set.Reflectance = CreateTarget(
				graph, targetWidth, targetHeight, prefix, R::ForwardPlusRenderer::ReflectanceFormat, attachmentSampled, "Reflectance");
			set.Specular = CreateTarget(
				graph, targetWidth, targetHeight, prefix, R::ForwardPlusRenderer::SpecularFormat, attachmentSampled, "Specular");
			set.Clear = false;		   // The sky pass clears colour and depth,
			set.ClearAuxiliary = true; // the opaque pass every other target.
			return set;
		}

		R::ForwardPlusTargets CreateMsaaTargets(
			R::RenderGraph& graph, std::uint32_t targetWidth, std::uint32_t targetHeight, S::SampleCount samples)
		{
			// Sampled: the resolve loads every sample.
			const auto create = [&](S::Format format, S::TextureUsage usage, const char* name)
			{
				S::TextureDesc textureDesc;
				textureDesc.Extent = { targetWidth, targetHeight, 1 };
				textureDesc.PixelFormat = format;
				textureDesc.Usage = usage | S::TextureUsage::Sampled;
				textureDesc.Samples = samples;
				textureDesc.DebugName = name;
				return graph.CreateTexture(textureDesc);
			};
			const auto attachment = S::TextureUsage::ColorAttachment;
			R::ForwardPlusTargets set;
			set.Samples = samples;
			set.Color = create(R::ForwardPlusRenderer::ColorFormat, attachment, "MSAA scene color");
			set.ObjectId = create(R::ForwardPlusRenderer::ObjectIdFormat, attachment, "MSAA object id");
			set.Depth = create(R::CanonicalDepthFormat, S::TextureUsage::DepthStencilAttachment, "MSAA scene depth");
			set.Velocity = create(R::ForwardPlusRenderer::VelocityFormat, attachment, "MSAA velocity");
			set.Normal = create(R::ForwardPlusRenderer::NormalFormat, attachment, "MSAA normal");
			set.Indirect = create(R::ForwardPlusRenderer::IndirectFormat, attachment, "MSAA indirect");
			set.Reflectance = create(R::ForwardPlusRenderer::ReflectanceFormat, attachment, "MSAA reflectance");
			set.Specular = create(R::ForwardPlusRenderer::SpecularFormat, attachment, "MSAA specular");
			set.Clear = false;
			set.ClearAuxiliary = true;
			return set;
		}

	} // namespace FrameRendererDetail

} // namespace Engine
