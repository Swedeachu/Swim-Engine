#include "Engine/Systems/Renderer/Resources/StreamingBuffer.h"
#include "Tests/Fixtures/RhiFrameCapture.h"
#include "Tests/Framework/Test.h"

#include <cstddef>
#include <vector>

using namespace Swim;
using namespace Swim::Render;

// A graph that is abandoned instead of executed sent nothing to the GPU: its writes are queued
// again and its moves are copied by the next Import, while the offsets already reported stay.
SWIM_TEST("Render.StreamingBuffer", "AnAbandonedGraphRequeuesItsWritesAndHandsOnItsMoves")
{
	Testing::MockDevice device;
	StreamingBufferDesc desc;
	desc.Capacity = 64;
	desc.DebugName = "streamed";
	StreamingBuffer streaming(device, desc);
	std::vector<std::uint64_t> relocations;
	streaming.SetRelocationCallback(
		[&](StreamingRange, std::uint64_t, std::uint64_t to)
		{
			relocations.push_back(to);
		});

	const auto first = *streaming.Allocate(16);
	const auto second = *streaming.Allocate(16);
	const auto third = *streaming.Allocate(16);
	const std::vector<std::byte> bytes(16, std::byte{ 7 });
	streaming.Write(second, bytes);
	streaming.Write(third, bytes);
	// Holes at 0 and 48: the next Import moves the top range down to 0.
	streaming.Free(first);

	{
		RenderGraph graph;
		streaming.Import(graph, 1024);
		SWIM_CHECK(streaming.IsUploaded(second));
		SWIM_CHECK_EQUAL(streaming.GetStats().Moves, 1u);
		SWIM_CHECK_EQUAL(streaming.GetOffset(third), std::uint64_t{ 0 });
		graph.Abandon();
	}

	SWIM_CHECK(!streaming.IsUploaded(second));
	SWIM_CHECK(!streaming.IsUploaded(third));
	SWIM_CHECK_EQUAL(streaming.GetStats().PendingUploadBytes, std::uint64_t{ 32 });
	SWIM_CHECK_EQUAL(streaming.GetOffset(third), std::uint64_t{ 0 });
	SWIM_CHECK_EQUAL(relocations.size(), std::size_t{ 1 });

	// No moves of its own this time: the one the abandoned graph never made is made now.
	streaming.SetDefragmentBudget(0);
	{
		RenderGraph graph;
		streaming.Import(graph, 1024);
		SWIM_CHECK_EQUAL(streaming.GetStats().Moves, 1u);
		SWIM_CHECK_EQUAL(streaming.GetStats().UploadedBytes, std::uint64_t{ 32 });
		SWIM_CHECK(streaming.IsUploaded(second));
		SWIM_CHECK(streaming.IsUploaded(third));
		graph.Compile();
	}

	// Once a graph took the copies, they are not made again; a freed range's never are.
	{
		RenderGraph graph;
		streaming.Import(graph, 1024);
		SWIM_CHECK_EQUAL(streaming.GetStats().Moves, 0u);
		graph.Abandon();
	}

	{
		RenderGraph graph;
		streaming.Import(graph, 1024);
		SWIM_CHECK_EQUAL(streaming.GetStats().Moves, 0u);
	}

	SWIM_CHECK_EQUAL(relocations.size(), std::size_t{ 1 });
}

// A range freed between the abandoned Import and the next one: its writes and moves are dropped.
SWIM_TEST("Render.StreamingBuffer", "WritesAndMovesOfRangesFreedAfterAnAbandonedGraphAreDropped")
{
	Testing::MockDevice device;
	StreamingBufferDesc desc;
	desc.Capacity = 64;
	desc.DebugName = "streamed";
	StreamingBuffer streaming(device, desc);
	const auto first = *streaming.Allocate(16);
	streaming.Allocate(16);
	const auto third = *streaming.Allocate(16);
	streaming.Write(third, std::vector<std::byte>(16, std::byte{ 1 }));
	streaming.Free(first);

	{
		RenderGraph graph;
		streaming.Import(graph, 1024);
		SWIM_CHECK_EQUAL(streaming.GetStats().Moves, 1u);
		streaming.Free(third);
		graph.Abandon();
	}

	SWIM_CHECK_EQUAL(streaming.GetStats().PendingUploadBytes, std::uint64_t{ 0 });
	streaming.SetDefragmentBudget(0);
	RenderGraph graph;
	streaming.Import(graph, 1024);
	SWIM_CHECK_EQUAL(streaming.GetStats().Moves, 0u);
	SWIM_CHECK_EQUAL(streaming.GetStats().UploadedBytes, std::uint64_t{ 0 });
}
