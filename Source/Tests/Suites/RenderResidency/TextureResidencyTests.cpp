#include "Engine/Systems/Renderer/RenderGraph/RenderGraph.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraphExecutor.h"
#include "Engine/Systems/Renderer/Residency/TextureResidency.h"
#include "Tests/Fixtures/RhiFrameCapture.h"
#include "Tests/Framework/Test.h"

#include <cstring>

using namespace Swim;
using namespace Swim::Render;
using Assets::TexturePayloadFormat;

namespace
{
	// A 4x2 texture with a full mip chain (4x2, 2x1, 1x1) in one payload.
	Assets::TextureAsset MakeTexture(TexturePayloadFormat format = TexturePayloadFormat::RGBA8UNorm, std::uint32_t texel = 4)
	{
		Assets::TextureAsset texture;
		texture.Width = 4;
		texture.Height = 2;
		Assets::TexturePayloadVariant payload;
		payload.Format = format;
		std::uint64_t offset = 0;
		for (auto [w, h] : { std::pair{ 4u, 2u }, std::pair{ 2u, 1u }, std::pair{ 1u, 1u } })
		{
			const std::uint64_t size = std::uint64_t(w) * h * texel;
			payload.Mips.push_back({ w, h, 1, offset, size, size });
			offset += size + 3; // Unaligned source offsets are repacked.
		}
		payload.Bytes.resize(static_cast<std::size_t>(offset));
		for (std::size_t i = 0; i < payload.Bytes.size(); ++i)
		{
			payload.Bytes[i] = static_cast<std::byte>(i * 3 + 1);
		}
		texture.Payloads.push_back(std::move(payload));
		return texture;
	}

	Rhi::TimelinePoint Upload(TextureResidency& residency, RenderGraphExecutor& executor, TextureGraphResources* out = nullptr)
	{
		RenderGraph graph;
		auto resources = residency.Import(graph);
		if (out)
		{
			*out = resources;
		}
		const auto completion = executor.Execute(graph.Compile());
		residency.CommitUploads(completion);
		return completion;
	}
} // namespace

SWIM_TEST("Render.TextureResidency", "SelectsUncompressedNativePayloadsOnly")
{
	auto texture = MakeTexture();
	auto selection = TextureResidency::SelectPayload(texture);
	SWIM_REQUIRE(selection.has_value());
	SWIM_CHECK(selection->Format == Rhi::Format::RGBA8Unorm);

	// A compressed variant first is skipped in favour of the native fallback.
	Assets::TexturePayloadVariant bc7;
	bc7.Format = TexturePayloadFormat::BC7UNorm;
	texture.Payloads.insert(texture.Payloads.begin(), bc7);
	selection = TextureResidency::SelectPayload(texture);
	SWIM_REQUIRE(selection.has_value());
	SWIM_CHECK_EQUAL(selection->Payload, 1u);

	auto srgb = MakeTexture(TexturePayloadFormat::RGBA8SRgb);
	SWIM_CHECK(TextureResidency::SelectPayload(srgb)->Format == Rhi::Format::RGBA8UnormSrgb);
	auto ktx = MakeTexture();
	ktx.Payloads[0].Container = Assets::TextureContainerFormat::Ktx2;
	SWIM_CHECK(!TextureResidency::SelectPayload(ktx));
	auto cube = MakeTexture();
	cube.Dimension = Assets::TextureDimension::Cube;
	SWIM_CHECK(!TextureResidency::SelectPayload(cube));
	auto compressedOnly = MakeTexture(TexturePayloadFormat::BC1UNorm);
	SWIM_CHECK(!TextureResidency::SelectPayload(compressedOnly));
}

