#include "Engine/Systems/Renderer/RenderGraph/RenderGraph.h"
#include "Tests/Framework/Test.h"
#include "Tests/Fixtures/RhiFrameCapture.h"
#include <algorithm>

using namespace Swim;
using namespace Swim::Render;
using S = Rhi::ResourceState;
using Q = Rhi::QueueType;

namespace
{
	const auto noCommands = [](RenderCommandContext&)
	{
	};

	Rhi::BufferDesc BufferDesc()
	{
		return { 64, Rhi::BufferUsage::Storage | Rhi::BufferUsage::TransferSource | Rhi::BufferUsage::TransferDestination,
			Rhi::MemoryPreference::DeviceLocal, "data" };
	}

	Rhi::TextureDesc TextureDesc()
	{
		Rhi::TextureDesc d;
		d.Extent = { 16, 16, 1 };
		d.MipLevels = 2;
		d.ArrayLayers = 2;
		d.PixelFormat = Rhi::Format::RGBA8Unorm;
		d.Usage = Rhi::TextureUsage::Storage | Rhi::TextureUsage::Sampled | Rhi::TextureUsage::TransferDestination;
		return d;
	}
} // namespace

SWIM_TEST("RenderGraph.Compile", "RejectsReadBeforeWriteAndUninitializedExports")
{
	RenderGraph graph;
	auto buffer = graph.CreateBuffer(BufferDesc());
	graph.AddPass(
		"bad read", Q::Compute,
		[&](auto& b)
		{
			b.Read(buffer, S::ShaderRead);
			b.SideEffect();
		},
		noCommands);
	SWIM_CHECK_THROWS(graph.Compile(), std::invalid_argument);
	RenderGraph empty;
	empty.Export(empty.CreateBuffer(BufferDesc()), S::CopySource);
	SWIM_CHECK_THROWS(empty.Compile(), std::invalid_argument);
}

SWIM_TEST("RenderGraph.Compile", "RejectsCyclesEvenWhenPassesAreDead")
{
	RenderGraph graph;
	auto a = graph.AddPass(
		"a", Q::Graphics,
		[](auto&)
		{
		},
		noCommands);
	auto b = graph.AddPass(
		"b", Q::Graphics,
		[](auto&)
		{
		},
		noCommands);
	graph.AddDependency(a, b);
	graph.AddDependency(b, a);
	SWIM_CHECK_THROWS(graph.Compile(), std::invalid_argument);
	RenderGraph self;
	auto c = self.AddPass(
		"self", Q::Graphics,
		[](auto&)
		{
		},
		noCommands);
	self.AddDependency(c, c);
	SWIM_CHECK_THROWS(self.Compile(), std::invalid_argument);
}

SWIM_TEST("RenderGraph.Compile", "OrdersForwardDependenciesDeterministically")
{
	RenderGraph graph;
	auto a = graph.AddPass(
		"a", Q::Graphics,
		[](auto& b)
		{
			b.SideEffect();
		},
		noCommands);
	auto b = graph.AddPass(
		"b", Q::Graphics,
		[](auto& b)
		{
			b.SideEffect();
		},
		noCommands);
	auto c = graph.AddPass(
		"c", Q::Graphics,
		[](auto& b)
		{
			b.SideEffect();
		},
		noCommands);
	graph.AddDependency(a, c);
	auto compiled = graph.Compile();
	SWIM_REQUIRE_EQUAL(compiled.GetSchedule().size(), 3u);
	SWIM_CHECK_EQUAL(compiled.GetSchedule()[0].Pass, b.Index);
	SWIM_CHECK_EQUAL(compiled.GetSchedule()[1].Pass, c.Index);
	SWIM_CHECK_EQUAL(compiled.GetSchedule()[2].Pass, a.Index);
	SWIM_CHECK_EQUAL(compiled.Dump(), graph.Compile().Dump());
}

