#include "Engine/Systems/Renderer/RenderGraph/RenderGraphExecutor.h"
#include "Engine/Systems/Renderer/Temporal/TemporalAntiAliasing.h"
#include "Engine/Systems/Renderer/Temporal/TemporalReference.h"
#include "Tests/Fixtures/RhiFrameCapture.h"
#include "Tests/Framework/Test.h"

#include <cstring>
#include <functional>
#include <memory>
#include <stdexcept>

using namespace Swim;
using namespace Swim::Render;

namespace
{
	// TAA on the mock device: imported color, depth and velocity inputs and the
	// resolve program with its reflected-style interface.
	struct TemporalWorld
	{
		static constexpr std::uint32_t Width = 64;
		static constexpr std::uint32_t Height = 36;

		TemporalWorld()
		{
			device.CreateTextures = true;
			executor = std::make_unique<RenderGraphExecutor>(device);
			using T = Rhi::DescriptorType;
			using B = TemporalResolveBindings;
			Rhi::DescriptorSchemaDesc space{ 0, {} };
			for (const auto& [binding, type] : { std::pair{ B::Current, T::SampledTexture }, std::pair{ B::Depth, T::SampledTexture },
					 std::pair{ B::Velocity, T::SampledTexture }, std::pair{ B::History, T::SampledTexture },
					 std::pair{ B::Output, T::StorageTexture } })
			{
				space.Bindings.push_back({ binding, type, 1, Rhi::ShaderStageMask::Compute });
			}
			layout.program.Interface.DescriptorSchemas = { space };
			Resize(Width, Height, Rhi::Format::D32Float);
		}

		void Resize(std::uint32_t width, std::uint32_t height, Rhi::Format depthFormat)
		{
			const auto make = [&](Rhi::Format format, Rhi::TextureUsage usage)
			{
				Rhi::TextureDesc desc;
				desc.Extent = { width, height, 1 };
				desc.PixelFormat = format;
				desc.Usage = usage | Rhi::TextureUsage::Sampled;
				return device.CreateTexture(desc);
			};
			color = make(Rhi::Format::RGBA16Float, Rhi::TextureUsage::ColorAttachment);
			depth = make(depthFormat,
				depthFormat == Rhi::Format::D32Float ? Rhi::TextureUsage::DepthStencilAttachment : Rhi::TextureUsage::ColorAttachment);
			velocity = make(Rhi::Format::RG16Float, Rhi::TextureUsage::ColorAttachment);
		}

		TemporalAntiAliasingDesc Desc()
		{
			TemporalAntiAliasingDesc desc;
			desc.Resolve = { &pipeline, &layout, 0 };
			desc.DebugName = "Test TAA";
			return desc;
		}