SWIM_TEST("Render.TextureResidency", "UploadsEveryMipThroughTheGraphAndBecomesResident")
{
	Testing::MockDevice device;
	device.CreateTextures = true;
	TextureResidency residency(device);
	RenderGraphExecutor executor(device);
	const auto asset = MakeTexture();
	const auto handle = residency.CreateTexture(asset, "albedo");
	SWIM_CHECK(residency.GetState(handle) == GpuUploadState::PendingUpload);
	SWIM_REQUIRE(residency.GetView(handle) != nullptr);
	SWIM_CHECK_EQUAL(residency.GetView(handle)->GetDesc().MipLevelCount, 3u);
	SWIM_CHECK_EQUAL(residency.GetTexture(handle)->GetDesc().MipLevels, 3u);
	SWIM_CHECK_EQUAL(residency.GetStats().PendingUploadBytes, 32u + 8u + 4u);

	TextureGraphResources resources;
	Upload(residency, executor, &resources);
	SWIM_REQUIRE_EQUAL(resources.Uploads.size(), 1u);
	SWIM_CHECK(resources.Uploads[0].Texture == handle);
	SWIM_CHECK(residency.GetState(handle) == GpuUploadState::Uploading);

	auto* texture = static_cast<Testing::MockTexture*>(residency.GetTexture(handle));
	const auto& payload = asset.Payloads[0];
	for (std::uint32_t mip = 0; mip < 3; ++mip)
	{
		const auto& expected = payload.Mips[mip];
		const auto& actual = texture->Bytes({ mip, 0 });
		SWIM_REQUIRE_EQUAL(actual.size(), std::size_t(expected.SizeBytes));
		SWIM_CHECK(std::memcmp(actual.data(), payload.Bytes.data() + expected.OffsetBytes, actual.size()) == 0);
	}
	// The graph leaves the texture sampled-ready.
	SWIM_CHECK(device.Commands->back().Kind == "TransitionTexture" && device.Commands->back().After == Rhi::ResourceState::ShaderRead);

	SWIM_CHECK_EQUAL(residency.Collect(), 0u);
	executor.Wait();
	SWIM_CHECK_EQUAL(residency.Collect(), 1u);
	SWIM_CHECK(residency.GetState(handle) == GpuUploadState::Resident);
	SWIM_CHECK_EQUAL(residency.GetStats().ResidentBytes, 44u);

	// Resident textures are not re-imported.
	RenderGraph idle;
	SWIM_CHECK(residency.Import(idle).Uploads.empty());
}

SWIM_TEST("Render.TextureResidency", "RejectsInvalidPayloadsWithoutPartialState")
{
	Testing::MockDevice device;
	device.CreateTextures = true;
	TextureResidency residency(device, { 2, "Small" });
	SWIM_CHECK_THROWS(residency.CreateTexture(MakeTexture(TexturePayloadFormat::BC7UNorm)), std::invalid_argument);

	auto badMip = MakeTexture();
	badMip.Payloads[0].Mips[1].Width = 3;
	SWIM_CHECK_THROWS(residency.CreateTexture(badMip), std::invalid_argument);
	auto shortBytes = MakeTexture();
	shortBytes.Payloads[0].Bytes.resize(20);
	SWIM_CHECK_THROWS(residency.CreateTexture(shortBytes), std::invalid_argument);
	auto tooManyMips = MakeTexture();
	tooManyMips.Payloads[0].Mips.push_back({ 1, 1, 1, 0, 4, 4 });
	SWIM_CHECK_THROWS(residency.CreateTexture(tooManyMips), std::invalid_argument);

	device.FailTextureCreate = device.TextureAttemptCount + 1;
	SWIM_CHECK_THROWS(residency.CreateTexture(MakeTexture()), std::runtime_error);
	SWIM_CHECK_EQUAL(residency.GetStats().PendingTextures, 0u);

	residency.CreateTexture(MakeTexture());
	residency.CreateTexture(MakeTexture(TexturePayloadFormat::RG8UNorm, 2));
	SWIM_CHECK_THROWS(residency.CreateTexture(MakeTexture()), std::length_error);
	SWIM_CHECK_EQUAL(residency.GetStats().PendingTextures, 2u);
}

SWIM_TEST("Render.TextureResidency", "AbortRecommitsAndDestructionWaitsForGpuUse")
{
	Testing::MockDevice device;
	device.CreateTextures = true;
	TextureResidency residency(device);
	RenderGraphExecutor executor(device);
	const auto handle = residency.CreateTexture(MakeTexture());
	{
		RenderGraph graph;
		SWIM_CHECK_EQUAL(residency.Import(graph).Uploads.size(), 1u);
		RenderGraph other;
		SWIM_CHECK_THROWS(residency.Import(other), std::logic_error);
		SWIM_CHECK_THROWS(residency.DestroyTexture(handle), std::logic_error);
		SWIM_CHECK_THROWS(residency.CommitUploads({}), std::invalid_argument);
		residency.AbortUploads();
	}
	SWIM_CHECK(residency.GetState(handle) == GpuUploadState::PendingUpload);

	const auto upload = Upload(residency, executor);
	// Destroying mid-upload retires after the upload completes.
	SWIM_CHECK(residency.DestroyTexture(handle));
	SWIM_CHECK(!residency.IsValid(handle));
	SWIM_CHECK_EQUAL(residency.GetStats().RetiringTextures, 1u);
	residency.Collect();
	SWIM_CHECK_EQUAL(residency.GetStats().RetiringTextures, 1u);
	static_cast<Testing::MockTimeline*>(upload.Semaphore)->Complete(upload.Value);
	residency.Collect();
	SWIM_CHECK_EQUAL(residency.GetStats().RetiringTextures, 0u);
	SWIM_CHECK(!residency.DestroyTexture(handle));
}
