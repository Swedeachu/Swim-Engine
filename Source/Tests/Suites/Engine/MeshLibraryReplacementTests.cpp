#include "Engine/Math/ColorMath.h"
#include "Engine/Systems/Renderer/Resources/BindlessResourceTable.h"
#include "Engine/Systems/Renderer/Runtime/MeshLibrary.h"
#include "Tests/Fixtures/BindlessTableFixture.h"
#include "Tests/Fixtures/ResidencyServiceFixture.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace Swim;
using namespace Swim::Render;

SWIM_TEST("Engine.MeshLibrary", "NamesRegisteredAgainResolveOldHandlesToTheNewAssets")
{
	Testing::ResidencyServiceFixture fixture("MeshLibraryReplacement");
	Testing::BindlessTableFixture elements(4);
	BindlessResourceTable bindless(elements.device, elements.Desc());
	AssetResidencyDesc desc;
	desc.Bindless = &bindless;
	auto& service = fixture.Service(std::move(desc));
	Engine::MeshLibrary library(fixture.assets, service, *fixture.geometry);
	const auto baseline = library.GetStats();

	const std::vector<std::uint8_t> red = { 255, 0, 0, 255 };
	const std::vector<std::uint8_t> blue = { 0, 0, 255, 255 };
	const auto first = library.RegisterTexture("Skin", 1, 1, red);
	const auto upload = [&](Engine::MeshLibrary::TextureHandle texture)
	{
		for (int i = 0; i < 20 && library.GetBindlessIndex(texture) == BindlessResourceTable::FallbackIndex; ++i)
		{
			service.Update();
			fixture.UploadFrame();
			bindless.Collect();
		}
	};
	upload(first);
	SWIM_REQUIRE(library.GetBindlessIndex(first) != BindlessResourceTable::FallbackIndex);

	// A scene re-running its setup registers the same name again: the old handle (held by
	// materials, UI images or caches) follows the new asset instead of the fallback.
	const auto second = library.RegisterTexture("Skin", 1, 1, blue);
	SWIM_CHECK(library.Resolve(first) == second && library.Resolve(second) == second);
	SWIM_CHECK(library.GetStats().PendingReleases == 1);
	library.FlushReleases({});
	SWIM_CHECK(library.GetStats().PendingReleases == 0);
	upload(second);
	SWIM_CHECK(library.GetBindlessIndex(first) == library.GetBindlessIndex(second));
	SWIM_CHECK(library.GetBindlessIndex(first) != BindlessResourceTable::FallbackIndex);

	Engine::ProceduralMeshes::MeshData mesh;
	mesh.AddVertex({ 0, 0, 0 }, { 0, 1, 0 }, { 0, 0 });
	mesh.AddVertex({ 1, 0, 0 }, { 0, 1, 0 }, { 1, 0 });
	mesh.AddVertex({ 0, 0, 1 }, { 0, 1, 0 }, { 0, 1 });
	mesh.AddTriangle(0, 1, 2);
	const auto box = library.Register("Box", mesh);
	const auto replacements = library.GetMeshReplacements();
	const auto box2 = library.Register("Box", mesh);
	SWIM_CHECK(library.Resolve(box) == box2 && library.GetMeshReplacements() == replacements + 1);
	SWIM_CHECK(library.GetStats().Replacements == baseline.Replacements + 2);

	// Unregistering ends the redirect; the texture is released with its slot.
	library.UnregisterTexture("Skin");
	SWIM_CHECK(!library.FindTexture("Skin").IsValid());
	SWIM_CHECK(library.Resolve(first) == first);
	SWIM_CHECK(library.GetStats().Textures == baseline.Textures);
	library.FlushReleases({});
	library.Unregister("Box");
	library.FlushReleases({});
	SWIM_CHECK(library.GetStats().Meshes == baseline.Meshes);
}

namespace
{

