#include "Engine/Systems/Renderer/Particles/ParticleSystem.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraphExecutor.h"
#include "Tests/Fixtures/ParticleFixture.h"
#include "Tests/Fixtures/RhiFrameCapture.h"
#include "Tests/Framework/Test.h"

#include <cstring>
#include <functional>
#include <memory>
#include <stdexcept>

using namespace Swim;
using namespace Swim::Render;
namespace P = Swim::Render::Particles;
namespace Scene = Swim::Testing::ParticleScene;

namespace
{
	class MockGraphicsPipeline final : public Rhi::GraphicsPipeline
	{
	  public:
		std::uintptr_t GetNativeHandle() const override { return 21; }
	};

	// A particle system on the mock device: host-backed buffers receive the graph's
	// uploads, so range resets can be read back; dispatches and draws are recorded.
	struct ParticleWorld
	{
		static constexpr std::uint32_t Width = 64;
		static constexpr std::uint32_t Height = 36;

		ParticleWorld()
		{
			device.CreateTextures = true;
			executor = std::make_unique<RenderGraphExecutor>(device);
			using T = Rhi::DescriptorType;
			using B = ParticleSimulationBindings;
			const auto schema = [](Testing::MockPipelineLayout& layout, std::initializer_list<std::pair<std::uint32_t, T>> bindings,
									Rhi::ShaderStageMask stages = Rhi::ShaderStageMask::Compute)
			{
				Rhi::DescriptorSchemaDesc space{ 0, {} };
				for (const auto& [binding, type] : bindings)
				{
					space.Bindings.push_back({ binding, type, 1, stages });
				}
				layout.program.Interface.DescriptorSchemas = { space };
			};
			schema(simulateLayout,
				{ { B::Frame, T::ReadOnlyStorageBuffer }, { B::Emitters, T::ReadOnlyStorageBuffer }, { B::Particles, T::StorageBuffer },
					{ B::FreeList, T::StorageBuffer }, { B::Counters, T::StorageBuffer } });
			schema(emitLayout,
				{ { B::Emitters, T::ReadOnlyStorageBuffer }, { B::Particles, T::StorageBuffer }, { B::FreeList, T::StorageBuffer },
					{ B::Counters, T::StorageBuffer } });
			schema(compactLayout,
				{ { B::Emitters, T::ReadOnlyStorageBuffer }, { B::Particles, T::ReadOnlyStorageBuffer }, { B::DrawList, T::StorageBuffer },
					{ B::Counters, T::StorageBuffer } });
			schema(finalizeLayout,
				{ { B::Frame, T::ReadOnlyStorageBuffer }, { B::Emitters, T::ReadOnlyStorageBuffer },
					{ B::Particles, T::ReadOnlyStorageBuffer }, { B::DrawList, T::StorageBuffer }, { B::Counters, T::StorageBuffer },
					{ B::DrawArgs, T::StorageBuffer } });
			using R = ParticleRenderBindings;
			schema(renderLayout,
				{ { R::Frame, T::ReadOnlyStorageBuffer }, { R::Emitters, T::ReadOnlyStorageBuffer },
					{ R::Particles, T::ReadOnlyStorageBuffer }, { R::DrawList, T::ReadOnlyStorageBuffer } },
				Rhi::ShaderStageMask::Vertex | Rhi::ShaderStageMask::Fragment);
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
		}

		ParticleSystemDesc Desc(std::uint32_t capacity = 1000, std::uint32_t emitters = 8)
		{
			ParticleSystemDesc desc;
			desc.Capacity = capacity;
			desc.MaxEmitters = emitters;
			desc.Simulate = { &simulatePipeline, &simulateLayout };
			desc.Emit = { &emitPipeline, &emitLayout };
			desc.Compact = { &compactPipeline, &compactLayout };
			desc.Finalize = { &finalizePipeline, &finalizeLayout };
			desc.DebugName = "Test particles";
			return desc;
		}

		ParticleRenderProgram Render() { return { &additivePipeline, &alphaPipeline, &renderLayout }; }

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

		template <typename T> static T Read(Rhi::Buffer& buffer, std::size_t index)
		{
			T value{};
			std::memcpy(&value, static_cast<Testing::MockMappedBuffer&>(buffer).Bytes.data() + index * sizeof(T), sizeof(T));
			return value;
		}

		Testing::MockDevice device;
		std::unique_ptr<RenderGraphExecutor> executor;
		Testing::MockPipelineLayout simulateLayout, emitLayout, compactLayout, finalizeLayout, renderLayout;
		Testing::MockComputePipeline simulatePipeline, emitPipeline, compactPipeline, finalizePipeline;
		MockGraphicsPipeline additivePipeline, alphaPipeline;
		std::unique_ptr<Rhi::Texture> color, depth;
	};
} // namespace

