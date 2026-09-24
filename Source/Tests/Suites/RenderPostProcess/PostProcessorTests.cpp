#include "Engine/Systems/Renderer/PostProcess/PostProcessReference.h"
#include "Engine/Systems/Renderer/PostProcess/PostProcessor.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraphExecutor.h"
#include "Tests/Fixtures/RhiFrameCapture.h"
#include "Tests/Framework/Test.h"

#include <cstring>
#include <memory>
#include <stdexcept>

using namespace Swim;
using namespace Swim::Render;

namespace
{
	// Post-processing on the mock device: imported source and output textures and the
	// six programs with their reflected-style interfaces.
	struct PostWorld
	{
		static constexpr std::uint32_t Width = 64;
		static constexpr std::uint32_t Height = 32;

		PostWorld()
		{
			device.CreateTextures = true;
			executor = std::make_unique<RenderGraphExecutor>(device);
			using T = Rhi::DescriptorType;
			const auto schema = [](Testing::MockPipelineLayout& layout, std::initializer_list<std::pair<std::uint32_t, T>> bindings)
			{
				Rhi::DescriptorSchemaDesc space{ 0, {} };
				for (const auto& [binding, type] : bindings)
				{
					space.Bindings.push_back({ binding, type, 1, Rhi::ShaderStageMask::Compute });
				}
				layout.program.Interface.DescriptorSchemas = { space };
			};
			schema(histogramLayout,
				{ { PostHistogramBindings::Source, T::SampledTexture }, { PostHistogramBindings::Histogram, T::StorageBuffer } });
			schema(exposureLayout,
				{ { PostExposureBindings::Histogram, T::ReadOnlyStorageBuffer }, { PostExposureBindings::State, T::StorageBuffer } });
			schema(downLayout,
				{ { PostBloomDownsampleBindings::Source, T::SampledTexture },
					{ PostBloomDownsampleBindings::Destination, T::StorageTexture },
					{ PostBloomDownsampleBindings::State, T::ReadOnlyStorageBuffer } });
			schema(upLayout,
				{ { PostBloomUpsampleBindings::Low, T::SampledTexture }, { PostBloomUpsampleBindings::High, T::SampledTexture },
					{ PostBloomUpsampleBindings::Destination, T::StorageTexture } });
			for (auto* layout : { &compositeLayout, &compositeHdrLayout })
			{
				schema(*layout,
					{ { PostCompositeBindings::Source, T::SampledTexture }, { PostCompositeBindings::Bloom, T::SampledTexture },
						{ PostCompositeBindings::State, T::ReadOnlyStorageBuffer },
						{ PostCompositeBindings::Params, T::ReadOnlyStorageBuffer },
						{ PostCompositeBindings::Output, T::StorageTexture } });
			}
			Rhi::TextureDesc sourceDesc;
			sourceDesc.Extent = { Width, Height, 1 };
			sourceDesc.PixelFormat = Rhi::Format::RGBA16Float;
			sourceDesc.Usage = Rhi::TextureUsage::Sampled | Rhi::TextureUsage::ColorAttachment;
			source = device.CreateTexture(sourceDesc);
			Rhi::TextureDesc outputDesc = sourceDesc;
			outputDesc.PixelFormat = Rhi::Format::RGBA8Unorm;
			outputDesc.Usage = Rhi::TextureUsage::Storage | Rhi::TextureUsage::TransferSource;
			sdrOutput = device.CreateTexture(outputDesc);
			outputDesc.PixelFormat = Rhi::Format::RGBA16Float;
			hdrOutput = device.CreateTexture(outputDesc);
		}

