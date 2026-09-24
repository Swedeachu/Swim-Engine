#include "Engine/Systems/Renderer/RenderGraph/RenderGraphExecutor.h"
#include "Engine/Systems/Renderer/ScreenSpace/ScreenSpaceEffects.h"
#include "Tests/Fixtures/RhiFrameCapture.h"
#include "Tests/Fixtures/ScreenSpaceFixture.h"
#include "Tests/Framework/Test.h"

#include <functional>
#include <memory>
#include <stdexcept>

using namespace Swim;
using namespace Swim::Render;

namespace
{
	// Screen-space effects on the mock device: imported inputs and the three programs
	// with their reflected-style interfaces.
	struct ScreenSpaceWorld
	{
		static constexpr std::uint32_t Width = 64;
		static constexpr std::uint32_t Height = 36;

		ScreenSpaceWorld()
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
			schema(aoLayout,
				{ { ScreenSpaceAoBindings::Depth, T::SampledTexture }, { ScreenSpaceAoBindings::Normal, T::SampledTexture },
					{ ScreenSpaceAoBindings::Params, T::ReadOnlyStorageBuffer }, { ScreenSpaceAoBindings::Output, T::StorageTexture } });
			schema(blurLayout,
				{ { ScreenSpaceBlurBindings::Source, T::SampledTexture }, { ScreenSpaceBlurBindings::Depth, T::SampledTexture },
					{ ScreenSpaceBlurBindings::Params, T::ReadOnlyStorageBuffer },
					{ ScreenSpaceBlurBindings::Output, T::StorageTexture } });
			schema(compositeLayout,
				{ { ScreenSpaceCompositeBindings::Color, T::SampledTexture }, { ScreenSpaceCompositeBindings::Indirect, T::SampledTexture },
					{ ScreenSpaceCompositeBindings::Ao, T::SampledTexture }, { ScreenSpaceCompositeBindings::Depth, T::SampledTexture },
					{ ScreenSpaceCompositeBindings::Params, T::ReadOnlyStorageBuffer },
					{ ScreenSpaceCompositeBindings::Output, T::StorageTexture } });
			const auto make = [&](Rhi::Format format, Rhi::TextureUsage usage)
			{
				Rhi::TextureDesc desc;
				desc.Extent = { Width, Height, 1 };
				desc.PixelFormat = format;
				desc.Usage = usage | Rhi::TextureUsage::Sampled;
				return device.CreateTexture(desc);
			};
			color = make(Rhi::Format::RGBA16Float, Rhi::TextureUsage::ColorAttachment);
			depth = make(Rhi::Format::D32Float, Rhi::TextureUsage::DepthStencilAttachment);
			normal = make(Rhi::Format::RGBA16Float, Rhi::TextureUsage::ColorAttachment);
			indirect = make(Rhi::Format::RGBA16Float, Rhi::TextureUsage::ColorAttachment);
		}

		ScreenSpaceEffectsDesc Desc()
		{
			ScreenSpaceEffectsDesc desc;
			desc.AmbientOcclusion = { &aoPipeline, &aoLayout, 0 };
			desc.Blur = { &blurPipeline, &blurLayout, 0 };
			desc.Composite = { &compositePipeline, &compositeLayout, 0 };
			desc.DebugName = "Test screen space";
			return desc;
		}

		ScreenSpaceFrame Frame(RenderGraph& graph, const ScreenSpaceSettings& settings = {})
		{
			ScreenSpaceFrame frame;
			frame.Color = graph.ImportTexture(*color, Rhi::ResourceState::ShaderRead);
			frame.Depth = graph.ImportTexture(*depth, Rhi::ResourceState::ShaderRead);
			frame.Normal = graph.ImportTexture(*normal, Rhi::ResourceState::ShaderRead);
			frame.Indirect = graph.ImportTexture(*indirect, Rhi::ResourceState::ShaderRead);
			frame.View = Testing::ScreenSpaceScene::View({ 0, 2, 6 }, { 0, 0, 0 }, float(Width) / float(Height));
			frame.Settings = settings;
			frame.NoiseFrame = 3;
			return frame;
		}

		void Run(RenderGraph& graph, GraphTexture output)
		{
			graph.Export(output, Rhi::ResourceState::ShaderRead);
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

		const Rhi::TextureView& Bound(std::uint32_t binding) const
		{
			SWIM_REQUIRE(device.LastDescriptorTable != nullptr);
			const auto* element = device.LastDescriptorTable->Element(binding, 0);
			SWIM_REQUIRE(element != nullptr);
			return *static_cast<const Rhi::TextureView*>(element);
		}

		Testing::MockDevice device;
		std::unique_ptr<RenderGraphExecutor> executor;
		Testing::MockPipelineLayout aoLayout, blurLayout, compositeLayout;
		Testing::MockComputePipeline aoPipeline, blurPipeline, compositePipeline;
		std::unique_ptr<Rhi::Texture> color, depth, normal, indirect;
	};
} // namespace