SWIM_TEST("RenderGraph.Compile", "CullsOverwrittenTransientWritesButKeepsDataAndExplicitDependencies")
{
	RenderGraph graph;
	auto buffer = graph.CreateBuffer(BufferDesc());
	graph.AddPass(
		"overwritten", Q::Compute,
		[&](auto& b)
		{
			b.Write(buffer, S::ShaderWrite);
		},
		noCommands);
	auto prerequisite = graph.AddPass(
		"explicit", Q::Graphics,
		[](auto&)
		{
		},
		noCommands);
	auto producer = graph.AddPass(
		"producer", Q::Compute,
		[&](auto& b)
		{
			b.Write(buffer, S::ShaderWrite);
			b.DependsOn(prerequisite);
		},
		noCommands);
	auto consumer = graph.AddPass(
		"consumer", Q::Compute,
		[&](auto& b)
		{
			b.Read(buffer, S::ShaderRead);
			b.SideEffect();
		},
		noCommands);
	auto plan = graph.Compile();
	SWIM_REQUIRE_EQUAL(plan.GetSchedule().size(), 3u);
	SWIM_CHECK_EQUAL(plan.GetSchedule()[0].Pass, prerequisite.Index);
	SWIM_CHECK_EQUAL(plan.GetSchedule()[1].Pass, producer.Index);
	SWIM_CHECK_EQUAL(plan.GetSchedule()[2].Pass, consumer.Index);
	SWIM_CHECK(plan.Dump().find("culled 0") != std::string::npos);
	SWIM_CHECK_EQUAL(plan.GetLifetimes()[buffer.Index].First, 1u);
	SWIM_CHECK_EQUAL(plan.GetLifetimes()[buffer.Index].Last, 2u);
}

SWIM_TEST("RenderGraph.Compile", "SynthesizesRAW_WAR_WAWAndSameStateStorageBarriers")
{
	RenderGraph graph;
	auto buffer = graph.CreateBuffer(BufferDesc());
	graph.AddPass(
		"write", Q::Compute,
		[&](auto& b)
		{
			b.Write(buffer, S::ShaderRead | S::ShaderWrite);
			b.SideEffect();
		},
		noCommands);
	graph.AddPass(
		"rw", Q::Compute,
		[&](auto& b)
		{
			b.ReadWrite(buffer, S::ShaderRead | S::ShaderWrite);
			b.SideEffect();
		},
		noCommands);
	graph.AddPass(
		"read", Q::Compute,
		[&](auto& b)
		{
			b.Read(buffer, S::ShaderRead);
			b.SideEffect();
		},
		noCommands);
	graph.AddPass(
		"read again", Q::Compute,
		[&](auto& b)
		{
			b.Read(buffer, S::ShaderRead);
			b.SideEffect();
		},
		noCommands);
	graph.AddPass(
		"overwrite", Q::Compute,
		[&](auto& b)
		{
			b.Write(buffer, S::ShaderWrite);
			b.SideEffect();
		},
		noCommands);
	graph.Export(buffer, S::CopySource);
	auto plan = graph.Compile();
	SWIM_REQUIRE_EQUAL(plan.GetSchedule().size(), 5u);
	const auto passes = plan.GetSchedule();
	SWIM_REQUIRE_EQUAL(passes[1].Barriers.size(), 1u);
	SWIM_CHECK_EQUAL(passes[1].Barriers[0].Before, S::ShaderRead | S::ShaderWrite);
	SWIM_CHECK_EQUAL(passes[1].Barriers[0].After, passes[1].Barriers[0].Before);
	SWIM_CHECK_EQUAL(passes[2].Barriers.size(), 1u);
	SWIM_CHECK(passes[3].Barriers.empty());
	SWIM_REQUIRE_EQUAL(passes[4].Barriers.size(), 1u);
	SWIM_CHECK_EQUAL(passes[4].Barriers[0].Before, S::ShaderRead);
	SWIM_REQUIRE_EQUAL(plan.GetFinalBarriers().size(), 1u);
	SWIM_CHECK_EQUAL(plan.GetFinalBarriers()[0].After, S::CopySource);
}