	// The previous per-texel pow implementation, kept as the reference.
	std::vector<std::vector<std::uint8_t>> ReferenceMips(
		std::uint32_t w, std::uint32_t h, const std::vector<std::uint8_t>& rgba8, bool srgb)
	{
		std::vector<std::vector<std::uint8_t>> chain;
		std::vector<float> level(rgba8.size());

		for (std::size_t i = 0; i < rgba8.size(); ++i)
		{
			const float value = float(rgba8[i]) / 255.0f;
			level[i] = srgb && i % 4 != 3 ? Engine::SrgbToLinear(value) : value;
		}

		while (true)
		{
			std::vector<std::uint8_t> bytes(level.size());

			for (std::size_t i = 0; i < level.size(); ++i)
			{
				const float value = srgb && i % 4 != 3 ? Engine::LinearToSrgb(level[i]) : level[i];
				bytes[i] = static_cast<std::uint8_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
			}

			chain.push_back(std::move(bytes));

			if (w == 1 && h == 1)
			{
				return chain;
			}

			const std::uint32_t nw = std::max(w / 2, 1u);
			const std::uint32_t nh = std::max(h / 2, 1u);
			std::vector<float> next(std::size_t(nw) * nh * 4);

			for (std::uint32_t y = 0; y < nh; ++y)
			{
				for (std::uint32_t x = 0; x < nw; ++x)
				{
					float colour[3] = {};
					float plain[3] = {};
					float alpha = 0.0f;

					for (std::uint32_t dy = 0; dy < 2; ++dy)
					{
						for (std::uint32_t dx = 0; dx < 2; ++dx)
						{
							const float* t = &level[(std::size_t(std::min(y * 2 + dy, h - 1)) * w + std::min(x * 2 + dx, w - 1)) * 4];

							for (int c = 0; c < 3; ++c)
							{
								colour[c] += t[c] * t[3];
								plain[c] += t[c];
							}

							alpha += t[3];
						}
					}

					float* out = &next[(std::size_t(y) * nw + x) * 4];

					for (int c = 0; c < 3; ++c)
					{
						out[c] = srgb && alpha > 0.0f ? colour[c] / alpha : plain[c] * 0.25f;
					}

					out[3] = alpha * 0.25f;
				}
			}

			level = std::move(next);
			w = nw;
			h = nh;
		}
	}

} // namespace

SWIM_TEST("Engine.MeshLibrary", "TextureMipChainsMatchTheReferenceAndBuildQuickly")
{
	// Odd sizes, cut-out alpha and both colour spaces.
	for (const bool srgb : { true, false })
	{
		const std::uint32_t w = 37;
		const std::uint32_t h = 12;
		std::vector<std::uint8_t> pixels(std::size_t(w) * h * 4);
		std::uint32_t seed = 12345;

		for (std::size_t i = 0; i < pixels.size(); ++i)
		{
			seed = seed * 1664525u + 1013904223u;
			pixels[i] = static_cast<std::uint8_t>(i % 4 == 3 && (seed >> 28) < 4 ? 0 : seed >> 24);
		}

		const auto asset = Engine::MakeTextureAsset(w, h, pixels, srgb, true);
		const auto reference = ReferenceMips(w, h, pixels, srgb);
		const auto& payload = asset.Payloads.front();
		SWIM_REQUIRE(payload.Mips.size() == reference.size());
		int worst = 0;

		for (std::size_t m = 0; m < reference.size(); ++m)
		{
			const auto* bytes = reinterpret_cast<const std::uint8_t*>(payload.Bytes.data() + payload.Mips[m].OffsetBytes);
			SWIM_REQUIRE(payload.Mips[m].SizeBytes == reference[m].size());

			for (std::size_t i = 0; i < reference[m].size(); ++i)
			{
				worst = std::max(worst, std::abs(int(bytes[i]) - int(reference[m][i])));
			}
		}

		SWIM_CHECK(worst <= 1);
		SWIM_CHECK(Engine::MakeTextureAsset(w, h, pixels, srgb, false).Payloads.front().Mips.size() == 1);
	}

	// A large sRGB image: the whole chain in well under a frame budget's worth of the old cost.
	const std::uint32_t size = 2048;
	std::vector<std::uint8_t> big(std::size_t(size) * size * 4);

	for (std::size_t i = 0; i < big.size(); ++i)
	{
		big[i] = static_cast<std::uint8_t>((i * 2654435761u) >> 24);
	}

	const auto start = std::chrono::steady_clock::now();
	const auto asset = Engine::MakeTextureAsset(size, size, big, true, true);
	const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
	const auto referenceStart = std::chrono::steady_clock::now();
	const auto reference = ReferenceMips(size, size, big, true);
	const double referenceMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - referenceStart).count();
	std::printf("             2048x2048 sRGB mip chain: %.1f ms (reference %.1f ms)\n", ms, referenceMs);
	SWIM_CHECK(asset.Payloads.front().Mips.size() == 12);
	SWIM_CHECK(ms < referenceMs * 0.5);
}
