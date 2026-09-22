#include "Engine/Systems/Renderer/Geometry/GeometryHeap.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraph.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraphExecutor.h"
#include "Tests/Framework/Test.h"
#include "Tests/Fixtures/RhiFrameCapture.h"
#include <cstring>

using namespace Swim;
using namespace Swim::Render;

namespace
{
	std::vector<std::byte> Pattern(std::size_t size, unsigned seed)
	{
		std::vector<std::byte> bytes(size);
		for (std::size_t i = 0; i < size; ++i)
		{
			bytes[i] = static_cast<std::byte>((i * 5 + seed * 31 + 1) & 0xff);
		}
		return bytes;
	}

	GeometryHeapDesc SmallHeap()
	{
		GeometryHeapDesc desc;
		desc.VertexPageSize = 4096;
		desc.IndexPageSize = 2048;
		desc.MeshletPageSize = 1024;
		desc.MaxMeshes = 8;
		desc.MaxPages = 8;
		return desc;
	}

	struct MeshData
	{
		std::vector<std::byte> Vertices;
		std::vector<std::byte> Indices;
		std::vector<std::byte> Meshlets;

		GeometryMeshDesc Desc(std::uint32_t stride = 12, Rhi::IndexType format = Rhi::IndexType::Uint32) const
		{
			GeometryMeshDesc desc;
			desc.VertexLayout = 7;
			desc.VertexStride = stride;
			desc.Vertices = Vertices;
			desc.IndexFormat = format;
			desc.Indices = Indices;
			desc.Meshlets = Meshlets;
			desc.MeshletCount = Meshlets.empty() ? 0 : 2;
			return desc;
		}
	};

	MeshData Mesh(std::size_t vertices, std::size_t indices, unsigned seed, std::size_t meshlets = 0)
	{
		return { Pattern(vertices, seed), Pattern(indices, seed + 1), Pattern(meshlets, seed + 2) };
	}

	Testing::MockMappedBuffer& Host(Rhi::Buffer* buffer)
	{
		return *static_cast<Testing::MockMappedBuffer*>(buffer);
	}

	bool Contains(Rhi::Buffer* buffer, std::uint64_t offset, const std::vector<std::byte>& expected)
	{
		const auto& bytes = Host(buffer).Bytes;
		return offset + expected.size() <= bytes.size() && std::memcmp(bytes.data() + offset, expected.data(), expected.size()) == 0;
	}

	// Imports the heap into a fresh graph, executes it and commits the uploads.
	Rhi::TimelinePoint Upload(GeometryHeap& heap, RenderGraphExecutor& executor, GeometryGraphResources* resources = nullptr)
	{
		RenderGraph graph;
		auto imported = heap.Import(graph);
		if (resources)
		{
			*resources = imported;
		}
		const auto completion = executor.Execute(graph.Compile());
		heap.CommitUploads(completion);
		return completion;
	}
} // namespace