		PostProcessorDesc Desc()
		{
			PostProcessorDesc desc;
			desc.Histogram = { &histogramPipeline, &histogramLayout, 0 };
			desc.Exposure = { &exposurePipeline, &exposureLayout, 0 };
			desc.BloomDownsample = { &downPipeline, &downLayout, 0 };
			desc.BloomUpsample = { &upPipeline, &upLayout, 0 };
			desc.Composite = { &compositePipeline, &compositeLayout, 0 };
			desc.CompositeHdr = { &compositeHdrPipeline, &compositeHdrLayout, 0 };
			desc.DebugName = "Test post";
			return desc;
		}

		PostProcessFrame Frame(RenderGraph& graph, const PostProcessSettings& settings, bool hdr = false)
		{
			PostProcessFrame frame;
			frame.Source = graph.ImportTexture(*source, Rhi::ResourceState::ShaderRead);
			frame.Output = graph.ImportTexture(hdr ? *hdrOutput : *sdrOutput, Rhi::ResourceState::Undefined);
			graph.Export(frame.Output, Rhi::ResourceState::CopySource);
			frame.Settings = settings;
			frame.DeltaTime = 1.0f / 60.0f;
			return frame;
		}

		void Run(RenderGraph& graph)
		{
			device.Commands->clear();
			executor->Execute(graph.Compile());
			executor->Wait();
		}

		std::vector<Testing::MockCommand> Commands(const std::string& kind) const
		{
			std::vector<Testing::MockCommand> result;
			for (const auto& command : *device.Commands)
			{
				if (command.Kind == kind)
				{
					result.push_back(command);
				}
			}
			return result;
		}

		template <typename T> std::vector<T> Pushes() const
		{
			std::vector<T> result;
			for (const auto& push : Commands("PushConstants"))
			{
				if (push.Data.size() == sizeof(T))
				{
					T value{};
					std::memcpy(&value, push.Data.data(), sizeof(T));
					result.push_back(value);
				}
			}
			return result;
		}

		Testing::MockDevice device;
		std::unique_ptr<RenderGraphExecutor> executor;
		Testing::MockPipelineLayout histogramLayout, exposureLayout, downLayout, upLayout, compositeLayout, compositeHdrLayout;
		Testing::MockComputePipeline histogramPipeline, exposurePipeline, downPipeline, upPipeline, compositePipeline, compositeHdrPipeline;
		std::unique_ptr<Rhi::Texture> source, sdrOutput, hdrOutput;
	};
} // namespace