SWIM_TEST("Render.ParticleSystem", "RecordsFourComputePassesAndResetsNewRangesOnce")
{
	ParticleWorld world;
	ParticleSystem system(world.device, world.Desc());
	const auto fountain = system.CreateEmitter(Scene::Fountain());
	const auto smoke = system.CreateEmitter(Scene::Smoke(), Scene::Translation(1, 0, 0));
	const auto view = Scene::View(float(ParticleWorld::Width) / float(ParticleWorld::Height));
	const float dt = 1.0f / 30.0f;

	RenderGraph graph;
	const auto resources = system.Simulate(graph, view, dt);
	SWIM_REQUIRE_EQUAL(resources.Emitters.size(), std::size_t(2));
	SWIM_CHECK_EQUAL(resources.InitializedEmitters, 2u);
	SWIM_REQUIRE(resources.SimulatePass && resources.EmitPass && resources.CompactPass && resources.FinalizePass);
	const auto& a = resources.Emitters[0].Record;
	const auto& b = resources.Emitters[1].Record;
	SWIM_CHECK(resources.Emitters[0].Handle == fountain && resources.Emitters[1].Handle == smoke);
	SWIM_CHECK(a.Row == fountain.Index && b.Row == smoke.Index);
	SWIM_CHECK(a.FirstSlot == 0u && b.FirstSlot == 256u && a.Capacity == 256u && b.Capacity == 128u);
	SWIM_CHECK_EQUAL(a.SpawnCount, 4u); // 120/s over 1/30 s.
	SWIM_CHECK_EQUAL(b.SpawnCount, 2u);
	SWIM_CHECK(a.FirstId == 0u && b.FirstId == 0u);
	SWIM_CHECK_EQUAL(resources.FrameRecord.EmitterCount, 2u);
	SWIM_CHECK_EQUAL(resources.FrameRecord.MaxSpawn, 4u);
	SWIM_CHECK(resources.Emitters[1].Blend == ParticleBlendMode::AlphaBlend);
	world.Run(graph);
	system.CommitFrame();

	// Simulate, emit, compact, finalize: 64-slot groups over the largest range, one row per emitter.
	const auto dispatches = world.Commands("Dispatch");
	SWIM_REQUIRE_EQUAL(dispatches.size(), std::size_t(4));
	SWIM_CHECK(dispatches[0].SourceOffset == 4u && dispatches[0].DestinationOffset == 2u);
	SWIM_CHECK(dispatches[1].SourceOffset == 1u && dispatches[1].DestinationOffset == 2u);
	SWIM_CHECK(dispatches[2].SourceOffset == 4u && dispatches[2].DestinationOffset == 2u);
	SWIM_CHECK(dispatches[3].SourceOffset == 2u && dispatches[3].DestinationOffset == 1u);
	const auto pipelines = world.Commands("BindComputePipeline");
	SWIM_REQUIRE_EQUAL(pipelines.size(), std::size_t(4));
	SWIM_CHECK(pipelines[0].Source == &world.simulatePipeline && pipelines[1].Source == &world.emitPipeline &&
		pipelines[2].Source == &world.compactPipeline && pipelines[3].Source == &world.finalizePipeline);
	// The resets landed: free lists hold every slot of each range, counters are fresh.
	const auto counters = ParticleWorld::Read<GpuParticleCounters>(system.GetCounterBuffer(), smoke.Index);
	SWIM_CHECK(counters.Free == 128u && counters.Alive == 0u && counters.Dropped == 0u);
	SWIM_CHECK_EQUAL(ParticleWorld::Read<std::uint32_t>(system.GetFreeListBuffer(), 256 + 17), 273u);
	SWIM_CHECK_EQUAL(ParticleWorld::Read<GpuParticle>(system.GetParticleBuffer(), 300).Lifetime, 0.0f);
	SWIM_CHECK_EQUAL(world.Commands("CopyBuffer").size(), std::size_t(7)); // 3 per new range + the quad indices.

	// The next frame resets nothing and continues the ids.
	RenderGraph next;
	const auto second = system.Simulate(next, view, dt);
	SWIM_CHECK_EQUAL(second.InitializedEmitters, 0u);
	SWIM_CHECK_EQUAL(second.Emitters[0].Record.FirstId, 4u);
	world.Run(next);
	system.CommitFrame();
	SWIM_CHECK(world.Commands("CopyBuffer").empty());
	SWIM_CHECK_EQUAL(world.Commands("Dispatch").size(), std::size_t(4));
}