SWIM_TEST("Render.GeometryHeap", "CreatesStableMetadataRowsInSharedPages")
{
	Testing::MockDevice device;
	GeometryHeap heap(device, SmallHeap());
	SWIM_CHECK_EQUAL(device.BufferCreateCount, 1u); // Metadata only; pages are lazy.
	SWIM_CHECK_EQUAL(heap.GetMetadataBuffer().GetDesc().Size, 8u * sizeof(GpuMeshMetadata));

	const auto first = Mesh(36, 24, 1);
	const auto second = Mesh(120, 12, 2, 64);
	std::array<GeometryLodRange, 2> lods{ { { 0, 6, 0.0f }, { 3, 3, 0.5f } } };
	auto firstDesc = first.Desc();
	firstDesc.Lods = lods;
	auto a = heap.CreateMesh(firstDesc);
	auto b = heap.CreateMesh(second.Desc(12, Rhi::IndexType::Uint16));
	SWIM_CHECK_EQUAL(heap.GetPageCount(), 3u); // Vertex, index, meshlet.

	const auto* rowA = heap.GetMetadata(a);
	const auto* rowB = heap.GetMetadata(b);
	SWIM_REQUIRE(rowA && rowB);
	SWIM_CHECK_EQUAL(rowA->VertexCount, 3u);
	SWIM_CHECK_EQUAL(rowA->VertexStride, 12u);
	SWIM_CHECK_EQUAL(rowA->VertexLayout, 7u);
	SWIM_CHECK_EQUAL(rowA->IndexCount, 6u);
	SWIM_CHECK_EQUAL(rowA->IndexBytes, 4u);
	SWIM_CHECK_EQUAL(rowA->LodCount, 2u);
	SWIM_CHECK_EQUAL(rowA->Lods[1].FirstIndex, rowA->FirstIndex + 3);
	SWIM_CHECK_EQUAL(rowA->Lods[1].IndexCount, 3u);
	SWIM_CHECK_EQUAL(rowA->Generation, a.Generation);
	SWIM_CHECK_EQUAL(rowA->MeshletPage, GpuMeshMetadata::InvalidPage);

	SWIM_CHECK_EQUAL(rowB->VertexPage, rowA->VertexPage);
	SWIM_CHECK(rowB->VertexOffset >= rowA->VertexCount); // Whole-vertex, non-overlapping placement.
	SWIM_CHECK_EQUAL(rowB->IndexBytes, 2u);
	SWIM_CHECK_EQUAL(rowB->IndexCount, 6u);
	SWIM_CHECK_EQUAL(rowB->LodCount, 1u);
	SWIM_CHECK_EQUAL(rowB->Lods[0].IndexCount, 6u);
	SWIM_CHECK_EQUAL(rowB->MeshletCount, 2u);
	SWIM_CHECK_EQUAL(rowB->MeshletOffset % 16, 0u);
	SWIM_CHECK(heap.GetResidency(a) == GeometryResidency::PendingUpload);

	const auto stats = heap.GetStats();
	SWIM_CHECK_EQUAL(stats.PendingMeshes, 2u);
	SWIM_CHECK_EQUAL(stats.PendingUploadBytes, 36u + 24u + 120u + 12u + 64u);
	SWIM_CHECK_EQUAL(stats.DirtyMetadataRows, 2u);
	SWIM_CHECK_EQUAL(stats.Vertex.Pages, 1u);
	SWIM_CHECK_EQUAL(stats.Vertex.ReservedBytes, 4096u);
	SWIM_CHECK_EQUAL(stats.Vertex.AllocatedBytes, 156u);
}

SWIM_TEST("Render.GeometryHeap", "GraphUploadsBatchPerPageAndBecomeResidentAfterCompletion")
{
	Testing::MockDevice device;
	GeometryHeap heap(device, SmallHeap());
	RenderGraphExecutor executor(device);
	const auto first = Mesh(48, 12, 3);
	const auto second = Mesh(24, 8, 4, 32);
	auto a = heap.CreateMesh(first.Desc());
	auto b = heap.CreateMesh(second.Desc(8, Rhi::IndexType::Uint16));

	GeometryGraphResources resources;
	const auto completion = Upload(heap, executor, &resources);
	SWIM_CHECK_EQUAL(resources.RecordedMeshes, 2u);
	SWIM_CHECK_EQUAL(resources.Pages.size(), 3u);
	SWIM_CHECK_EQUAL(resources.UploadPasses.size(), 4u); // Three pages + metadata.
	SWIM_CHECK_EQUAL(resources.RecordedBytes, 48u + 12u + 24u + 8u + 32u + 2u * sizeof(GpuMeshMetadata));
	SWIM_CHECK(heap.GetResidency(a) == GeometryResidency::Uploading);
	SWIM_CHECK_EQUAL(heap.GetStats().PendingUploadBytes, 0u);
	SWIM_CHECK_EQUAL(heap.GetStats().DirtyMetadataRows, 0u);

	// Upload bytes landed at the metadata-described offsets.
	const auto* rowA = heap.GetMetadata(a);
	const auto* rowB = heap.GetMetadata(b);
	SWIM_CHECK(Contains(heap.GetPage(rowA->VertexPage), std::uint64_t(rowA->VertexOffset) * 12, first.Vertices));
	SWIM_CHECK(Contains(heap.GetPage(rowA->IndexPage), std::uint64_t(rowA->FirstIndex) * 4, first.Indices));
	SWIM_CHECK(Contains(heap.GetPage(rowB->VertexPage), std::uint64_t(rowB->VertexOffset) * 8, second.Vertices));
	SWIM_CHECK(Contains(heap.GetPage(rowB->IndexPage), std::uint64_t(rowB->FirstIndex) * 2, second.Indices));
	SWIM_CHECK(Contains(heap.GetPage(rowB->MeshletPage), rowB->MeshletOffset, second.Meshlets));
	GpuMeshMetadata uploaded;
	std::memcpy(&uploaded, Host(&heap.GetMetadataBuffer()).Bytes.data() + b.Index * sizeof(GpuMeshMetadata), sizeof(uploaded));
	SWIM_CHECK_EQUAL(uploaded.VertexOffset, rowB->VertexOffset);
	SWIM_CHECK_EQUAL(uploaded.Generation, b.Generation);

	SWIM_CHECK_EQUAL(heap.Collect(), 0u); // Not complete yet.
	executor.Wait();
	SWIM_CHECK_EQUAL(heap.Collect(), 2u);
	SWIM_CHECK(heap.GetResidency(a) == GeometryResidency::Resident);
	SWIM_CHECK_EQUAL(heap.GetStats().ResidentMeshes, 2u);
	SWIM_CHECK_EQUAL(completion.Value, 1u);

	// Nothing pending: the next import records no passes and needs no commit.
	RenderGraph idle;
	auto quiet = heap.Import(idle);
	SWIM_CHECK(quiet.UploadPasses.empty());
	SWIM_CHECK(quiet.Metadata.Graph != 0);
	heap.CommitUploads({});
}