SWIM_TEST("RenderGraph.Compile", "TracksEachMipAndLayerAndRejectsPartialInitialization")
{
	RenderGraph graph;
	auto texture = graph.CreateTexture(TextureDesc());
	graph.AddPass(
		"mip1 layer0", Q::Compute,
		[&](auto& b)
		{
			b.Write(texture, S::ShaderWrite, { 1, 1, 0, 1 });
		},
		noCommands);
	graph.AddPass(
		"read other layer", Q::Compute,
		[&](auto& b)
		{
			b.Read(texture, S::ShaderRead, { 1, 1, 1, 1 });
			b.SideEffect();
		},
		noCommands);
	SWIM_CHECK_THROWS(graph.Compile(), std::invalid_argument);
	RenderGraph valid;
	auto t = valid.CreateTexture(TextureDesc());
	valid.AddPass(
		"write", Q::Compute,
		[&](auto& b)
		{
			b.Write(t, S::ShaderWrite, { 1, 1, 1, 1 });
		},
		noCommands);
	valid.AddPass(
		"storage read", Q::Compute,
		[&](auto& b)
		{
			b.Read(t, S::ShaderRead | S::ShaderWrite, { 1, 1, 1, 1 });
			b.SideEffect();
		},
		noCommands);
	auto plan = valid.Compile();
	SWIM_REQUIRE_EQUAL(plan.GetSchedule().size(), 2u);
	for (const auto& pass : plan.GetSchedule())
	{
		SWIM_REQUIRE_EQUAL(pass.Barriers.size(), 1u);
		SWIM_CHECK_EQUAL(pass.Barriers[0].Range.BaseMipLevel, 1u);
		SWIM_CHECK_EQUAL(pass.Barriers[0].Range.BaseArrayLayer, 1u);
	}
	valid.Export(t, S::ShaderRead);
	SWIM_CHECK_THROWS(valid.Compile(), std::invalid_argument);
}

SWIM_TEST("RenderGraph.Compile", "PoolsOnlyCompatibleNonoverlappingResourcesAndPinsExports")
{
	RenderGraph graph;
	auto a = graph.CreateBuffer(BufferDesc());
	auto b = graph.CreateBuffer(BufferDesc());
	auto c = graph.CreateBuffer(BufferDesc());
	graph.AddPass(
		"a", Q::Compute,
		[&](auto& p)
		{
			p.Write(a, S::ShaderWrite);
			p.SideEffect();
		},
		noCommands);
	graph.AddPass(
		"b", Q::Compute,
		[&](auto& p)
		{
			p.Write(b, S::ShaderWrite);
			p.SideEffect();
		},
		noCommands);
	graph.AddPass(
		"c", Q::Compute,
		[&](auto& p)
		{
			p.Write(c, S::ShaderWrite);
		},
		noCommands);
	graph.Export(b, S::ShaderRead);
	graph.Export(c, S::ShaderRead);
	auto plan = graph.Compile();
	SWIM_CHECK_EQUAL(plan.GetAllocationCount(), 2u);
	SWIM_CHECK_EQUAL(plan.GetLifetimes()[a.Index].Allocation, plan.GetLifetimes()[b.Index].Allocation);
	SWIM_CHECK(plan.GetLifetimes()[b.Index].Allocation != plan.GetLifetimes()[c.Index].Allocation);
	SWIM_CHECK_EQUAL(plan.GetLifetimes()[b.Index].Last, 3u);
	SWIM_CHECK_EQUAL(plan.GetSchedule()[1].Barriers[0].Before, S::ShaderWrite);
	SWIM_CHECK_EQUAL(plan.GetSchedule()[1].Barriers[0].After, S::ShaderWrite);
}

