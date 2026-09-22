#include "Engine/Systems/Renderer/RenderGraph/RenderGraphExecutor.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraphTransfers.h"
#include "Tests/Framework/Test.h"
#include "Tests/Fixtures/RhiFrameCapture.h"
#include <algorithm>
#include <numeric>

using namespace Swim;
using namespace Swim::Render;
using S = Rhi::ResourceState;
using Q = Rhi::QueueType;
using U = Rhi::BufferUsage;

namespace
{
	std::vector<std::byte> Pattern(std::size_t size, unsigned seed)
	{
		std::vector<std::byte> bytes(size);
		for (std::size_t i = 0; i < size; ++i)
		{
			bytes[i] = static_cast<std::byte>((i * 7 + seed) & 0xff);
		}
		return bytes;
	}

	Rhi::BufferDesc DeviceBuffer(std::uint64_t size)
	{
		return { size, U::TransferSource | U::TransferDestination | U::Storage, Rhi::MemoryPreference::DeviceLocal, "device data" };
	}

	std::size_t CountKind(const std::vector<Testing::MockCommand>& commands, std::string_view kind)
	{
		return static_cast<std::size_t>(std::count_if(commands.begin(), commands.end(),
			[&](const auto& c)
			{
				return c.Kind == kind;
			}));
	}
} // namespace

SWIM_TEST("RenderGraph.Transfers", "UploadCopyAndReadbackRoundTripThroughExecutorArenas")
{
	Testing::MockDevice device;
	RenderGraph graph;
	const auto payload = Pattern(256, 3);
	auto working = graph.CreateBuffer(DeviceBuffer(256));
	AddBufferUpload(graph, "Upload", payload, working);
	auto readback = AddBufferReadback(graph, "Readback", working, 0, 256);

	RenderGraphExecutor executor(device);
	auto compiled = graph.Compile();
	SWIM_CHECK(compiled.Dump().find("staging=upload") != std::string::npos);
	SWIM_CHECK(compiled.Dump().find("staging=readback") != std::string::npos);
	SWIM_REQUIRE_EQUAL(compiled.GetSchedule().size(), 2u);

	const auto completion = executor.Execute(compiled);
	SWIM_CHECK_EQUAL(completion.Value, 1u);
	SWIM_CHECK_EQUAL(executor.GetUploadCapacity(), 64u * 1024u);
	SWIM_CHECK_EQUAL(executor.GetReadbackCapacity(), 64u * 1024u);

	std::vector<std::byte> result(256);
	SWIM_CHECK(executor.TryReadback(readback.Buffer, result) == Rhi::ReadbackStatus::NotReady);
	SWIM_CHECK(std::all_of(result.begin(), result.end(),
		[](auto b)
		{
			return b == std::byte{ 0 };
		}));
	executor.Wait();
	SWIM_REQUIRE(executor.TryReadback(readback.Buffer, result) == Rhi::ReadbackStatus::Ready);
	SWIM_CHECK(result == payload);
	std::span<const std::byte> view;
	SWIM_REQUIRE(executor.TryGetReadback(readback.Buffer, view) == Rhi::ReadbackStatus::Ready);
	SWIM_CHECK(std::equal(view.begin(), view.end(), payload.begin(), payload.end()));

	// Two copies, generated barriers only, and a final host-read transition.
	const auto& commands = *device.Commands;
	SWIM_CHECK_EQUAL(CountKind(commands, "CopyBuffer"), 2u);
	SWIM_CHECK(std::any_of(commands.begin(), commands.end(),
		[](const auto& c)
		{
			return c.Kind == "TransitionBuffer" && c.Before == S::HostWrite && c.After == S::CopySource;
		}));
	SWIM_CHECK(commands.back().Kind == "TransitionBuffer" && commands.back().After == S::HostRead);
	SWIM_CHECK_THROWS(executor.GetExported(readback.Buffer), std::invalid_argument);
	SWIM_CHECK_THROWS(executor.TryReadback(working, result), std::invalid_argument);
	std::vector<std::byte> wrongSize(8);
	SWIM_CHECK_THROWS(executor.TryReadback(readback.Buffer, wrongSize), std::invalid_argument);
}

