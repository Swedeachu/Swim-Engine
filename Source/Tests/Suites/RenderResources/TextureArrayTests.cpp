#include "Engine/Systems/Renderer/Resources/TextureArray.h"
#include "Tests/Fixtures/RhiFrameCapture.h"
#include "Tests/Framework/Test.h"

#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

using namespace Swim;
using namespace Swim::Render;

SWIM_TEST("Render.TextureArray", "RejectsMoreLayersThanTheDeviceSupports")
{
	Testing::MockDevice device;
	device.CreateTextures = true;
	device.adapterInfo.Capabilities.MaxTextureArrayLayers = 4;
	TextureArrayDesc desc;
	desc.Width = 4;
	desc.Height = 4;
	desc.Layers = 5;
	desc.DebugName = "too deep";
	SWIM_CHECK_THROWS(TextureArray(device, desc), std::invalid_argument);
	SWIM_CHECK_EQUAL(device.TextureCreateCount, 0u);
	desc.Layers = 4;
	TextureArray fits(device, desc);
	SWIM_CHECK_EQUAL(device.TextureCreateCount, 1u);
}

SWIM_TEST("Render.TextureArray", "ABigFirstUploadDeclaresOneUseAndStaysFast")
{
	Testing::MockDevice device;
	device.CreateTextures = true;
	device.adapterInfo.Capabilities.MaxTextureArrayLayers = 4096;
	TextureArrayDesc desc;
	desc.Width = 4;
	desc.Height = 4;
	desc.Layers = 2048;
	desc.DebugName = "many layers";
	TextureArray array(device, desc);

	for (std::uint32_t layer = 0; layer < desc.Layers; ++layer)
	{
		TextureLayerData data;
		data.Width = 4;
		data.Height = 4;
		data.Mips.push_back(std::vector<std::byte>(4 * 4 * 4, std::byte{ 200 }));
		array.SetLayer(layer, std::move(data));
	}

	// Thousands of copies in one upload pass used to take seconds to declare (one use per
	// copy, each checked against all the earlier ones).
	const auto start = std::chrono::steady_clock::now();
	RenderGraph graph;
	array.Import(graph, std::uint64_t{ 1 } << 30);
	const auto compiled = graph.Compile();
	const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
	SWIM_CHECK(array.GetStats().UploadedBytes > 0);
	SWIM_CHECK(array.GetStats().PendingUploadBytes == 0);
	SWIM_CHECK(seconds < 0.5);
}

SWIM_TEST("Render.TextureArray", "SettingManyCutOutLayersWithGeneratedMipsIsCheap")
{
	Testing::MockDevice device;
	device.CreateTextures = true;
	device.adapterInfo.Capabilities.MaxTextureArrayLayers = 4096;
	TextureArrayDesc desc;
	desc.Width = 32;
	desc.Height = 32;
	desc.Layers = 512;
	desc.Format = Rhi::Format::RGBA8UnormSrgb;
	desc.AlphaCoverageCutoff = 0.5f;
	desc.DebugName = "cut-out layers";
	TextureArray array(device, desc);
	std::uint32_t seed = 7;
	std::vector<TextureLayerData> layers(desc.Layers);

	for (auto& data : layers)
	{
		data.Format = desc.Format;
		data.Width = 32;
		data.Height = 32;
		auto& pixels = data.Mips.emplace_back(32 * 32 * 4);

		for (auto& b : pixels)
		{
			seed = seed * 1664525u + 1013904223u;
			b = std::byte(seed >> 24);
		}
	}

	const auto start = std::chrono::steady_clock::now();

	for (std::uint32_t layer = 0; layer < desc.Layers; ++layer)
	{
		array.SetLayer(layer, std::move(layers[layer]));
	}

	const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
	std::printf("             512 cut-out 32x32 sRGB layers with mips: %.1f ms\n", ms);
	SWIM_CHECK(ms < 100.0);
}

SWIM_TEST("Render.TextureArray", "BackgroundMipGenerationFinishesLayersOnTheLoaderThread")
{
	Testing::MockDevice device;
	device.CreateTextures = true;
	device.adapterInfo.Capabilities.MaxTextureArrayLayers = 4096;
	TextureArrayDesc desc;
	desc.Width = 64;
	desc.Height = 64;
	desc.Layers = 64;
	desc.Format = Rhi::Format::RGBA8Unorm;
	desc.GenerateMipsInBackground = true;
	desc.DebugName = "background mips";
	TextureArray array(device, desc);

	const auto layerData = [](std::uint32_t size, std::uint8_t value)
	{
		TextureLayerData data;
		data.Width = size;
		data.Height = size;
		data.Mips.push_back(std::vector<std::byte>(std::size_t(size) * size * 4, std::byte{ value }));
		return data;
	};

	// Bad data still throws at the call.
	TextureLayerData broken = layerData(64, 1);
	broken.Mips[0].resize(10);
	SWIM_CHECK_THROWS(array.SetLayer(0, std::move(broken)), std::invalid_argument);

	for (std::uint32_t layer = 0; layer < desc.Layers; ++layer)
	{
		// Every other layer at another size: resized on the loader thread too.
		array.SetLayer(layer, layerData(layer % 2 ? 64 : 32, static_cast<std::uint8_t>(layer)));
	}

	// Complete data needs no work and queues its upload at once.
	TextureLayerData full = layerData(64, 9);

	for (std::uint32_t mip = 1; mip < array.GetMipLevels(); ++mip)
	{
		const std::uint32_t size = std::max(64u >> mip, 1u);
		full.Mips.push_back(std::vector<std::byte>(std::size_t(size) * size * 4, std::byte{ 9 }));
	}

	array.SetLayer(1, std::move(full));
	SWIM_CHECK(array.GetLayerState(1) == TextureLayerState::Uploading);

	bool resident = false;

	for (int frame = 0; frame < 2000 && !resident; ++frame)
	{
		RenderGraph graph;
		array.Import(graph, std::uint64_t{ 1 } << 30);
		graph.Compile();
		resident = true;

		for (std::uint32_t layer = 0; layer < desc.Layers; ++layer)
		{
			resident = resident && array.GetLayerState(layer) == TextureLayerState::Resident;
		}

		if (!resident)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	}

	SWIM_CHECK(resident);
	SWIM_CHECK(array.GetMinLod(0) == 0.0f);
}
