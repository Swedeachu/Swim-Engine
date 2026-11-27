#include "Engine/Math/AlphaCoverage.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraphExecutor.h"
#include "Engine/Systems/Renderer/Resources/TextureArray.h"
#include "Tests/Fixtures/RhiFrameCapture.h"
#include "Tests/Framework/Test.h"

#include <chrono>
#include <cmath>
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

SWIM_TEST("Render.TextureArray", "UploadsOfSomeLayersRunOnlyOnThoseLayers")
{
	// The upload pass declares only the layers its copies touch; its callback must ask for no
	// more than that, both for scattered layers and past the run limit (the whole array).
	Testing::MockDevice device;
	device.CreateTextures = true;
	device.adapterInfo.Capabilities.MaxTextureArrayLayers = 4096;
	TextureArrayDesc desc;
	desc.Width = 4;
	desc.Height = 4;
	desc.Layers = 512;
	desc.DebugName = "some layers";
	TextureArray array(device, desc);
	RenderGraphExecutor executor(device);
	const auto set = [&](std::uint32_t layer)
	{
		TextureLayerData data;
		data.Width = 4;
		data.Height = 4;
		data.Mips.push_back(std::vector<std::byte>(4 * 4 * 4, std::byte{ 200 }));
		array.SetLayer(layer, std::move(data));
	};
	const auto frame = [&]
	{
		RenderGraph graph;
		array.Import(graph, std::uint64_t{ 1 } << 30);
		auto plan = graph.Compile();
		executor.Execute(plan);
		return array.GetStats().UploadedBytes;
	};

	frame();										  // The placeholder fill, every layer.
	constexpr std::uint64_t layerBytes = 64 + 16 + 4; // 4x4, 2x2 and 1x1 mips.

	for (const std::uint32_t layer : { 3u, 4u, 5u, 40u, 511u })
	{
		set(layer);
	}

	SWIM_CHECK_EQUAL(frame(), 5u * layerBytes);

	for (std::uint32_t layer = 0; layer < 200; layer += 2)
	{
		set(layer); // 100 runs: past the limit, one use over the whole array.
	}

	SWIM_CHECK_EQUAL(frame(), 100u * layerBytes);
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

namespace
{

	// A 16 x 16 window: a one-texel opaque frame around a clear middle (glass, bars, fences).
	TextureLayerData Frame()
	{
		TextureLayerData data;
		data.Format = Rhi::Format::RGBA8UnormSrgb;
		data.Width = 16;
		data.Height = 16;
		auto& pixels = data.Mips.emplace_back(16 * 16 * 4, std::byte{ 255 });

		for (std::uint32_t y = 1; y < 15; ++y)
		{
			for (std::uint32_t x = 1; x < 15; ++x)
			{
				pixels[(y * 16 + x) * 4 + 3] = std::byte{ 0 };
			}
		}

		return data;
	}

	// Solid (fully opaque) texels along each mip's outer border, as a share of that border.
	double BorderOpaque(const std::vector<std::byte>& mip, std::uint32_t size)
	{
		std::uint32_t opaque = 0;
		std::uint32_t border = 0;

		for (std::uint32_t y = 0; y < size; ++y)
		{
			for (std::uint32_t x = 0; x < size; ++x)
			{
				if (x == 0 || y == 0 || x == size - 1 || y == size - 1)
				{
					++border;
					opaque += std::to_integer<std::uint8_t>(mip[(y * size + x) * 4 + 3]) == 255 ? 1u : 0u;
				}
			}
		}

		return double(opaque) / double(border);
	}

} // namespace

SWIM_TEST("Render.TextureArray", "MaximumAlphaMipsKeepThinFramesSolidAtEveryMip")
{
	Testing::MockDevice device;
	device.CreateTextures = true;
	TextureArrayDesc desc;
	desc.Width = 16;
	desc.Height = 16;
	desc.Layers = 1;
	desc.Format = Rhi::Format::RGBA8UnormSrgb;
	desc.DebugName = "frames";
	TextureArray array(device, desc);

	// The plain average halves the frame's alpha at mip 1: an alpha test above 0.5 loses it.
	auto average = Frame();
	average.AlphaMips = AlphaMipFilter::Average;
	const auto averaged = array.PrepareLayer(std::move(average));
	SWIM_REQUIRE(averaged->Mips.size() == 5);
	SWIM_CHECK(BorderOpaque(averaged->Mips[1], 8) < 0.01);

	// The maximum keeps an unbroken one-texel frame down to 2 x 2, with the frame's colour.
	auto frame = Frame();
	frame.AlphaMips = AlphaMipFilter::Maximum;
	const auto kept = array.PrepareLayer(std::move(frame));

	for (std::uint32_t mip = 1; mip < 5; ++mip)
	{
		const std::uint32_t size = 16u >> mip;
		SWIM_CHECK_EQUAL(BorderOpaque(kept->Mips[mip], size), 1.0);
		SWIM_CHECK_EQUAL(std::to_integer<int>(kept->Mips[mip][0]), 255);
	}

	// Inside the frame it stays clear until the frame fills the mip.
	SWIM_CHECK_EQUAL(std::to_integer<int>(kept->Mips[1][(3 * 8 + 3) * 4 + 3]), 0);
}

SWIM_TEST("Render.TextureArray", "CoverageAlphaMipsPerLayerOverrideTheArray")
{
	Testing::MockDevice device;
	device.CreateTextures = true;
	TextureArrayDesc desc;
	desc.Width = 16;
	desc.Height = 16;
	desc.Layers = 1;
	desc.Format = Rhi::Format::RGBA8UnormSrgb;
	desc.DebugName = "cut-outs";
	TextureArray array(device, desc); // No array cutoff: plain averages by default.

	// Scattered opaque texels (leaves): two in five covered at mip 0.
	TextureLayerData data;
	data.Format = desc.Format;
	data.Width = 16;
	data.Height = 16;
	auto& pixels = data.Mips.emplace_back(16 * 16 * 4, std::byte{ 200 });

	for (std::uint32_t y = 0; y < 16; ++y)
	{
		for (std::uint32_t x = 0; x < 16; ++x)
		{
			pixels[(y * 16 + x) * 4 + 3] = (x * 7 + y * 13) % 5 < 2 ? std::byte{ 255 } : std::byte{ 0 };
		}
	}

	const float covered0 = ::Engine::AlphaCoverage(data.Mips[0], 0.6f);
	SWIM_CHECK_NEAR(covered0, 0.4f, 0.05f);
	auto plain = data;
	const auto averaged = array.PrepareLayer(std::move(plain));

	data.AlphaMips = AlphaMipFilter::Coverage;
	data.AlphaCutoff = 0.6f;
	const auto covered = array.PrepareLayer(std::move(data));

	for (std::uint32_t mip = 1; mip < 4; ++mip)
	{
		// As close to mip 0's coverage as the mip's alpha levels allow; the average thins out.
		const float kept = ::Engine::AlphaCoverage(covered->Mips[mip], 0.6f);
		const float plainCoverage = ::Engine::AlphaCoverage(averaged->Mips[mip], 0.6f);
		SWIM_CHECK(std::abs(kept - covered0) < std::abs(plainCoverage - covered0));
		SWIM_CHECK_NEAR(kept, covered0, 0.35f);
	}
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