SWIM_TEST("RenderGraph.Transfers", "PartialCopiesPreserveInitializedContentsOnly")
{
	Testing::MockDevice device;
	{
		RenderGraph uninitialized;
		auto transient = uninitialized.CreateBuffer(DeviceBuffer(64));
		AddBufferUpload(uninitialized, "Partial", Pattern(16, 1), transient, 16);
		uninitialized.Export(transient, S::ShaderRead);
		SWIM_CHECK_THROWS(uninitialized.Compile(), std::invalid_argument);
	}

	auto persistent = device.CreateBuffer(DeviceBuffer(64));
	auto* host = static_cast<Testing::MockMappedBuffer*>(persistent.get());
	std::fill(host->Bytes.begin(), host->Bytes.end(), std::byte{ 0xAA });

	RenderGraph graph;
	auto imported = graph.ImportBuffer(*persistent, S::ShaderRead);
	const auto first = Pattern(16, 5);
	const auto second = Pattern(8, 9);
	AddBufferUpload(graph, "First", first, imported, 16);
	AddBufferUpload(graph, "Second", second, imported, 40);
	RenderGraphExecutor executor(device);
	auto compiled = graph.Compile();
	SWIM_REQUIRE_EQUAL(compiled.GetSchedule().size(), 2u); // Writes to imports are roots.
	executor.Execute(compiled);
	executor.Wait();

	for (std::size_t i = 0; i < 64; ++i)
	{
		std::byte expected{ 0xAA };
		if (i >= 16 && i < 32)
		{
			expected = first[i - 16];
		}
		else if (i >= 40 && i < 48)
		{
			expected = second[i - 40];
		}
		SWIM_CHECK(host->Bytes[i] == expected);
	}
	// The import returns to its declared resting state after the partial updates.
	SWIM_CHECK(compiled.GetFinalBarriers().size() == 1u && compiled.GetFinalBarriers()[0].After == S::ShaderRead);
	SWIM_CHECK_THROWS(AddBufferUpload(graph, "Out of range", Pattern(16, 0), imported, 56), std::invalid_argument);
}

SWIM_TEST("RenderGraph.Transfers", "StagingGrowsOnlyBetweenSubmissionsAndIsReused")
{
	Testing::MockDevice device;
	RenderGraphExecutor executor(device);
	const auto run = [&](std::uint64_t size)
	{
		RenderGraph graph;
		auto target = graph.CreateBuffer(DeviceBuffer(size));
		AddBufferUpload(graph, "Upload", Pattern(size, 2), target);
		graph.Export(target, S::ShaderRead);
		executor.Execute(graph.Compile());
	};

	run(1024);
	SWIM_CHECK_EQUAL(executor.GetUploadCapacity(), 64u * 1024u);
	const auto created = device.BufferCreateCount;
	run(1024);
	SWIM_CHECK_EQUAL(device.BufferCreateCount, created); // Pooled target + reused arena.
	SWIM_CHECK_EQUAL(device.LastTimeline->WaitCount, 1u);

	run(100 * 1024);
	SWIM_CHECK_EQUAL(executor.GetUploadCapacity(), 128u * 1024u);
	SWIM_CHECK_EQUAL(device.LastTimeline->WaitCount, 2u); // Growth happens after the predecessor wait.
	run(1024);
	SWIM_CHECK_EQUAL(executor.GetUploadCapacity(), 128u * 1024u);
	executor.Trim();
	SWIM_CHECK_EQUAL(executor.GetUploadCapacity(), 0u);

	RenderGraphExecutor reserved(device, { 256u * 1024u, 4096u });
	SWIM_CHECK_EQUAL(reserved.GetUploadCapacity(), 256u * 1024u);
	SWIM_CHECK_EQUAL(reserved.GetReadbackCapacity(), 4096u);
}

SWIM_TEST("RenderGraph.Transfers", "WritersRunPerExecutionAndFailuresPublishNothing")
{
	Testing::MockDevice device;
	RenderGraph graph;
	unsigned calls = 0;
	bool fail = true;
	auto target = graph.CreateBuffer(DeviceBuffer(32));
	AddBufferUpload(
		graph, "Generated", 32,
		[&](std::span<std::byte> bytes)
		{
			++calls;
			if (fail)
			{
				throw std::runtime_error("writer failure");
			}
			std::fill(bytes.begin(), bytes.end(), std::byte(calls));
		},
		target);
	auto readback = AddBufferReadback(graph, "Readback", target, 0, 32);
	RenderGraphExecutor executor(device);
	auto compiled = graph.Compile();

	SWIM_CHECK_THROWS(executor.Execute(compiled), std::runtime_error);
	SWIM_CHECK_EQUAL(device.queue.SubmitCount, 0u);
	std::vector<std::byte> bytes(32);
	SWIM_CHECK_THROWS(executor.TryReadback(readback.Buffer, bytes), std::logic_error);

	fail = false;
	device.queue.FailSubmit = true;
	SWIM_CHECK_THROWS(executor.Execute(compiled), std::runtime_error);
	device.queue.FailSubmit = false;
	SWIM_CHECK_EQUAL(executor.Execute(compiled).Value, 1u);
	executor.Wait();
	SWIM_REQUIRE(executor.TryReadback(readback.Buffer, bytes) == Rhi::ReadbackStatus::Ready);
	SWIM_CHECK(bytes[0] == std::byte(3));

	SWIM_CHECK_EQUAL(executor.Execute(compiled).Value, 2u);
	executor.Wait();
	SWIM_REQUIRE(executor.TryReadback(readback.Buffer, bytes) == Rhi::ReadbackStatus::Ready);
	SWIM_CHECK(bytes[31] == std::byte(4));
	executor.Trim();
	SWIM_CHECK_THROWS(executor.TryReadback(readback.Buffer, bytes), std::logic_error);
}