SWIM_TEST("Render.GeometryHeap", "AbortReRecordsAndPendingImportIsExclusive")
{
	Testing::MockDevice device;
	GeometryHeap heap(device, SmallHeap());
	const auto data = Mesh(24, 12, 5);
	auto mesh = heap.CreateMesh(data.Desc());
	{
		RenderGraph graph;
		auto resources = heap.Import(graph);
		SWIM_CHECK_EQUAL(resources.RecordedMeshes, 1u);
		SWIM_CHECK(heap.GetResidency(mesh) == GeometryResidency::Recorded);
		RenderGraph other;
		SWIM_CHECK_THROWS(heap.Import(other), std::logic_error);
		SWIM_CHECK_THROWS(heap.DestroyMesh(mesh), std::logic_error);
		SWIM_CHECK_THROWS(heap.CommitUploads({}), std::invalid_argument);
		heap.AbortUploads();
	}
	SWIM_CHECK(heap.GetResidency(mesh) == GeometryResidency::PendingUpload);
	SWIM_CHECK_EQUAL(heap.GetStats().DirtyMetadataRows, 1u);
	SWIM_CHECK_EQUAL(heap.GetStats().PendingUploadBytes, 36u);

	RenderGraphExecutor executor(device);
	Upload(heap, executor);
	executor.Wait();
	heap.Collect();
	const auto* row = heap.GetMetadata(mesh);
	SWIM_CHECK(Contains(heap.GetPage(row->VertexPage), std::uint64_t(row->VertexOffset) * 12, data.Vertices));
	SWIM_CHECK(heap.GetResidency(mesh) == GeometryResidency::Resident);
}