		TemporalFrame Frame(RenderGraph& graph, const TemporalSettings& settings = {})
		{
			TemporalFrame frame;
			frame.Color = graph.ImportTexture(*color, Rhi::ResourceState::ShaderRead);
			frame.Depth = graph.ImportTexture(*depth, Rhi::ResourceState::ShaderRead);
			frame.Velocity = graph.ImportTexture(*velocity, Rhi::ResourceState::ShaderRead);
			frame.Settings = settings;
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

		TemporalResolveConstants Constants() const
		{
			const auto pushes = Commands("PushConstants");
			SWIM_REQUIRE_EQUAL(pushes.size(), std::size_t(1));
			SWIM_REQUIRE_EQUAL(pushes[0].Data.size(), sizeof(TemporalResolveConstants));
			TemporalResolveConstants constants;
			std::memcpy(&constants, pushes[0].Data.data(), sizeof(constants));
			return constants;
		}

		const Rhi::TextureView& Bound(std::uint32_t binding) const
		{
			SWIM_REQUIRE(device.LastDescriptorTable != nullptr);
			const auto* element = device.LastDescriptorTable->Element(binding, 0);
			SWIM_REQUIRE(element != nullptr);
			return *static_cast<const Rhi::TextureView*>(element);
		}

		Testing::MockDevice device;
		std::unique_ptr<RenderGraphExecutor> executor;
		Testing::MockPipelineLayout layout;
		Testing::MockComputePipeline pipeline;
		std::unique_ptr<Rhi::Texture> color, depth, velocity;
	};
} // namespace

SWIM_TEST("Render.TemporalAntiAliasing", "ResolvesIntoPingPongHistoryAndAdvancesTheJitter")
{
	using B = TemporalResolveBindings;
	TemporalWorld world;
	TemporalAntiAliasing taa(world.device, world.Desc());
	TemporalSettings settings;
	settings.Feedback = 0.125f;
	settings.ClipGamma = 1.5f;
	SWIM_CHECK(!taa.HasHistory());
	SWIM_CHECK((taa.GetJitterPixels(settings) == Temporal::JitterPixels(0, 8)));
	SWIM_CHECK((taa.GetJitterNdc(settings, TemporalWorld::Width, TemporalWorld::Height) ==
		Temporal::JitterNdc(0, 8, TemporalWorld::Width, TemporalWorld::Height)));

	// Frame 0: no history. The color stands in as history, and the push says so.
	const Rhi::Texture* firstOutput = nullptr;
	{
		RenderGraph graph;
		const auto frame = world.Frame(graph, settings);
		const auto resources = taa.Record(graph, frame);
		SWIM_CHECK(!resources.HistoryValid);
		SWIM_CHECK(resources.History == frame.Color);
		SWIM_CHECK_EQUAL(resources.FrameIndex, std::uint64_t(0));
		SWIM_CHECK((resources.JitterPixels == Temporal::JitterPixels(0, 8)));
		const auto& outputDesc = graph.GetDesc(resources.Output);
		SWIM_CHECK(outputDesc.PixelFormat == Rhi::Format::RGBA16Float);
		SWIM_CHECK_EQUAL(outputDesc.Extent.Width, TemporalWorld::Width);
		world.Run(graph);
		const auto dispatches = world.Commands("Dispatch");
		SWIM_REQUIRE_EQUAL(dispatches.size(), std::size_t(1));
		SWIM_CHECK_EQUAL(dispatches[0].SourceOffset, 8u);	   // 64 / 8
		SWIM_CHECK_EQUAL(dispatches[0].DestinationOffset, 5u); // ceil(36 / 8)
		SWIM_CHECK(world.Commands("BindComputePipeline").at(0).Source == &world.pipeline);
		const auto constants = world.Constants();
		SWIM_CHECK_EQUAL(constants.Width, TemporalWorld::Width);
		SWIM_CHECK_EQUAL(constants.Height, TemporalWorld::Height);
		SWIM_CHECK(constants.Feedback == 0.125f && constants.ClipGamma == 1.5f);
		SWIM_CHECK_EQUAL(constants.HistoryValid, 0u);
		SWIM_CHECK(&world.Bound(B::Current).GetTexture() == world.color.get());
		SWIM_CHECK(&world.Bound(B::History).GetTexture() == world.color.get());
		SWIM_CHECK(&world.Bound(B::Velocity).GetTexture() == world.velocity.get());
		SWIM_CHECK(world.Bound(B::Velocity).GetDesc().PixelFormat == Rhi::Format::RG16Float);
		const auto& depthView = world.Bound(B::Depth);
		SWIM_CHECK(&depthView.GetTexture() == world.depth.get());
		SWIM_CHECK(depthView.GetDesc().PixelFormat == Rhi::Format::D32Float && depthView.GetDesc().Aspect == Rhi::TextureAspect::Depth);
		firstOutput = &world.Bound(B::Output).GetTexture();
		SWIM_CHECK(firstOutput != world.color.get());
	}
	SWIM_CHECK(taa.HasHistory());
	SWIM_CHECK_EQUAL(taa.GetFrameIndex(), std::uint64_t(1));
	SWIM_CHECK((taa.GetJitterPixels(settings) == Temporal::JitterPixels(1, 8)));

	// Frames 1 and 2: last frame's output is the history, and the two textures alternate.
	const Rhi::Texture* secondOutput = nullptr;
	for (int i = 0; i < 2; ++i)
	{
		RenderGraph graph;
		const auto resources = taa.Record(graph, world.Frame(graph, settings));
		SWIM_CHECK(resources.HistoryValid);
		world.Run(graph);
		SWIM_CHECK_EQUAL(world.Constants().HistoryValid, 1u);
		const auto* history = &world.Bound(B::History).GetTexture();
		const auto* output = &world.Bound(B::Output).GetTexture();
		SWIM_CHECK(history != output);
		if (i == 0)
		{
			SWIM_CHECK(history == firstOutput);
			secondOutput = output;
		}
		else
		{
			SWIM_CHECK(history == secondOutput);
			SWIM_CHECK(output == firstOutput);
		}
	}

	// A reset restarts from the current frame without touching the jitter sequence.
	taa.ResetHistory();
	{
		RenderGraph graph;
		const auto frame = world.Frame(graph, settings);
		const auto resources = taa.Record(graph, frame);
		SWIM_CHECK(!resources.HistoryValid && resources.History == frame.Color);
		SWIM_CHECK_EQUAL(resources.FrameIndex, std::uint64_t(3));
		world.Run(graph);
		SWIM_CHECK_EQUAL(world.Constants().HistoryValid, 0u);
	}

	// A new size recreates the history (the old pair is released by that graph) and
	// resets it; an R32Float depth is bound without the depth aspect.
	world.Resize(40, 20, Rhi::Format::R32Float);
	{
		RenderGraph graph;
		const auto resources = taa.Record(graph, world.Frame(graph, settings));
		SWIM_CHECK(!resources.HistoryValid);
		SWIM_CHECK_EQUAL(graph.GetDesc(resources.Output).Extent.Width, 40u);
		world.Run(graph);
		SWIM_CHECK_EQUAL(world.Constants().Width, 40u);
		SWIM_CHECK_EQUAL(world.Bound(B::Output).GetTexture().GetDesc().Extent.Height, 20u);
		const auto& depthView = world.Bound(B::Depth);
		SWIM_CHECK(depthView.GetDesc().PixelFormat == Rhi::Format::R32Float);
		SWIM_CHECK(depthView.GetDesc().Aspect == Rhi::TextureAspect::Automatic);
		const auto dispatches = world.Commands("Dispatch");
		SWIM_CHECK_EQUAL(dispatches.at(0).SourceOffset, 5u);
		SWIM_CHECK_EQUAL(dispatches.at(0).DestinationOffset, 3u);
	}
	{
		RenderGraph graph;
		SWIM_CHECK(taa.Record(graph, world.Frame(graph, settings)).HistoryValid);
		world.Run(graph);
	}
	SWIM_CHECK_EQUAL(TemporalAntiAliasing::HistoryDesc(8, 4).Extent.Width, 8u);
	SWIM_CHECK(TemporalAntiAliasing::HistoryDesc(8, 4).PixelFormat == Rhi::Format::RGBA16Float);
}

SWIM_TEST("Render.TemporalAntiAliasing", "RejectsInvalidProgramsSettingsAndInputs")
{
	TemporalWorld world;
	SWIM_CHECK_THROWS(TemporalAntiAliasing(world.device, TemporalAntiAliasingDesc{}), std::invalid_argument);
	TemporalAntiAliasing taa(world.device, world.Desc());

	const auto attempt = [&](const std::function<void(RenderGraph&, TemporalFrame&)>& edit)
	{
		RenderGraph graph;
		auto frame = world.Frame(graph);
		edit(graph, frame);
		taa.Record(graph, frame);
	};
	const auto texture = [](RenderGraph& graph, Rhi::Format format, Rhi::TextureUsage usage, std::uint32_t width = TemporalWorld::Width)
	{
		Rhi::TextureDesc desc;
		desc.Extent = { width, TemporalWorld::Height, 1 };
		desc.PixelFormat = format;
		desc.Usage = usage;
		return graph.CreateTexture(desc);
	};
	using U = Rhi::TextureUsage;
	SWIM_CHECK_THROWS(attempt(
						  [](RenderGraph&, TemporalFrame& frame)
						  {
							  frame.Settings.Feedback = 0.0f;
						  }),
		std::invalid_argument);
	SWIM_CHECK_THROWS(attempt(
						  [](RenderGraph&, TemporalFrame& frame)
						  {
							  frame.Settings.JitterPhases = 100;
						  }),
		std::invalid_argument);
	SWIM_CHECK_THROWS(attempt(
						  [&](RenderGraph& graph, TemporalFrame& frame)
						  {
							  frame.Color = texture(graph, Rhi::Format::RGBA8Unorm, U::Sampled);
						  }),
		std::invalid_argument);
	SWIM_CHECK_THROWS(attempt(
						  [&](RenderGraph& graph, TemporalFrame& frame)
						  {
							  frame.Color = texture(graph, Rhi::Format::RGBA16Float, U::Storage);
						  }), // Not sampled.
		std::invalid_argument);
	SWIM_CHECK_THROWS(attempt(
						  [&](RenderGraph& graph, TemporalFrame& frame)
						  {
							  frame.Depth = texture(graph, Rhi::Format::D32Float, U::Sampled, 32);
						  }), // Wrong size.
		std::invalid_argument);
	SWIM_CHECK_THROWS(attempt(
						  [&](RenderGraph& graph, TemporalFrame& frame)
						  {
							  frame.Depth = texture(graph, Rhi::Format::RGBA16Float, U::Sampled);
						  }),
		std::invalid_argument);
	SWIM_CHECK_THROWS(attempt(
						  [&](RenderGraph& graph, TemporalFrame& frame)
						  {
							  frame.Velocity = texture(graph, Rhi::Format::RG32Float, U::Sampled);
						  }),
		std::invalid_argument);
	SWIM_CHECK_THROWS(attempt(
						  [](RenderGraph&, TemporalFrame& frame)
						  {
							  frame.Velocity = frame.Color;
						  }),
		std::invalid_argument);
	SWIM_CHECK(!taa.HasHistory()); // Nothing was recorded.
	SWIM_CHECK_EQUAL(taa.GetFrameIndex(), std::uint64_t(0));
	TemporalSettings bad;
	bad.ClipGamma = 100.0f;
	SWIM_CHECK_THROWS(taa.GetJitterPixels(bad), std::invalid_argument);
	SWIM_CHECK_THROWS(taa.GetJitterNdc(TemporalSettings{}, 0, 10), std::invalid_argument);
}