SWIM_TEST("Render.ParticleSystem", "AbortRewindsTheClocksAndRedoesResets")
{
	ParticleWorld world;
	ParticleSystem system(world.device, world.Desc());
	auto bursts = Scene::Sparks();
	system.CreateEmitter(bursts);
	const auto view = Scene::View(2.0f);
	RenderGraph graph;
	const auto first = system.Simulate(graph, view, 0.1f);
	SWIM_CHECK_EQUAL(first.Emitters[0].Record.SpawnCount, 40u);				 // The burst at 0.
	SWIM_CHECK_THROWS(system.Simulate(graph, view, 0.1f), std::logic_error); // Awaiting commit/abort.
	system.AbortFrame();
	RenderGraph again;
	const auto retry = system.Simulate(again, view, 0.1f);
	SWIM_CHECK_EQUAL(retry.Emitters[0].Record.SpawnCount, 40u);
	SWIM_CHECK_EQUAL(retry.Emitters[0].Record.FirstId, 0u);
	SWIM_CHECK_EQUAL(retry.InitializedEmitters, 1u);
	world.Run(again);
	SWIM_CHECK_EQUAL(world.Commands("CopyBuffer").size(), std::size_t(4)); // The range and the quad indices again.
	system.CommitFrame();
	RenderGraph later;
	const auto third = system.Simulate(later, view, 0.2f);
	SWIM_CHECK_EQUAL(third.Emitters[0].Record.SpawnCount, 20u); // The burst at 0.25.
	system.CommitFrame();
}

SWIM_TEST("Render.ParticleSystem", "DrawsAdditiveFirstThenBlendedEmittersBackToFront")
{
	ParticleWorld world;
	ParticleSystem system(world.device, world.Desc());
	const auto view = Scene::View(2.0f, { 0, 0, 10 }, { 0, 0, 0 });
	const auto nearSmoke = system.CreateEmitter(Scene::Smoke(), Scene::Translation(0, 0, 5));
	const auto fountain = system.CreateEmitter(Scene::Fountain());
	const auto farSmoke = system.CreateEmitter(Scene::Smoke(), Scene::Translation(0, 0, -5));
	RenderGraph graph;
	const auto frame = system.Simulate(graph, view, 0.02f);
	const auto color = graph.ImportTexture(*world.color, Rhi::ResourceState::ColorAttachment);
	const auto depth = graph.ImportTexture(*world.depth, Rhi::ResourceState::DepthStencilWrite);
	Testing::MockDescriptorTable bindless(world.renderLayout, ParticleRenderBindings::BindlessSpace);
	const auto pass = system.Draw(graph, frame, world.Render(), { color, depth }, bindless);
	SWIM_REQUIRE(pass.has_value());
	graph.Export(color, Rhi::ResourceState::ShaderRead);
	world.Run(graph);
	system.CommitFrame();

	const auto draws = world.Commands("DrawIndexedIndirect");
	SWIM_REQUIRE_EQUAL(draws.size(), std::size_t(3));
	SWIM_CHECK_EQUAL(draws[0].SourceOffset, std::uint64_t(fountain.Index) * 20); // Additive first.
	SWIM_CHECK_EQUAL(draws[1].SourceOffset, std::uint64_t(farSmoke.Index) * 20); // Then the farther blended emitter.
	SWIM_CHECK_EQUAL(draws[2].SourceOffset, std::uint64_t(nearSmoke.Index) * 20);
	SWIM_CHECK(draws[0].Size == 1u && draws[0].Source == &system.GetDrawArgumentBuffer());
	const auto pipelines = world.Commands("BindGraphicsPipeline");
	SWIM_REQUIRE_EQUAL(pipelines.size(), std::size_t(2)); // Rebound only when the blend mode changes.
	SWIM_CHECK(pipelines[0].Source == &world.additivePipeline && pipelines[1].Source == &world.alphaPipeline);
	const auto constants = world.Commands("PushConstants");
	SWIM_REQUIRE_EQUAL(constants.size(), std::size_t(3));
	std::array<std::uint32_t, 3> rows{};
	for (int i = 0; i < 3; ++i)
	{
		std::memcpy(&rows[std::size_t(i)], constants[std::size_t(i)].Data.data(), 4);
	}
	// Push constants name the emitter's entry in this frame's records (row order).
	SWIM_CHECK(rows[0] == 1u && rows[1] == 2u && rows[2] == 0u);
	SWIM_CHECK_EQUAL(world.Commands("BindIndexBuffer").size(), std::size_t(1));
	bool boundBindless = false;
	for (const auto& command : world.Commands("BindDescriptorTable"))
	{
		boundBindless = boundBindless || (command.Source == &bindless && command.SourceOffset == ParticleRenderBindings::BindlessSpace);
	}
	SWIM_CHECK(boundBindless);
	SWIM_CHECK_EQUAL(world.device.LastDescriptorTable->ElementWrites, ParticleRenderBindings::Count);

	// Targets must match the pipelines.
	RenderGraph bad;
	const auto badFrame = system.Simulate(bad, view, 0.02f);
	const auto badColor = bad.ImportTexture(*world.depth, Rhi::ResourceState::DepthStencilWrite);
	SWIM_CHECK_THROWS(system.Draw(bad, badFrame, world.Render(), { badColor, badColor }, bindless), std::invalid_argument);
	auto missing = world.Render();
	missing.AlphaBlend = nullptr;
	const auto goodColor = bad.ImportTexture(*world.color, Rhi::ResourceState::ColorAttachment);
	SWIM_CHECK_THROWS(system.Draw(bad, badFrame, missing, { goodColor, badColor }, bindless), std::invalid_argument);
	system.AbortFrame();
}