SWIM_TEST("Render.PostProcessor", "RecordsHistogramExposureBloomChainsAndComposite")
{
	PostWorld world;
	PostProcessor post(world.device, world.Desc());
	SWIM_CHECK_EQUAL(post.GetExposureStateBuffer().GetDesc().Size, std::uint64_t(sizeof(GpuExposureState)));
	PostProcessSettings settings; // Automatic exposure, bloom with 6 requested levels.
	RenderGraph graph;
	const auto frame = world.Frame(graph, settings);
	const auto resources = post.Record(graph, frame);
	SWIM_CHECK(resources.ExposureReset);
	SWIM_CHECK(resources.Histogram.has_value());
	SWIM_CHECK_EQUAL(resources.BloomLevels, 5u); // 32 -> 16, 8, 4, 2, 1.
	SWIM_REQUIRE_EQUAL(resources.BloomDown.size(), std::size_t(5));
	SWIM_REQUIRE_EQUAL(resources.BloomUp.size(), std::size_t(4));
	for (std::uint32_t i = 0; i < 5; ++i)
	{
		const auto& level = graph.GetDesc(resources.BloomDown[i]);
		SWIM_CHECK(level.Extent.Width == (64u >> (i + 1)) && level.Extent.Height == (32u >> (i + 1)));
		SWIM_CHECK(level.PixelFormat == Rhi::Format::RGBA16Float);
	}
	SWIM_CHECK_EQUAL(graph.GetDesc(resources.BloomUp[0]).Extent.Width, 32u);
	SWIM_CHECK_EQUAL(graph.GetDesc(*resources.Histogram).Size, std::uint64_t(1024));
	SWIM_CHECK_EQUAL(resources.ParamsRecord.BloomEnabled, 1u);
	SWIM_CHECK_EQUAL(graph.GetDesc(resources.Params).Size, std::uint64_t(sizeof(GpuPostParams)));
	world.Run(graph);

	// Histogram, exposure, 5 downsamples, 4 upsamples, composite.
	const auto dispatches = world.Commands("Dispatch");
	SWIM_REQUIRE_EQUAL(dispatches.size(), std::size_t(12));
	SWIM_CHECK(dispatches[0].SourceOffset == 4 && dispatches[0].DestinationOffset == 2); // 64x32 / 16x16.
	SWIM_CHECK(dispatches[1].SourceOffset == 1 && dispatches[1].DestinationOffset == 1);
	SWIM_CHECK(dispatches[2].SourceOffset == 4 && dispatches[2].DestinationOffset == 2);   // 32x16 / 8x8.
	SWIM_CHECK(dispatches[11].SourceOffset == 8 && dispatches[11].DestinationOffset == 4); // 64x32 / 8x8.
	const auto pipelines = world.Commands("BindComputePipeline");
	SWIM_REQUIRE_EQUAL(pipelines.size(), std::size_t(12));
	SWIM_CHECK(pipelines[0].Source == &world.histogramPipeline && pipelines[1].Source == &world.exposurePipeline);
	SWIM_CHECK(pipelines[2].Source == &world.downPipeline && pipelines[6].Source == &world.downPipeline);
	SWIM_CHECK(pipelines[7].Source == &world.upPipeline && pipelines[10].Source == &world.upPipeline);
	SWIM_CHECK(pipelines[11].Source == &world.compositePipeline);
	SWIM_CHECK_EQUAL(world.Commands("CopyBuffer").size() >= 1u, true); // The histogram clear.

	const auto exposure = world.Pushes<PostExposureConstants>();
	SWIM_REQUIRE_EQUAL(exposure.size(), std::size_t(1));
	SWIM_CHECK_EQUAL(exposure[0].Mode, 1u);
	SWIM_CHECK_EQUAL(exposure[0].Reset, 1u);
	SWIM_CHECK_EQUAL(exposure[0].Log2Range, 24.0f);
	SWIM_CHECK(std::abs(exposure[0].DeltaTime - 1.0f / 60.0f) < 1.0e-9f);
	const auto downs = world.Pushes<PostBloomDownsampleConstants>();
	SWIM_REQUIRE_EQUAL(downs.size(), std::size_t(5));
	SWIM_CHECK(downs[0].First == 1u && downs[0].SourceWidth == 64u && downs[0].DestinationWidth == 32u);
	SWIM_CHECK(downs[1].First == 0u && downs[1].SourceWidth == 32u && downs[1].DestinationWidth == 16u);
	SWIM_CHECK(downs[0].Threshold == settings.Bloom.Threshold && downs[0].Knee == settings.Bloom.Knee);
	// 16-byte pushes: histogram, upsamples (from the smallest level up), composite.
	std::vector<std::array<std::uint32_t, 4>> small;
	for (const auto& push : world.Commands("PushConstants"))
	{
		if (push.Data.size() == 16)
		{
			std::array<std::uint32_t, 4> v{};
			std::memcpy(v.data(), push.Data.data(), 16);
			small.push_back(v);
		}
	}
	SWIM_REQUIRE_EQUAL(small.size(), std::size_t(6));
	SWIM_CHECK((small[1] == std::array<std::uint32_t, 4>{ 2, 1, 4, 2 }));	  // Level 3 (4x2) <- level 4 (2x1).
	SWIM_CHECK((small[4] == std::array<std::uint32_t, 4>{ 16, 8, 32, 16 }));  // Level 0 <- level 1.
	SWIM_CHECK((small[5] == std::array<std::uint32_t, 4>{ 64, 32, 32, 16 })); // Composite + half-size bloom.

	// The composite table: the state buffer, the params and the output view.
	const auto* table = world.device.LastDescriptorTable;
	SWIM_REQUIRE(table != nullptr);
	SWIM_CHECK(table->Element(PostCompositeBindings::State, 0) == &post.GetExposureStateBuffer());
	SWIM_CHECK(table->Element(PostCompositeBindings::Params, 0) != nullptr);
	const auto* outputView = static_cast<const Rhi::TextureView*>(table->Element(PostCompositeBindings::Output, 0));
	SWIM_REQUIRE(outputView != nullptr);
	SWIM_CHECK(&outputView->GetTexture() == world.sdrOutput.get());
	SWIM_CHECK(outputView->GetDesc().PixelFormat == Rhi::Format::RGBA8Unorm);
	SWIM_CHECK_EQUAL(table->ElementWrites, 5u);

	// The next frame adapts; a reset snaps again.
	RenderGraph second;
	SWIM_CHECK(!post.Record(second, world.Frame(second, settings)).ExposureReset);
	world.Run(second);
	const auto adapted = world.Pushes<PostExposureConstants>();
	SWIM_REQUIRE_EQUAL(adapted.size(), std::size_t(1));
	SWIM_CHECK_EQUAL(adapted[0].Reset, 0u);
	post.ResetExposureHistory();
	RenderGraph third;
	SWIM_CHECK(post.Record(third, world.Frame(third, settings)).ExposureReset);
}