SWIM_TEST("RenderGraph.Transfers", "StagedBuffersEnforceDirectionAndSuballocation")
{
	RenderGraph graph;
	const auto writer = [](std::span<std::byte>)
	{
	};
	SWIM_CHECK_THROWS(graph.CreateUpload({ 0, U::TransferSource, 4, "empty" }, writer), std::invalid_argument);
	SWIM_CHECK_THROWS(graph.CreateUpload({ 16, U::TransferDestination, 4, "dst" }, writer), std::invalid_argument);
	SWIM_CHECK_THROWS(graph.CreateUpload({ 16, U::TransferSource, 3, "align" }, writer), std::invalid_argument);
	SWIM_CHECK_THROWS(graph.CreateUpload({ 16, U::TransferSource, 4, "writer" }, {}), std::invalid_argument);
	SWIM_CHECK_THROWS(graph.CreateReadback({ 0, 4, "empty" }), std::invalid_argument);

	auto upload = graph.CreateUpload({ 16, U::TransferSource | U::Storage, 4, "upload" }, writer);
	auto readback = graph.CreateReadback({ 16, 4, "readback" });
	SWIM_CHECK_THROWS(graph.Export(upload, S::CopySource), std::invalid_argument);
	SWIM_CHECK_THROWS(graph.Export(readback, S::HostRead), std::invalid_argument);
	SWIM_CHECK_THROWS(graph.AddPass(
						  "writes upload", Q::Compute,
						  [&](auto& b)
						  {
							  b.ReadWrite(upload, S::ShaderRead | S::ShaderWrite);
						  },
						  [](auto&)
						  {
						  }),
		std::invalid_argument);
	// An unwritten readback exports uninitialized contents.
	SWIM_CHECK_THROWS(graph.Compile(), std::invalid_argument);

	Testing::MockDevice device;
	RenderGraph direct;
	auto source = direct.CreateUpload({ 16, U::TransferSource | U::Storage, 4, "shader input" }, writer);
	auto sink = direct.CreateReadback({ 16, 4, "sink" });
	direct.AddPass(
		"copy", Q::Transfer,
		[&](auto& b)
		{
			b.Read(source, S::CopySource);
			b.Write(sink, S::CopyDestination);
		},
		[&](RenderCommandContext& c)
		{
			SWIM_CHECK_THROWS(c.Get(source), std::invalid_argument);
			SWIM_CHECK_THROWS(c.Get(sink), std::invalid_argument);
			const auto from = c.GetRange(source);
			const auto to = c.GetRange(sink);
			SWIM_CHECK(from.Buffer != nullptr && to.Buffer != nullptr && from.Buffer != to.Buffer);
			SWIM_CHECK_EQUAL(from.Size, 16u);
			c.Commands().CopyBuffer(*from.Buffer, *to.Buffer, { from.Offset, to.Offset, 16 });
		});
	RenderGraphExecutor executor(device);
	executor.Execute(direct.Compile());
}

SWIM_TEST("RenderGraph.Transfers", "UploadOffsetsHonorRequestedAndDescriptorAlignment")
{
	Testing::MockDevice device;
	device.adapterInfo.Capabilities.MinUniformBufferOffsetAlignment = 256;
	RenderGraph graph;
	std::vector<GraphBuffer> uploads;
	for (unsigned i = 0; i < 3; ++i)
	{
		uploads.push_back(graph.CreateUpload(Pattern(20, i), "constants", U::Uniform, 16));
	}
	auto odd = graph.CreateUpload(Pattern(12, 7), "odd", U::TransferSource, 1024);
	std::vector<std::uint64_t> offsets;
	graph.AddPass(
		"consume", Q::Graphics,
		[&](auto& b)
		{
			for (auto u : uploads)
			{
				b.Read(u, S::UniformBuffer);
			}
			b.Read(odd, S::CopySource);
			b.SideEffect();
		},
		[&](RenderCommandContext& c)
		{
			for (auto u : uploads)
			{
				offsets.push_back(c.GetRange(u).Offset);
			}
			offsets.push_back(c.GetRange(odd).Offset);
		});
	RenderGraphExecutor executor(device);
	executor.Execute(graph.Compile());
	SWIM_REQUIRE_EQUAL(offsets.size(), 4u);
	for (auto offset : offsets)
	{
		SWIM_CHECK_EQUAL(offset % 256, 0u);
	}
	SWIM_CHECK_EQUAL(offsets[3] % 1024, 0u);
	SWIM_CHECK(offsets[0] != offsets[1] && offsets[1] != offsets[2]);
}