SWIM_TEST("Render.ScreenSpaceEffects", "RecordsAoBlurAndCompositeOrOnlyWhatIsEnabled")
{
	using C = ScreenSpaceCompositeBindings;
	ScreenSpaceWorld world;
	const ScreenSpaceEffects effects(world.Desc());

	// AO and fog: three dispatches in order, each 8 x 5 groups.
	{
		ScreenSpaceSettings settings;
		settings.Fog.Enabled = true;
		RenderGraph graph;
		const auto frame = world.Frame(graph, settings);
		const auto resources = effects.Record(graph, frame);
		SWIM_CHECK(!resources.Passthrough);
		SWIM_REQUIRE(resources.AmbientOcclusionRaw && resources.AmbientOcclusion && resources.Params && resources.CompositePass);
		SWIM_CHECK(graph.GetDesc(*resources.AmbientOcclusion).PixelFormat == Rhi::Format::R32Float);
		SWIM_CHECK(graph.GetDesc(resources.Output).PixelFormat == Rhi::Format::RGBA16Float);
		SWIM_CHECK_EQUAL(graph.GetDesc(resources.Output).Extent.Width, ScreenSpaceWorld::Width);
		SWIM_CHECK_EQUAL(graph.GetDesc(*resources.Params).Size, std::uint64_t(sizeof(GpuScreenSpaceParams)));
		SWIM_CHECK(resources.ParamsRecord.AoEnabled == 1u && resources.ParamsRecord.FogEnabled == 1u);
		SWIM_CHECK_EQUAL(resources.ParamsRecord.NoiseFrame, 3u);
		world.Run(graph, resources.Output);
		const auto dispatches = world.Commands("Dispatch");
		SWIM_REQUIRE_EQUAL(dispatches.size(), std::size_t(3));
		for (const auto& dispatch : dispatches)
		{
			SWIM_CHECK_EQUAL(dispatch.SourceOffset, 8u);	  // 64 / 8
			SWIM_CHECK_EQUAL(dispatch.DestinationOffset, 5u); // ceil(36 / 8)
		}
		const auto pipelines = world.Commands("BindComputePipeline");
		SWIM_REQUIRE_EQUAL(pipelines.size(), std::size_t(3));
		SWIM_CHECK(pipelines[0].Source == &world.aoPipeline && pipelines[1].Source == &world.blurPipeline &&
			pipelines[2].Source == &world.compositePipeline);
		SWIM_CHECK(world.Commands("PushConstants").empty());
		// The composite's table: the inputs, the depth through its depth aspect, and the output.
		SWIM_CHECK(&world.Bound(C::Color).GetTexture() == world.color.get());
		SWIM_CHECK(&world.Bound(C::Indirect).GetTexture() == world.indirect.get());
		const auto& depthView = world.Bound(C::Depth);
		SWIM_CHECK(&depthView.GetTexture() == world.depth.get());
		SWIM_CHECK(depthView.GetDesc().Aspect == Rhi::TextureAspect::Depth && depthView.GetDesc().PixelFormat == Rhi::Format::D32Float);
		SWIM_CHECK(world.Bound(C::Ao).GetTexture().GetDesc().PixelFormat == Rhi::Format::R32Float);
		SWIM_CHECK(world.Bound(C::Ao).GetTexture().GetDesc().Extent.Width == ScreenSpaceWorld::Width);
		SWIM_CHECK(world.Bound(C::Output).GetTexture().GetDesc().PixelFormat == Rhi::Format::RGBA16Float);
		SWIM_CHECK(world.device.LastDescriptorTable->Element(C::Params, 0) != nullptr);
		SWIM_CHECK_EQUAL(world.device.LastDescriptorTable->ElementWrites, C::Count);
	}

	// Fog only: the composite alone, with a 1x1 AO stand-in.
	{
		ScreenSpaceSettings settings;
		settings.AmbientOcclusion.Enabled = false;
		settings.Fog.Enabled = true;
		RenderGraph graph;
		const auto resources = effects.Record(graph, world.Frame(graph, settings));
		SWIM_CHECK(!resources.AmbientOcclusion && !resources.AmbientOcclusionPass && resources.CompositePass);
		world.Run(graph, resources.Output);
		SWIM_CHECK_EQUAL(world.Commands("Dispatch").size(), std::size_t(1));
		SWIM_CHECK_EQUAL(world.Commands("CopyBufferToTexture").size(), std::size_t(1));
		SWIM_CHECK_EQUAL(world.Bound(C::Ao).GetTexture().GetDesc().Extent.Width, 1u);
	}

	// Everything off: nothing is recorded and the color passes through.
	{
		ScreenSpaceSettings settings;
		settings.AmbientOcclusion.Enabled = false;
		RenderGraph graph;
		const auto frame = world.Frame(graph, settings);
		const auto resources = effects.Record(graph, frame);
		SWIM_CHECK(resources.Passthrough && resources.Output == frame.Color);
		SWIM_CHECK(!resources.Params && !resources.CompositePass);
		world.Run(graph, resources.Output);
		SWIM_CHECK(world.Commands("Dispatch").empty());
	}

	// R32Float depth binds without the depth aspect.
	{
		Rhi::TextureDesc desc;
		desc.Extent = { ScreenSpaceWorld::Width, ScreenSpaceWorld::Height, 1 };
		desc.PixelFormat = Rhi::Format::R32Float;
		desc.Usage = Rhi::TextureUsage::Sampled | Rhi::TextureUsage::ColorAttachment;
		const auto linearDepth = world.device.CreateTexture(desc);
		RenderGraph graph;
		auto frame = world.Frame(graph);
		frame.Depth = graph.ImportTexture(*linearDepth, Rhi::ResourceState::ShaderRead);
		const auto resources = effects.Record(graph, frame);
		world.Run(graph, resources.Output);
		SWIM_CHECK(world.Bound(C::Depth).GetDesc().Aspect == Rhi::TextureAspect::Automatic);
		SWIM_CHECK(world.Bound(C::Depth).GetDesc().PixelFormat == Rhi::Format::R32Float);
	}
	SWIM_CHECK(ScreenSpaceEffects::OcclusionDesc(4, 2).PixelFormat == Rhi::Format::R32Float);
	SWIM_CHECK(ScreenSpaceEffects::OutputDesc(4, 2).PixelFormat == Rhi::Format::RGBA16Float);
}