SWIM_TEST("Render.PostProcessor", "ManualExposureAndDisabledBloomRecordOnlyWhatIsNeeded")
{
	PostWorld world;
	PostProcessor post(world.device, world.Desc());
	PostProcessSettings settings;
	settings.Exposure.Mode = ExposureMode::Manual;
	settings.Exposure.ManualEv100 = 7.0f;
	settings.Bloom.Enabled = false;
	RenderGraph graph;
	const auto resources = post.Record(graph, world.Frame(graph, settings));
	SWIM_CHECK(!resources.Histogram.has_value());
	SWIM_CHECK(resources.BloomDown.empty() && resources.BloomUp.empty());
	SWIM_CHECK_EQUAL(resources.BloomLevels, 0u);
	SWIM_CHECK_EQUAL(resources.ParamsRecord.BloomEnabled, 0u);
	world.Run(graph);
	const auto pipelines = world.Commands("BindComputePipeline");
	SWIM_REQUIRE_EQUAL(pipelines.size(), std::size_t(2));
	SWIM_CHECK(pipelines[0].Source == &world.exposurePipeline && pipelines[1].Source == &world.compositePipeline);
	const auto exposure = world.Pushes<PostExposureConstants>();
	SWIM_REQUIRE_EQUAL(exposure.size(), std::size_t(1));
	SWIM_CHECK(exposure[0].Mode == 0u && exposure[0].ManualEv100 == 7.0f);
	SWIM_CHECK_EQUAL(world.Commands("CopyBufferToTexture").size(), std::size_t(1)); // The 1x1 bloom stand-in.

	// One bloom level: the composite reads the first downsample directly.
	settings.Bloom.Enabled = true;
	settings.Bloom.MipCount = 1;
	RenderGraph single;
	const auto one = post.Record(single, world.Frame(single, settings));
	SWIM_CHECK(one.BloomDown.size() == 1 && one.BloomUp.empty());
	SWIM_CHECK(std::abs(one.ParamsRecord.PowerBloom[3] - settings.Bloom.Intensity) < 1.0e-9f);
	world.Run(single);
	SWIM_CHECK_EQUAL(world.Commands("Dispatch").size(), std::size_t(3));
}