SWIM_TEST("RenderGraph.Transfers", "TextureUploadAndReadbackUseSubresourceStates")
{
	Testing::MockDevice device;
	device.CreateTextures = true;
	Rhi::TextureDesc desc;
	desc.Extent = { 4, 4, 1 };
	desc.MipLevels = 2;
	desc.PixelFormat = Rhi::Format::RGBA8Unorm;
	desc.Usage = Rhi::TextureUsage::TransferSource | Rhi::TextureUsage::TransferDestination | Rhi::TextureUsage::Sampled;

	RenderGraph graph;
	auto texture = graph.CreateTexture(desc);
	const auto base = Pattern(4 * 4 * 4, 11);
	const auto patch = Pattern(2 * 2 * 4, 90);
	Rhi::BufferTextureCopyRegion whole{};
	whole.Extent = { 4, 4, 1 };
	Rhi::BufferTextureCopyRegion corner{};
	corner.TextureOffset = { 2, 2, 0 };
	corner.Extent = { 2, 2, 1 };
	AddTextureUpload(graph, "Base", base, texture, whole);
	AddTextureUpload(graph, "Patch", patch, texture, corner);
	auto readback = AddTextureReadback(graph, "Read mip 0", texture, whole);
	SWIM_CHECK_EQUAL(graph.GetDesc(readback.Buffer).Size, 64u);

	Rhi::BufferTextureCopyRegion outside{};
	outside.TextureOffset = { 3, 0, 0 };
	outside.Extent = { 2, 1, 1 };
	SWIM_CHECK_THROWS(AddTextureUpload(graph, "Outside", Pattern(8, 0), texture, outside), std::invalid_argument);
	SWIM_CHECK_THROWS(AddTextureUpload(graph, "Short", Pattern(8, 0), texture, whole), std::invalid_argument);
	Rhi::BufferTextureCopyRegion mip1{};
	mip1.Subresource.MipLevel = 1;
	mip1.Extent = { 2, 2, 1 };
	// Mip 1 was never written; reading it rejects at compilation.
	AddTextureReadback(graph, "Read mip 1", texture, mip1);
	SWIM_CHECK_THROWS(graph.Compile(), std::invalid_argument);

	RenderGraph valid;
	auto target = valid.CreateTexture(desc);
	AddTextureUpload(valid, "Base", base, target, whole);
	AddTextureUpload(valid, "Patch", patch, target, corner);
	auto result = AddTextureReadback(valid, "Read mip 0", target, whole);
	RenderGraphExecutor executor(device);
	executor.Execute(valid.Compile());
	executor.Wait();
	std::vector<std::byte> pixels(64);
	SWIM_REQUIRE(executor.TryReadback(result.Buffer, pixels) == Rhi::ReadbackStatus::Ready);
	for (std::uint32_t y = 0; y < 4; ++y)
	{
		for (std::uint32_t x = 0; x < 4; ++x)
		{
			for (std::uint32_t c = 0; c < 4; ++c)
			{
				const auto i = (y * 4 + x) * 4 + c;
				const bool patched = x >= 2 && y >= 2;
				const auto expected = patched ? patch[((y - 2) * 2 + (x - 2)) * 4 + c] : base[i];
				SWIM_CHECK(pixels[i] == expected);
			}
		}
	}
	SWIM_CHECK_EQUAL(CountKind(*device.Commands, "CopyBufferToTexture"), 2u);
	SWIM_CHECK_EQUAL(CountKind(*device.Commands, "CopyTextureToBuffer"), 1u);

	Rhi::TextureDesc rgb = desc;
	rgb.PixelFormat = Rhi::Format::RGB32Float;
	RenderGraph unsupported;
	auto rgbTexture = unsupported.CreateTexture(rgb);
	SWIM_CHECK_THROWS(AddTextureUpload(unsupported, "RGB", Pattern(4 * 4 * 12, 0), rgbTexture, whole), std::invalid_argument);
}
