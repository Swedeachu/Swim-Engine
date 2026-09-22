#include "Engine/Platform/PlatformSystem.h"
#include "Engine/Systems/Renderer/Geometry/GeometryHeap.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/VulkanRhiBackend.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraphExecutor.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraphTransfers.h"
#include "Tests/Fixtures/VulkanSmokeDiagnostics.h"
#include "Tests/Framework/Test.h"
#include <cstdlib>

namespace
{
	std::vector<std::byte> Bytes(std::size_t size, std::uint32_t seed)
	{
		std::vector<std::byte> result(size);
		for (std::size_t i = 0; i < size; ++i)
		{
			result[i] = static_cast<std::byte>((i * 13 + seed * 29 + 1) & 0xff);
		}
		return result;
	}

	// Graph-scheduled upload -> device buffer/texture -> readback, with partial
	// preserving updates, executor-owned staging growth and repeated reuse.
	void RunGraphTransferSmoke(const Swim::Rhi::GraphicsSystemDesc& graphicsDesc)
	{
		using namespace Swim;
		using namespace Swim::Render;
		using S = Rhi::ResourceState;

		Platform::PlatformSystem platform;
		SWIM_REQUIRE(platform.Initialize());
		auto graphics = RhiVulkan::CreateGraphicsSystem(graphicsDesc);
		SWIM_REQUIRE(graphics);
		Testing::RequireVulkanSmokeValidation(*graphics, graphicsDesc.Checks);
		auto device = graphics->GetAdapter(0).CreateDevice();
		SWIM_REQUIRE(device);

		constexpr std::uint64_t persistentBytes = 4096;
		auto persistent = device->CreateBuffer(
			{ persistentBytes, Rhi::BufferUsage::TransferSource | Rhi::BufferUsage::TransferDestination | Rhi::BufferUsage::Storage,
				Rhi::MemoryPreference::DeviceLocal, "Graph transfer persistent" });
		SWIM_REQUIRE(persistent);

		Rhi::TextureDesc textureDesc;
		textureDesc.Extent = { 16, 8, 1 };
		textureDesc.PixelFormat = Rhi::Format::RGBA8Unorm;
		textureDesc.Usage = Rhi::TextureUsage::TransferSource | Rhi::TextureUsage::TransferDestination | Rhi::TextureUsage::Sampled;
		textureDesc.DebugName = "Graph transfer texture";

		RenderGraphExecutor executor(*device);
		std::vector<std::byte> persistentShadow(persistentBytes);
		for (std::uint32_t frame = 0; frame < 4; ++frame)
		{
			RenderGraph graph;
			auto imported = graph.ImportBuffer(*persistent, frame ? S::ShaderRead : S::Undefined);
			if (frame == 0)
			{
				const auto initial = Bytes(persistentBytes, 100);
				AddBufferUpload(graph, "Initialize persistent", initial, imported);
				persistentShadow = initial;
			}
			const auto patch = Bytes(64 + frame * 16, frame);
			const std::uint64_t patchOffset = 256 * (frame + 1);
			AddBufferUpload(graph, "Patch persistent", patch, imported, patchOffset);
			std::copy(patch.begin(), patch.end(), persistentShadow.begin() + static_cast<std::ptrdiff_t>(patchOffset));
			graph.Export(imported, S::ShaderRead);
			auto bufferResult = AddBufferReadback(graph, "Read persistent", imported, 0, persistentBytes);

			// Frame 2 carries a large transient payload to force staging growth.
			const std::uint64_t largeBytes = frame == 2 ? 192 * 1024 : 1024;
			auto large = graph.CreateBuffer({ largeBytes, Rhi::BufferUsage::TransferSource | Rhi::BufferUsage::TransferDestination,
				Rhi::MemoryPreference::DeviceLocal, "Graph transfer large" });
			const auto largePayload = Bytes(static_cast<std::size_t>(largeBytes), frame + 7);
			AddBufferUpload(graph, "Upload large", largePayload, large);
			auto largeResult = AddBufferReadback(graph, "Read large tail", large, largeBytes - 128, 128);

			auto texture = graph.CreateTexture(textureDesc);
			const auto base = Bytes(16 * 8 * 4, frame + 40);
			const auto corner = Bytes(4 * 2 * 4, frame + 80);
			Rhi::BufferTextureCopyRegion whole{};
			whole.Extent = textureDesc.Extent;
			Rhi::BufferTextureCopyRegion patchRegion{};
			patchRegion.TextureOffset = { 5, 3, 0 };
			patchRegion.Extent = { 4, 2, 1 };
			AddTextureUpload(graph, "Upload texture", base, texture, whole);
			AddTextureUpload(graph, "Patch texture", corner, texture, patchRegion);
			auto textureResult = AddTextureReadback(graph, "Read texture", texture, whole);

			executor.Execute(graph.Compile());
			executor.Wait();

			std::vector<std::byte> actual(persistentBytes);
			SWIM_REQUIRE(executor.TryReadback(bufferResult.Buffer, actual) == Rhi::ReadbackStatus::Ready);
			SWIM_CHECK(actual == persistentShadow);

			std::vector<std::byte> tail(128);
			SWIM_REQUIRE(executor.TryReadback(largeResult.Buffer, tail) == Rhi::ReadbackStatus::Ready);
			SWIM_CHECK(std::equal(tail.begin(), tail.end(), largePayload.end() - 128));

			std::vector<std::byte> pixels(base.size());
			SWIM_REQUIRE(executor.TryReadback(textureResult.Buffer, pixels) == Rhi::ReadbackStatus::Ready);
			for (std::uint32_t y = 0; y < 8; ++y)
			{
				for (std::uint32_t x = 0; x < 16; ++x)
				{
					for (std::uint32_t c = 0; c < 4; ++c)
					{
						const auto i = (y * 16 + x) * 4 + c;
						const bool patched = x >= 5 && x < 9 && y >= 3 && y < 5;
						SWIM_CHECK(pixels[i] == (patched ? corner[((y - 3) * 4 + (x - 5)) * 4 + c] : base[i]));
					}
				}
			}
			if (frame >= 2)
			{
				SWIM_CHECK(executor.GetUploadCapacity() >= 256u * 1024u);
			}
		}
		executor.Trim();
	}