SWIM_TEST("Render.ScreenSpaceEffects", "RejectsMissingProgramsAndInvalidInputs")
{
	ScreenSpaceWorld world;
	SWIM_CHECK_THROWS(ScreenSpaceEffects(ScreenSpaceEffectsDesc{}), std::invalid_argument);
	auto partial = world.Desc();
	partial.Blur = {};
	SWIM_CHECK_THROWS(ScreenSpaceEffects(partial), std::invalid_argument);
	const ScreenSpaceEffects effects(world.Desc());
	const auto attempt = [&](const std::function<void(RenderGraph&, ScreenSpaceFrame&)>& edit)
	{
		RenderGraph graph;
		auto frame = world.Frame(graph);
		edit(graph, frame);
		effects.Record(graph, frame);
	};
	const auto texture = [](RenderGraph& graph, Rhi::Format format, Rhi::TextureUsage usage, std::uint32_t width = ScreenSpaceWorld::Width)
	{
		Rhi::TextureDesc desc;
		desc.Extent = { width, ScreenSpaceWorld::Height, 1 };
		desc.PixelFormat = format;
		desc.Usage = usage;
		return graph.CreateTexture(desc);
	};
	using U = Rhi::TextureUsage;
	SWIM_CHECK_THROWS(attempt(
						  [&](RenderGraph& g, ScreenSpaceFrame& f)
						  {
							  f.Color = texture(g, Rhi::Format::RGBA8Unorm, U::Sampled);
						  }),
		std::invalid_argument);
	SWIM_CHECK_THROWS(attempt(
						  [&](RenderGraph& g, ScreenSpaceFrame& f)
						  {
							  f.Color = texture(g, Rhi::Format::RGBA16Float, U::Storage);
						  }),
		std::invalid_argument);
	SWIM_CHECK_THROWS(attempt(
						  [&](RenderGraph& g, ScreenSpaceFrame& f)
						  {
							  f.Depth = texture(g, Rhi::Format::D32Float, U::Sampled, 32);
						  }),
		std::invalid_argument);
	SWIM_CHECK_THROWS(attempt(
						  [&](RenderGraph& g, ScreenSpaceFrame& f)
						  {
							  f.Depth = texture(g, Rhi::Format::RG16Float, U::Sampled);
						  }),
		std::invalid_argument);
	SWIM_CHECK_THROWS(attempt(
						  [&](RenderGraph& g, ScreenSpaceFrame& f)
						  {
							  f.Normal = texture(g, Rhi::Format::RGBA8Unorm, U::Sampled);
						  }),
		std::invalid_argument);
	SWIM_CHECK_THROWS(attempt(
						  [&](RenderGraph& g, ScreenSpaceFrame& f)
						  {
							  f.Indirect = texture(g, Rhi::Format::RGBA16Float, U::Sampled, 40);
						  }),
		std::invalid_argument);
	SWIM_CHECK_THROWS(attempt(
						  [](RenderGraph&, ScreenSpaceFrame& f)
						  {
							  f.Settings.AmbientOcclusion.StepCount = 9;
						  }),
		std::invalid_argument);
	SWIM_CHECK_THROWS(attempt(
						  [](RenderGraph&, ScreenSpaceFrame& f)
						  {
							  f.View.Projection[14] = 0.0f;
						  }),
		std::invalid_argument);
}