SWIM_TEST("RenderGraph.Compile", "ImportsPreserveContentsAndRestoreStateUnlessExported")
{
	Testing::MockDevice device;
	auto buffer = device.CreateBuffer(BufferDesc());
	RenderGraph graph;
	auto imported = graph.ImportBuffer(*buffer, S::CopySource);
	graph.AddPass(
		"read", Q::Compute,
		[&](auto& b)
		{
			b.Read(imported, S::ShaderRead);
			b.SideEffect();
		},
		noCommands);
	auto plan = graph.Compile();
	SWIM_CHECK_EQUAL(plan.GetSchedule()[0].Barriers[0].Before, S::CopySource);
	SWIM_CHECK_EQUAL(plan.GetFinalBarriers()[0].After, S::CopySource);
	SWIM_CHECK_THROWS(graph.ImportBuffer(*buffer, S::CopySource), std::invalid_argument);
	RenderGraph foreignOwner;
	SWIM_CHECK_THROWS(foreignOwner.ImportBuffer(*buffer, S::CopySource, Q::Transfer), std::invalid_argument);
	graph.Export(imported, S::ShaderRead);
	SWIM_CHECK_THROWS(graph.Export(imported, S::CopySource), std::invalid_argument);
}

SWIM_TEST("RenderGraph.Compile", "RejectsForeignHandlesOverlapsAndInvalidDeclarationsTransactionally")
{
	RenderGraph graph, other;
	auto a = graph.CreateBuffer(BufferDesc());
	auto foreign = other.CreateBuffer(BufferDesc());
	SWIM_CHECK_THROWS(graph.Export(foreign, S::ShaderRead), std::invalid_argument);
	SWIM_CHECK_THROWS(graph.AddPass(
						  "foreign", Q::Compute,
						  [&](auto& p)
						  {
							  p.Write(foreign, S::ShaderWrite);
						  },
						  noCommands),
		std::invalid_argument);
	SWIM_CHECK_THROWS(graph.AddPass(
						  "overlap", Q::Compute,
						  [&](auto& p)
						  {
							  p.Read(a, S::ShaderRead);
							  p.Write(a, S::ShaderWrite);
						  },
						  noCommands),
		std::invalid_argument);
	SWIM_CHECK_THROWS(graph.AddPass(
						  "invalid state", Q::Transfer,
						  [&](auto& p)
						  {
							  p.Write(a, S::ShaderWrite);
						  },
						  noCommands),
		std::invalid_argument);
	SWIM_CHECK_THROWS(graph.AddPass(
						  "invalid access", Q::Compute,
						  [&](auto& p)
						  {
							  p.Read(a, S::ShaderWrite);
						  },
						  noCommands),
		std::invalid_argument);
	SWIM_CHECK_THROWS(graph.AddPass(
						  "missing read access", Q::Compute,
						  [&](auto& p)
						  {
							  p.ReadWrite(a, S::ShaderWrite);
						  },
						  noCommands),
		std::invalid_argument);
	SWIM_CHECK_THROWS(graph.AddPass(
						  "missing usage", Q::Graphics,
						  [&](auto& p)
						  {
							  p.Read(a, S::VertexBuffer);
						  },
						  noCommands),
		std::invalid_argument);
	SWIM_CHECK(graph.Compile().GetSchedule().empty());
	auto texture = graph.CreateTexture(TextureDesc());
	SWIM_CHECK_THROWS(graph.AddPass(
						  "range", Q::Compute,
						  [&](auto& p)
						  {
							  p.Write(texture, S::ShaderWrite, { 2, 1, 0, 1 });
						  },
						  noCommands),
		std::invalid_argument);
	SWIM_CHECK_THROWS(graph.AddPass(
						  "zero", Q::Compute,
						  [&](auto& p)
						  {
							  p.Write(texture, S::ShaderWrite, { 0, 0, 0, 1 });
						  },
						  noCommands),
		std::invalid_argument);
}