	// Paged GeometryHeap residency on a real device: batched graph uploads into
	// shared pages, metadata rows, same-graph readback verification, deferred
	// destruction and range reuse after the retiring submission completes.
	void RunGeometryHeapSmoke(const Swim::Rhi::GraphicsSystemDesc& graphicsDesc)
	{
		using namespace Swim;
		using namespace Swim::Render;

		Platform::PlatformSystem platform;
		SWIM_REQUIRE(platform.Initialize());
		auto graphics = RhiVulkan::CreateGraphicsSystem(graphicsDesc);
		SWIM_REQUIRE(graphics);
		Testing::RequireVulkanSmokeValidation(*graphics, graphicsDesc.Checks);
		auto device = graphics->GetAdapter(0).CreateDevice();
		SWIM_REQUIRE(device);

		GeometryHeapDesc heapDesc;
		heapDesc.VertexPageSize = 64 * 1024;
		heapDesc.IndexPageSize = 32 * 1024;
		heapDesc.MeshletPageSize = 16 * 1024;
		heapDesc.MaxMeshes = 64;
		heapDesc.MaxPages = 16;
		RenderGraphExecutor executor(*device);
		{
			GeometryHeap heap(*device, heapDesc);
			std::vector<GpuMeshHandle> meshes;
			std::vector<std::vector<std::byte>> vertices;
			std::vector<std::vector<std::byte>> indices;
			for (std::uint32_t round = 0; round < 3; ++round)
			{
				for (std::uint32_t i = 0; i < 4; ++i)
				{
					vertices.push_back(Bytes(24 * (8 + i + round), round * 4 + i));
					indices.push_back(Bytes(2 * 3 * (4 + i), round * 4 + i + 50));
					GeometryMeshDesc mesh;
					mesh.VertexStride = 24;
					mesh.Vertices = vertices.back();
					mesh.IndexFormat = Rhi::IndexType::Uint16;
					mesh.Indices = indices.back();
					meshes.push_back(heap.CreateMesh(mesh));
				}

				RenderGraph graph;
				auto resources = heap.Import(graph);
				std::vector<GraphReadback> checks;
				for (std::size_t m = meshes.size() - 4; m < meshes.size(); ++m)
				{
					const auto* row = heap.GetMetadata(meshes[m]);
					checks.push_back(AddBufferReadback(graph, "Verify vertices", resources.Pages[row->VertexPage],
						std::uint64_t(row->VertexOffset) * 24, vertices[m].size()));
					checks.push_back(AddBufferReadback(
						graph, "Verify indices", resources.Pages[row->IndexPage], std::uint64_t(row->FirstIndex) * 2, indices[m].size()));
				}
				auto metadataCheck = AddBufferReadback(graph, "Verify metadata", resources.Metadata,
					std::uint64_t(meshes.back().Index) * sizeof(GpuMeshMetadata), sizeof(GpuMeshMetadata));
				const auto completion = executor.Execute(graph.Compile());
				heap.CommitUploads(completion);
				executor.Wait();
				heap.Collect();

				for (std::size_t c = 0; c < checks.size(); ++c)
				{
					const auto m = meshes.size() - 4 + c / 2;
					const auto& expected = c % 2 ? indices[m] : vertices[m];
					std::vector<std::byte> actual(expected.size());
					SWIM_REQUIRE(executor.TryReadback(checks[c].Buffer, actual) == Rhi::ReadbackStatus::Ready);
					SWIM_CHECK(actual == expected);
				}
				GpuMeshMetadata row;
				SWIM_REQUIRE(
					executor.TryReadback(metadataCheck.Buffer, std::as_writable_bytes(std::span(&row, 1))) == Rhi::ReadbackStatus::Ready);
				SWIM_CHECK_EQUAL(row.Generation, meshes.back().Generation);
				SWIM_CHECK(heap.GetResidency(meshes.back()) == GeometryResidency::Resident);

				// Retire the oldest mesh of this round after the completed submission.
				heap.DestroyMesh(meshes[meshes.size() - 4], completion);
			}
			heap.Collect();
			SWIM_CHECK_EQUAL(heap.GetStats().RetiringMeshes, 0u);
			SWIM_CHECK_EQUAL(heap.GetStats().Vertex.Pages, 1u);
			heap.Drain();
		}
		executor.Trim();
	}

	[[maybe_unused]] const bool registeredGeometry = []
	{
		const char* enabled = std::getenv("SWIM_RUN_RHI_SMOKE");
		if (enabled && std::string_view(enabled) == "1")
		{
			Swim::Testing::TestRegistry::Get().Add({ "RHI.Vulkan.Smoke", "GeometryHeapPagedUploadAndRetirement", SWIM_TEST_LOCATION,
				+[]
				{
					Swim::Testing::RunValidatedVulkanSmoke(&RunGeometryHeapSmoke);
				} });
		}
		return true;
	}();

	[[maybe_unused]] const bool registered = []
	{
		const char* enabled = std::getenv("SWIM_RUN_RHI_SMOKE");
		if (enabled && std::string_view(enabled) == "1")
		{
			Swim::Testing::TestRegistry::Get().Add({ "RHI.Vulkan.Smoke", "RenderGraphStagedTransfersAndReadback", SWIM_TEST_LOCATION,
				+[]
				{
					Swim::Testing::RunValidatedVulkanSmoke(&RunGraphTransferSmoke);
				} });
		}
		return true;
	}();
} // namespace
