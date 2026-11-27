#include "Engine/Systems/Renderer/Runtime/FrameRendererImpl.h"
#include "Engine/Logging/Log.h"

// The cube map sky background (FrameRenderer::SetSkyCubemap, SkyBackground.slang with SKY_CUBEMAP=1).
namespace Engine
{

	using namespace FrameRendererDetail;

	void FrameRenderer::SetSkyCubemap(std::uint32_t size, const std::array<std::span<const std::byte>, 6>& faces)
	{
		const std::size_t faceBytes = std::size_t(size) * size * 4;

		if (size == 0)
		{
			throw std::invalid_argument("FrameRenderer::SetSkyCubemap: the faces need a size");
		}

		for (const auto& face : faces)
		{
			if (face.size() != faceBytes)
			{
				throw std::invalid_argument("FrameRenderer::SetSkyCubemap: every face must be size x size RGBA8");
			}
		}

		auto& I = *impl;
		I.skyCubeFaces.resize(faceBytes * 6);

		for (std::size_t i = 0; i < faces.size(); ++i)
		{
			std::memcpy(I.skyCubeFaces.data() + i * faceBytes, faces[i].data(), faceBytes);
		}

		if (I.skyCubeSize != size)
		{
			I.skyCube.reset(); // Released after the frames that may still sample it.
		}

		I.skyCubeSize = size;
		I.skyCubeUploaded = false;
	}

	void FrameRenderer::ClearSkyCubemap()
	{
		auto& I = *impl;
		I.skyCubeFaces.clear();
		I.skyCubeFaces.shrink_to_fit();
		I.skyCube.reset();
		I.skyCubeSize = 0;
		I.skyCubeUploaded = false;
	}

	bool FrameRenderer::HasSkyCubemap() const
	{
		return !impl->skyCubeFaces.empty();
	}

	R::GraphTexture FrameRenderer::Impl::ImportSkyCube(R::RenderGraph& graph)
	{
		if (skyCubeImport && skyCubeImportGraph == &graph && skyCubeImportFrame == frameIndex)
		{
			return *skyCubeImport;
		}

		if (!skyCube)
		{
			S::TextureDesc cubeDesc;
			cubeDesc.Dimension = S::TextureDimension::TextureCube;
			cubeDesc.Extent = { skyCubeSize, skyCubeSize, 1 };
			cubeDesc.PixelFormat = S::Format::RGBA8UnormSrgb;
			cubeDesc.Usage = S::TextureUsage::Sampled | S::TextureUsage::TransferDestination;
			cubeDesc.ArrayLayers = 6;
			cubeDesc.DebugName = "Sky cube";
			skyCube = device.CreateTexture(cubeDesc);

			if (!skyCube)
			{
				throw std::runtime_error("FrameRenderer: cannot create the sky cube");
			}

			skyCubeUploaded = false;
		}

		R::GraphTexture imported{};

		if (!skyCubeUploaded)
		{
			imported = graph.ImportTexture(*skyCube, ResourceState::Undefined);
			const std::size_t faceBytes = std::size_t(skyCubeSize) * skyCubeSize * 4;
			std::array<R::GraphBuffer, 6> staging{};

			for (std::size_t i = 0; i < staging.size(); ++i)
			{
				staging[i] = graph.CreateUpload(std::span<const std::byte>(skyCubeFaces.data() + i * faceBytes, faceBytes), "Sky cube face",
					S::BufferUsage::TransferSource, 512);
			}

			graph.AddPass(
				"Sky cube upload", S::QueueType::Graphics,
				[&](R::RenderGraphBuilder& b)
				{
					for (const auto buffer : staging)
					{
						b.Read(buffer, ResourceState::CopySource);
					}

					b.Write(imported, ResourceState::CopyDestination);
				},
				[imported, staging, size = skyCubeSize](R::RenderCommandContext& c)
				{
					auto& target = c.Get(imported);

					for (std::uint32_t layer = 0; layer < 6; ++layer)
					{
						const auto source = c.GetRange(staging[layer]);
						c.Commands().CopyBufferToTexture(*source.Buffer, target, { source.Offset, { 0, layer }, {}, { size, size, 1 } });
					}
				});
			skyCubeUploaded = true;
			graph.OnAbandoned(
				[this]
				{
					skyCubeUploaded = false;
				});
		}
		else
		{
			imported = graph.ImportTexture(*skyCube, ResourceState::ShaderRead);
		}

		graph.Export(imported, ResourceState::ShaderRead);
		skyCubeImport = imported;
		skyCubeImportGraph = &graph;
		skyCubeImportFrame = frameIndex;
		return imported;
	}

	S::GraphicsPipeline* FrameRenderer::Impl::SkyCubePipeline(S::SampleCount samples)
	{
		if (skyCubeFailed)
		{
			return nullptr;
		}

		auto& slot = samples == S::SampleCount::X1 ? skyCubePipeline : msaaSkyCube[static_cast<std::uint32_t>(samples)];

		if (slot)
		{
			return slot.get();
		}

		try
		{
			if (!skyCubeProgram.Program)
			{
				if (!shaders.Contains("SkyCubemap"))
				{
					throw std::runtime_error("the SkyCubemap shader is missing");
				}

				skyCubeProgram = LoadDrawProgram("SkyCubemap", nullptr);
			}

			static const std::array<S::Format, 1> formats{ R::ForwardPlusRenderer::ColorFormat };
			S::GraphicsPipelineDesc skyDesc{};
			skyDesc.Program = skyCubeProgram.Program.get();
			skyDesc.Layout = skyCubeProgram.Layout.get();
			skyDesc.ColorFormats = formats;
			skyDesc.DepthStencilFormat = R::CanonicalDepthFormat;
			skyDesc.DepthStencil.DepthTest = false;
			skyDesc.DepthStencil.DepthWrite = false;
			skyDesc.Raster.Cull = S::CullMode::None;
			skyDesc.Samples = samples;
			skyDesc.DebugName = "Sky cube map";
			slot = Require(device.CreateGraphicsPipeline(skyDesc), "sky cube map");
			return slot.get();
		}
		catch (const std::exception& error)
		{
			skyCubeFailed = true;
			Logging::Write(Logging::Level::Warning,
				std::string("[Render] The cube map sky is unavailable, drawing the procedural sky: ") + error.what());
			return nullptr;
		}
	}

} // namespace Engine