SWIM_TEST("Render.GeometryHeap", "DestroyedRangesAndRowsAreReusedOnlyAfterRetirement")
{
	Testing::MockDevice device;
	GeometryHeap heap(device, SmallHeap());
	RenderGraphExecutor executor(device);
	const auto data = Mesh(96, 48, 6);
	auto mesh = heap.CreateMesh(data.Desc());
	const auto original = *heap.GetMetadata(mesh);
	const auto upload = Upload(heap, executor);

	// Destroying mid-upload with no explicit point retires after the upload.
	SWIM_CHECK(heap.DestroyMesh(mesh));
	SWIM_CHECK(!heap.IsValid(mesh));
	SWIM_CHECK(heap.GetResidency(mesh) == GeometryResidency::Invalid);
	SWIM_CHECK(!heap.DestroyMesh(mesh));
	SWIM_CHECK_EQUAL(heap.GetStats().RetiringMeshes, 1u);
	SWIM_CHECK_EQUAL(heap.GetStats().DirtyMetadataRows, 1u); // Cleared row queued.

	auto replacement = heap.CreateMesh(data.Desc());
	SWIM_CHECK(replacement.Index != mesh.Index);
	SWIM_CHECK(heap.GetMetadata(replacement)->VertexOffset != original.VertexOffset);
	heap.DestroyMesh(replacement); // Never uploaded: retires at the next collection.

	SWIM_CHECK_EQUAL(heap.Collect(), 1u); // Replacement only; the upload is still in flight.
	SWIM_CHECK_EQUAL(heap.GetStats().RetiringMeshes, 1u);
	static_cast<Testing::MockTimeline*>(upload.Semaphore)->Complete(upload.Value);
	SWIM_CHECK_EQUAL(heap.Collect(), 1u);
	SWIM_CHECK_EQUAL(heap.GetStats().RetiringMeshes, 0u);
	SWIM_CHECK_EQUAL(heap.GetStats().Vertex.AllocatedBytes, 0u);
	SWIM_CHECK_EQUAL(heap.GetStats().Vertex.FreeRanges, 1u);

	auto reused = heap.CreateMesh(data.Desc());
	// Rows recycle in retirement order: the replacement retired first.
	SWIM_CHECK_EQUAL(reused.Index, replacement.Index);
	SWIM_CHECK_EQUAL(reused.Generation, replacement.Generation + 1);
	SWIM_CHECK_EQUAL(heap.CreateMesh(Mesh(12, 0, 1).Desc()).Index, mesh.Index);
	SWIM_CHECK_EQUAL(heap.GetMetadata(reused)->VertexOffset, original.VertexOffset);
	SWIM_CHECK(heap.GetMetadata(mesh) == nullptr);

	// An explicit later point on the same timeline is honored as-is.
	Upload(heap, executor);
	executor.Wait();
	heap.Collect();
	Testing::MockTimeline frame;
	SWIM_CHECK(heap.DestroyMesh(reused, { &frame, 4 }));
	frame.Complete(3);
	heap.Collect();
	SWIM_CHECK_EQUAL(heap.GetStats().RetiringMeshes, 1u);
	heap.Drain();
	SWIM_CHECK_EQUAL(heap.GetStats().RetiringMeshes, 0u);
	SWIM_CHECK_EQUAL(frame.GetCompletedValue(), 4u);
}

SWIM_TEST("Render.GeometryHeap", "OversizedStreamsUseDedicatedPagesReleasedOnRetirement")
{
	Testing::MockDevice device;
	GeometryHeap heap(device, SmallHeap());
	const auto big = Mesh(6000, 12, 7); // Larger than the 4 KiB vertex page.
	auto mesh = heap.CreateMesh(big.Desc());
	const auto page = heap.GetMetadata(mesh)->VertexPage;
	SWIM_REQUIRE(heap.GetPage(page) != nullptr);
	SWIM_CHECK_EQUAL(heap.GetPage(page)->GetDesc().Size, 6000u);
	SWIM_CHECK_EQUAL(heap.GetStats().Vertex.DedicatedPages, 1u);
	SWIM_CHECK_EQUAL(heap.GetMetadata(mesh)->VertexOffset, 0u);

	heap.DestroyMesh(mesh);
	heap.Collect();
	SWIM_CHECK(heap.GetPage(page) == nullptr);
	SWIM_CHECK_EQUAL(heap.GetStats().Vertex.DedicatedPages, 0u);

	RenderGraph graph;
	auto resources = heap.Import(graph);
	SWIM_CHECK(!resources.Pages[page].Graph); // Released slot is not imported.
	heap.AbortUploads();

	auto next = heap.CreateMesh(Mesh(8004, 12, 8).Desc());
	SWIM_CHECK_EQUAL(heap.GetMetadata(next)->VertexPage, page); // Page slot reused.
}