SWIM_TEST("Render.ParticleSystem", "EmittersAllocateReleaseAndReuseRanges")
{
	ParticleWorld world;
	ParticleSystem system(world.device, world.Desc(600, 3));
	auto big = Scene::Fountain();
	big.Capacity = 300;
	const auto a = system.CreateEmitter(big);
	const auto b = system.CreateEmitter(big);
	SWIM_CHECK(system.GetRange(a) == std::optional(std::pair{ 0u, 300u }) && system.GetRange(b) == std::optional(std::pair{ 300u, 300u }));
	SWIM_CHECK(!system.TryCreateEmitter(Scene::Smoke()).has_value()); // No free range.
	SWIM_CHECK_THROWS(system.CreateEmitter(Scene::Smoke()), std::length_error);
	SWIM_CHECK_EQUAL(system.GetStats().AllocatedSlots, 600u);

	// A released emitter keeps its range until it retires.
	SWIM_CHECK(system.Release(a));
	SWIM_CHECK(!system.IsValid(a) && !system.Release(a));
	SWIM_CHECK(!system.GetRange(a).has_value());
	SWIM_CHECK_EQUAL(system.GetStats().RetiringEmitters, 1u);
	SWIM_CHECK(!system.TryCreateEmitter(Scene::Smoke()).has_value());
	SWIM_CHECK_EQUAL(system.Collect(), std::size_t(1));
	const auto c = system.CreateEmitter(Scene::Smoke()); // 128 slots at the freed start.
	const auto d = system.CreateEmitter(Scene::Smoke());
	SWIM_CHECK(system.GetRange(c)->first == 0u && system.GetRange(d)->first == 128u);
	SWIM_CHECK(!system.TryCreateEmitter(Scene::Smoke()).has_value()); // Three emitter rows are the limit.
	SWIM_CHECK_EQUAL(system.GetStats().LargestFreeRange, 44u);
	// Freed neighbours coalesce.
	system.Release(c);
	system.Release(d);
	system.Collect();
	SWIM_CHECK_EQUAL(system.GetStats().LargestFreeRange, 300u);

	// Transforms, emission toggles and invalid handles.
	SWIM_CHECK(!system.SetTransform(a, Scene::Translation(1, 2, 3)));
	SWIM_CHECK_THROWS(system.SetTransform(b, { NAN, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 }), std::invalid_argument);
	SWIM_CHECK(system.SetTransform(b, Scene::Translation(1, 2, 3)));
	SWIM_CHECK(system.SetEmitting(b, false));
	RenderGraph graph;
	const auto frame = system.Simulate(graph, Scene::View(2.0f), 0.5f);
	SWIM_REQUIRE_EQUAL(frame.Emitters.size(), std::size_t(1));
	SWIM_CHECK_EQUAL(frame.Emitters[0].Record.SpawnCount, 0u);
	SWIM_CHECK(frame.Emitters[0].Record.Transform[3] == 1.0f && frame.Emitters[0].Record.Transform[11] == 3.0f);
	system.CommitFrame();
	SWIM_CHECK(system.SetEmitting(b, true));
	RenderGraph resumed;
	const auto resumedFrame = system.Simulate(resumed, Scene::View(2.0f), 0.5f);
	SWIM_CHECK_EQUAL(resumedFrame.Emitters[0].Record.SpawnCount, 60u);
	system.CommitFrame();
	system.Drain();
}