SWIM_TEST("RenderGraph.Compile", "DoesNotPoolOverlappingOrIncompatibleResources")
{
	RenderGraph graph;
	auto a = graph.CreateBuffer(BufferDesc());
	auto b = graph.CreateBuffer(BufferDesc());
	auto different = BufferDesc();
	different.Size *= 2;
	auto c = graph.CreateBuffer(different);
	graph.AddPass(
		"write both", Q::Compute,
		[&](auto& p)
		{
			p.Write(a, S::ShaderWrite);
			p.Write(b, S::ShaderWrite);
			p.SideEffect();
		},
		noCommands);
	graph.AddPass(
		"consume a", Q::Compute,
		[&](auto& p)
		{
			p.Read(a, S::ShaderRead);
			p.SideEffect();
		},
		noCommands);
	graph.AddPass(
		"different size", Q::Compute,
		[&](auto& p)
		{
			p.Write(c, S::ShaderWrite);
			p.SideEffect();
		},
		noCommands);
	auto plan = graph.Compile();
	SWIM_CHECK_EQUAL(plan.GetAllocationCount(), 3u);
	SWIM_CHECK(plan.GetLifetimes()[a.Index].Allocation != plan.GetLifetimes()[b.Index].Allocation);
	SWIM_CHECK(plan.GetLifetimes()[c.Index].Allocation != plan.GetLifetimes()[a.Index].Allocation);
}

SWIM_TEST("RenderGraph.Compile", "ImportedWritesAreSideEffectsAndUndefinedImportsNeedFinalState")
{
	Testing::MockDevice device;
	auto buffer = device.CreateBuffer(BufferDesc());
	RenderGraph graph;
	auto imported = graph.ImportBuffer(*buffer, S::Undefined);
	graph.AddPass(
		"persistent write", Q::Compute,
		[&](auto& p)
		{
			p.Write(imported, S::ShaderWrite);
		},
		noCommands);
	SWIM_CHECK_THROWS(graph.Compile(), std::invalid_argument);
	graph.Export(imported, S::ShaderRead);
	SWIM_CHECK_EQUAL(graph.Compile().GetSchedule().size(), 1u);
	RenderGraph restore;
	auto known = restore.ImportBuffer(*buffer, S::ShaderRead);
	restore.AddPass(
		"update", Q::Compute,
		[&](auto& p)
		{
			p.ReadWrite(known, S::ShaderRead | S::ShaderWrite);
		},
		noCommands);
	auto plan = restore.Compile();
	SWIM_REQUIRE_EQUAL(plan.GetSchedule().size(), 1u);
	SWIM_CHECK_EQUAL(plan.GetFinalBarriers()[0].After, S::ShaderRead);
}

SWIM_TEST("RenderGraph.Compile", "RejectsHazardContradictingExplicitOrder")
{
	RenderGraph graph;
	auto a = graph.CreateBuffer(BufferDesc());
	auto write = graph.AddPass(
		"writer", Q::Compute,
		[&](auto& p)
		{
			p.Write(a, S::ShaderWrite);
		},
		noCommands);
	auto read = graph.AddPass(
		"reader", Q::Compute,
		[&](auto& p)
		{
			p.Read(a, S::ShaderRead);
			p.SideEffect();
		},
		noCommands);
	graph.AddDependency(write, read);
	SWIM_CHECK_THROWS(graph.Compile(), std::invalid_argument);
}

SWIM_TEST("RenderGraph.Compile", "CompiledSnapshotOwnsNamesAndSurvivesGraphDestruction")
{
	CompiledRenderGraph plan;
	{
		RenderGraph graph;
		std::string name = "owned resource name";
		auto desc = BufferDesc();
		desc.DebugName = name;
		auto buffer = graph.CreateBuffer(desc);
		name.assign(100, 'x');
		graph.AddPass(
			std::string("owned pass"), Q::Compute,
			[&](auto& p)
			{
				p.Write(buffer, S::ShaderWrite);
			},
			noCommands);
		graph.Export(buffer, S::ShaderRead);
		plan = graph.Compile();
		graph.AddPass(
			"later mutation", Q::Graphics,
			[](auto& p)
			{
				p.SideEffect();
			},
			noCommands);
	}
	SWIM_CHECK_EQUAL(plan.GetSchedule().size(), 1u);
	SWIM_CHECK(plan.Dump().find("owned resource name") != std::string::npos);
	SWIM_CHECK(plan.Dump().find("owned pass") != std::string::npos);
	SWIM_CHECK(plan.Dump().find("later mutation") == std::string::npos);
}