SWIM_TEST("Render.GeometryHeap", "ValidatesInputAndFailsWithoutPartialState")
{
	Testing::MockDevice device;
	SWIM_CHECK_THROWS(GeometryHeap(device, { 0, 1, 1, 1, 1 }), std::invalid_argument);
	SWIM_CHECK_THROWS(GeometryHeap(device, { 1ull << 33, 1, 1, 1, 1 }), std::invalid_argument);
	SWIM_CHECK_THROWS(GeometryHeap(device, { 1, 1, 1, 0, 1 }), std::invalid_argument);

	auto desc = SmallHeap();
	desc.MaxMeshes = 2;
	desc.MaxPages = 2;
	GeometryHeap heap(device, desc);
	const auto data = Mesh(24, 12, 9);
	auto invalidStride = data.Desc(0);
	SWIM_CHECK_THROWS(heap.CreateMesh(invalidStride), std::invalid_argument);
	SWIM_CHECK_THROWS(heap.CreateMesh(data.Desc(10)), std::invalid_argument); // 24 % 10 != 0.
	auto oddIndices = data.Desc();
	const auto odd = Pattern(6, 0);
	oddIndices.Indices = odd;
	SWIM_CHECK_THROWS(heap.CreateMesh(oddIndices), std::invalid_argument);
	auto badLod = data.Desc();
	const GeometryLodRange lod{ 2, 2, 0.0f };
	badLod.Lods = { &lod, 1 };
	SWIM_CHECK_THROWS(heap.CreateMesh(badLod), std::invalid_argument);
	auto meshletMismatch = data.Desc();
	meshletMismatch.MeshletCount = 3;
	SWIM_CHECK_THROWS(heap.CreateMesh(meshletMismatch), std::invalid_argument);

	// Page creation failure (the index page) rolls back the vertex allocation.
	device.FailBufferCreate = device.BufferCreateCount + 2;
	SWIM_CHECK_THROWS(heap.CreateMesh(data.Desc()), std::runtime_error);
	SWIM_CHECK_EQUAL(heap.GetStats().Vertex.AllocatedBytes, 0u);
	SWIM_CHECK_EQUAL(heap.GetStats().PendingMeshes, 0u);

	heap.CreateMesh(data.Desc());
	// Meshlet stream needs a third page: the page limit rejects it without leaks.
	const auto withMeshlets = Mesh(24, 12, 10, 16);
	SWIM_CHECK_THROWS(heap.CreateMesh(withMeshlets.Desc()), std::length_error);
	SWIM_CHECK_EQUAL(heap.GetStats().Vertex.AllocatedBytes, 24u);
	heap.CreateMesh(data.Desc());
	SWIM_CHECK_THROWS(heap.CreateMesh(data.Desc()), std::length_error); // Mesh slots exhausted.
	SWIM_CHECK_EQUAL(heap.GetStats().Vertex.AllocatedBytes, 48u);
}

SWIM_TEST("Render.GeometryHeap", "ReportsFragmentationAcrossPages")
{
	Testing::MockDevice device;
	auto desc = SmallHeap();
	desc.VertexPageSize = 256;
	GeometryHeap heap(device, desc);
	std::vector<GpuMeshHandle> meshes;
	for (unsigned i = 0; i < 4; ++i)
	{
		GeometryMeshDesc mesh;
		const auto vertices = Pattern(64, i);
		mesh.VertexStride = 16;
		mesh.Vertices = vertices;
		meshes.push_back(heap.CreateMesh(mesh));
	}
	SWIM_CHECK_EQUAL(heap.GetStats().Vertex.Pages, 1u);
	SWIM_CHECK_EQUAL(heap.GetStats().Vertex.FreeBytes, 0u);
	SWIM_CHECK_EQUAL(heap.GetStats().Vertex.Fragmentation, 0.0);
	heap.DestroyMesh(meshes[0]);
	heap.DestroyMesh(meshes[2]);
	heap.Collect();
	const auto stats = heap.GetStats().Vertex;
	SWIM_CHECK_EQUAL(stats.FreeBytes, 128u);
	SWIM_CHECK_EQUAL(stats.LargestFreeRange, 64u);
	SWIM_CHECK_EQUAL(stats.FreeRanges, 2u);
	SWIM_CHECK_EQUAL(stats.Fragmentation, 0.5);
	const auto* row = heap.GetMetadata(meshes[1]);
	SWIM_CHECK_EQUAL(row->IndexPage, GpuMeshMetadata::InvalidPage); // Non-indexed mesh.
	SWIM_CHECK_EQUAL(row->LodCount, 0u);

	// A 128-byte request cannot use the fragmented holes and opens a second page.
	GeometryMeshDesc large;
	const auto vertices = Pattern(128, 9);
	large.VertexStride = 16;
	large.Vertices = vertices;
	heap.CreateMesh(large);
	SWIM_CHECK_EQUAL(heap.GetStats().Vertex.Pages, 2u);
}