SWIM_TEST("Render.ParticleSystem", "RejectsInvalidProgramsDescsAndViewsAndRecordsNothingWhenEmpty")
{
	ParticleWorld world;
	SWIM_CHECK_THROWS(ParticleSystem(world.device, ParticleSystemDesc{}), std::invalid_argument);
	auto partial = world.Desc();
	partial.Finalize = {};
	SWIM_CHECK_THROWS(ParticleSystem(world.device, partial), std::invalid_argument);
	SWIM_CHECK_THROWS(ParticleSystem(world.device, world.Desc(0)), std::invalid_argument);
	SWIM_CHECK_THROWS(ParticleSystem(world.device, world.Desc(10, 0)), std::invalid_argument);

	ParticleSystem system(world.device, world.Desc());
	auto invalid = Scene::Fountain();
	invalid.LifetimeMin = -1.0f;
	SWIM_CHECK_THROWS(system.CreateEmitter(invalid), std::invalid_argument);
	SWIM_CHECK_EQUAL(system.GetStats().AllocatedSlots, 0u); // Nothing leaked.

	// No emitters: nothing is recorded and nothing awaits a commit.
	RenderGraph graph;
	const auto empty = system.Simulate(graph, Scene::View(2.0f), 0.1f);
	SWIM_CHECK(empty.Emitters.empty() && !empty.SimulatePass && !empty.Frame);
	RenderGraph again;
	SWIM_CHECK(system.Simulate(again, Scene::View(2.0f), 0.1f).Emitters.empty());
	const auto color = graph.ImportTexture(*world.color, Rhi::ResourceState::ColorAttachment);
	const auto depth = graph.ImportTexture(*world.depth, Rhi::ResourceState::DepthStencilWrite);
	Testing::MockDescriptorTable bindless(world.renderLayout, ParticleRenderBindings::BindlessSpace);
	SWIM_CHECK(!system.Draw(graph, empty, world.Render(), { color, depth }, bindless).has_value());

	system.CreateEmitter(Scene::Fountain());
	RenderGraph committed;
	const auto first = system.Simulate(committed, Scene::View(2.0f), 0.1f);
	SWIM_CHECK_EQUAL(first.Emitters[0].Record.FirstId, 0u);
	system.CommitFrame();
	auto bad = Scene::View(2.0f);
	bad.View[0] = NAN;
	RenderGraph rejected;
	SWIM_CHECK_THROWS(system.Simulate(rejected, bad, 0.1f), std::invalid_argument);
	SWIM_CHECK_THROWS(system.Simulate(rejected, Scene::View(2.0f), -0.1f), std::invalid_argument);
	// A rejected frame neither leaves a frame pending nor rewinds the committed clocks.
	RenderGraph after;
	const auto next = system.Simulate(after, Scene::View(2.0f), 0.1f);
	SWIM_CHECK_EQUAL(next.Emitters[0].Record.FirstId, 12u); // 120/s: 12 spawned in the committed frame.
	system.CommitFrame();

	// Pipelines: additive keeps destination alpha; blended emitters use premultiplied over.
	Testing::MockShaderProgram program;
	const auto additive = ParticleSystem::PipelineDesc(ParticleBlendMode::Additive, program, world.renderLayout);
	const auto alpha = ParticleSystem::PipelineDesc(ParticleBlendMode::AlphaBlend, program, world.renderLayout);
	SWIM_REQUIRE(additive.BlendAttachments.size() == 1u && alpha.BlendAttachments.size() == 1u);
	SWIM_CHECK(additive.BlendAttachments[0].DestinationColor == Rhi::BlendFactor::One &&
		additive.BlendAttachments[0].SourceAlpha == Rhi::BlendFactor::Zero &&
		additive.BlendAttachments[0].DestinationAlpha == Rhi::BlendFactor::One);
	SWIM_CHECK(alpha.BlendAttachments[0].SourceColor == Rhi::BlendFactor::One &&
		alpha.BlendAttachments[0].DestinationColor == Rhi::BlendFactor::OneMinusSourceAlpha);
	SWIM_CHECK(additive.DepthStencil.DepthTest && !additive.DepthStencil.DepthWrite);
	SWIM_CHECK(additive.DepthStencil.DepthCompare == Rhi::CompareOp::GreaterEqual && additive.Raster.Cull == Rhi::CullMode::None);
	SWIM_CHECK(additive.ColorFormats.size() == 1u && additive.ColorFormats[0] == Rhi::Format::RGBA16Float);
	SWIM_CHECK(additive.DepthStencilFormat == Rhi::Format::D32Float);
	SWIM_CHECK_THROWS(ParticleSystem::PipelineDesc(static_cast<ParticleBlendMode>(9), program, world.renderLayout), std::invalid_argument);
}