SWIM_TEST("Render.PostProcessor", "HdrEncodingsUseTheHdrCompositeAndGradingReachesTheParams")
{
	PostWorld world;
	PostProcessor post(world.device, world.Desc());
	PostProcessSettings settings;
	settings.Output.Encoding = OutputEncoding::Hdr10;
	settings.Output.PeakNits = 600.0f;
	settings.Grading.Saturation = 1.2f;
	settings.ToneMap.Operator = ToneMapper::Aces;
	RenderGraph graph;
	const auto resources = post.Record(graph, world.Frame(graph, settings, true));
	const auto& p = resources.ParamsRecord;
	SWIM_CHECK(p.Encoding == 1u && p.PeakNits == 600.0f && p.ToneMapper == 2u && p.GradingEnabled == 1u && p.Dither == 0u);
	SWIM_CHECK(p.OffsetSaturation[3] == 1.2f);
	world.Run(graph);
	const auto pipelines = world.Commands("BindComputePipeline");
	SWIM_CHECK(pipelines.back().Source == &world.compositeHdrPipeline);
	const auto* outputView =
		static_cast<const Rhi::TextureView*>(world.device.LastDescriptorTable->Element(PostCompositeBindings::Output, 0));
	SWIM_REQUIRE(outputView != nullptr);
	SWIM_CHECK(outputView->GetDesc().PixelFormat == Rhi::Format::RGBA16Float);
}

SWIM_TEST("Render.PostProcessor", "RejectsMissingProgramsAndMismatchedFrames")
{
	PostWorld world;
	auto missing = world.Desc();
	missing.BloomUpsample = {};
	SWIM_CHECK_THROWS(PostProcessor(world.device, missing), std::invalid_argument);
	PostProcessor post(world.device, world.Desc());
	const auto attempt = [&](const auto& edit)
	{
		RenderGraph graph;
		auto frame = world.Frame(graph, PostProcessSettings{});
		edit(graph, frame);
		post.Record(graph, frame);
	};
	SWIM_CHECK_THROWS(attempt(
						  [](RenderGraph&, PostProcessFrame& f)
						  {
							  f.DeltaTime = -1.0f;
						  }),
		std::invalid_argument);
	SWIM_CHECK_THROWS(attempt(
						  [](RenderGraph&, PostProcessFrame& f)
						  {
							  f.DeltaTime = NAN;
						  }),
		std::invalid_argument);
	SWIM_CHECK_THROWS(attempt(
						  [](RenderGraph&, PostProcessFrame& f)
						  {
							  f.Settings.Bloom.Knee = 0.0f;
						  }),
		std::invalid_argument);
	// HDR encoding into the RGBA8 output.
	SWIM_CHECK_THROWS(attempt(
						  [](RenderGraph&, PostProcessFrame& f)
						  {
							  f.Settings.Output.Encoding = OutputEncoding::ScRgb;
						  }),
		std::invalid_argument);
	// Wrong source format, wrong output size, output without Storage.
	SWIM_CHECK_THROWS(attempt(
						  [](RenderGraph& g, PostProcessFrame& f)
						  {
							  f.Source = f.Output;
						  }),
		std::invalid_argument);
	SWIM_CHECK_THROWS(attempt(
						  [](RenderGraph& g, PostProcessFrame& f)
						  {
							  Rhi::TextureDesc small;
							  small.Extent = { 16, 16, 1 };
							  small.PixelFormat = Rhi::Format::RGBA8Unorm;
							  small.Usage = Rhi::TextureUsage::Storage;
							  f.Output = g.CreateTexture(small);
						  }),
		std::invalid_argument);
	SWIM_CHECK_THROWS(attempt(
						  [](RenderGraph& g, PostProcessFrame& f)
						  {
							  Rhi::TextureDesc sampled;
							  sampled.Extent = { PostWorld::Width, PostWorld::Height, 1 };
							  sampled.PixelFormat = Rhi::Format::RGBA8Unorm;
							  sampled.Usage = Rhi::TextureUsage::Sampled;
							  f.Output = g.CreateTexture(sampled);
						  }),
		std::invalid_argument);
}
