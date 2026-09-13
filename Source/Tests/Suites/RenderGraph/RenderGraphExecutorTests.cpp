#include "Engine/Systems/Renderer/RenderGraph/RenderGraph.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraphExecutor.h"
#include "Tests/Framework/Test.h"
#include "Tests/Fixtures/RhiFrameCapture.h"

using namespace Swim;
using namespace Swim::Render;
using S = Rhi::ResourceState;
using Q = Rhi::QueueType;

SWIM_TEST("RenderGraph.Execute", "WaitsBeforePoolingAndKeepsExportsUntilReuse")
{
	Testing::MockDevice device;
	RenderGraph graph;
	auto buffer = graph.CreateBuffer({ 64, Rhi::BufferUsage::Storage, Rhi::MemoryPreference::DeviceLocal, {} });
	unsigned calls = 0;
	graph.AddPass(
		"compute", Q::Compute,
		[&](auto& b)
		{
			b.Write(buffer, S::ShaderWrite);
		},
		[&](auto& c)
		{
			++calls;
			SWIM_CHECK_EQUAL(c.Get(buffer).GetDesc().Size, 64u);
		});
	graph.Export(buffer, S::ShaderRead);
	RenderGraphExecutor executor(device);
	auto plan = graph.Compile();
	auto first = executor.Execute(plan);
	auto* resource = &executor.GetExported(buffer);
	SWIM_CHECK_EQUAL(first.Value, 1u);
	SWIM_CHECK_EQUAL(device.LastTimeline->WaitCount, 0u);
	device.LastTimeline->FailWait = true;
	SWIM_CHECK_THROWS(executor.Execute(plan), std::runtime_error);
	SWIM_CHECK_EQUAL(calls, 1u);
	SWIM_CHECK_EQUAL(device.BufferCreateCount, 1u);
	device.LastTimeline->FailWait = false;
	auto second = executor.Execute(plan);
	SWIM_CHECK_EQUAL(second.Value, 2u);
	SWIM_CHECK_EQUAL(&executor.GetExported(buffer), resource);
	SWIM_CHECK_EQUAL(device.BufferCreateCount, 1u);
	SWIM_CHECK_EQUAL(calls, 2u);
	SWIM_CHECK_EQUAL(executor.GetPooledResourceCount(), 1u);
	auto timings = executor.ReadTimings();
	SWIM_REQUIRE_EQUAL(timings.size(), 1u);
	SWIM_CHECK_EQUAL(timings[0].Name, "compute");
	SWIM_CHECK(!timings[0].Nanoseconds);
	executor.Trim();
	SWIM_CHECK_EQUAL(executor.GetPooledResourceCount(), 0u);
	SWIM_CHECK_THROWS(executor.GetExported(buffer), std::logic_error);
}

SWIM_TEST("RenderGraph.Execute", "FailedRecordOrSubmitDoesNotAdvanceCompletionAndCanRetry")
{
	Testing::MockDevice device;
	RenderGraph graph;
	auto buffer = graph.CreateBuffer({ 64, Rhi::BufferUsage::Storage, Rhi::MemoryPreference::DeviceLocal, {} });
	bool fail = true;
	graph.AddPass(
		"failable", Q::Compute,
		[&](auto& b)
		{
			b.Write(buffer, S::ShaderWrite);
		},
		[&](auto&)
		{
			if (fail)
			{
				throw std::runtime_error("record failure");
			}
		});
	graph.Export(buffer, S::ShaderRead);
	RenderGraphExecutor executor(device);
	auto plan = graph.Compile();
	SWIM_CHECK_THROWS(executor.Execute(plan), std::runtime_error);
	SWIM_CHECK_EQUAL(device.queue.SubmitCount, 0u);
	fail = false;
	device.queue.FailSubmit = true;
	SWIM_CHECK_THROWS(executor.Execute(plan), std::runtime_error);
	SWIM_CHECK_THROWS(executor.GetExported(buffer), std::logic_error);
	device.queue.FailSubmit = false;
	SWIM_CHECK_EQUAL(executor.Execute(plan).Value, 1u);
	SWIM_CHECK_EQUAL(device.queue.SubmitCount, 1u);
}

SWIM_TEST("RenderGraph.Execute", "RejectsUndeclaredResourceAccessAndExecutorReentry")
{
	Testing::MockDevice device;
	RenderGraph graph;
	auto used = graph.CreateBuffer({ 64, Rhi::BufferUsage::Storage, Rhi::MemoryPreference::DeviceLocal, {} });
	auto hidden = graph.CreateBuffer({ 64, Rhi::BufferUsage::Storage, Rhi::MemoryPreference::DeviceLocal, {} });
	RenderGraphExecutor executor(device);
	graph.AddPass(
		"checks", Q::Compute,
		[&](auto& b)
		{
			b.Write(used, S::ShaderWrite);
		},
		[&](auto& c)
		{
			SWIM_CHECK_THROWS(c.Get(hidden), std::invalid_argument);
			SWIM_CHECK_THROWS(executor.Trim(), std::logic_error);
			SWIM_CHECK_THROWS(c.Retain(nullptr), std::invalid_argument);
		});
	graph.Export(used, S::ShaderRead);
	executor.Execute(graph.Compile());
	SWIM_CHECK_EQUAL(device.BufferCreateCount, 1u);
	SWIM_CHECK_THROWS(executor.GetExported(hidden), std::invalid_argument);
}

SWIM_TEST("RenderGraph.Execute", "AllocationFailureIsRecoverableAndDeadWorkNeverExecutes")
{
	Testing::MockDevice device;
	RenderGraph graph;
	auto used = graph.CreateBuffer({ 64, Rhi::BufferUsage::Storage, Rhi::MemoryPreference::DeviceLocal, {} });
	auto dead = graph.CreateBuffer({ 128, Rhi::BufferUsage::Storage, Rhi::MemoryPreference::DeviceLocal, {} });
	graph.AddPass(
		"live", Q::Compute,
		[&](auto& b)
		{
			b.Write(used, S::ShaderWrite);
		},
		[](auto&)
		{
		});
	graph.AddPass(
		"dead", Q::Compute,
		[&](auto& b)
		{
			b.Write(dead, S::ShaderWrite);
		},
		[](auto&)
		{
			SWIM_FAIL("Culled pass executed");
		});
	graph.Export(used, S::ShaderRead);
	RenderGraphExecutor executor(device);
	device.FailBufferCreate = 1;
	SWIM_CHECK_THROWS(executor.Execute(graph.Compile()), std::runtime_error);
	device.FailBufferCreate = 0;
	executor.Execute(graph.Compile());
	SWIM_CHECK_EQUAL(executor.GetPooledResourceCount(), 1u);
	SWIM_CHECK_EQUAL(device.queue.SubmitCount, 1u);
}
