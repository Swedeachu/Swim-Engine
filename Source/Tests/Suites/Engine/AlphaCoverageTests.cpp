#include "Engine/Math/AlphaCoverage.h"
#include "Tests/Framework/Test.h"

#include <cmath>
#include <cstdint>
#include <vector>

using namespace Engine;

namespace
{

	// A 64 x 64 card of scattered one-texel leaves: about 35% opaque, the rest clear.
	std::vector<std::byte> Leaves()
	{
		std::vector<std::byte> rgba(64 * 64 * 4, std::byte{ 0 });

		for (std::uint32_t y = 0; y < 64; ++y)
		{
			for (std::uint32_t x = 0; x < 64; ++x)
			{
				const std::uint32_t hash = (((x * 73856093u) ^ (y * 19349663u)) & 0xffffu) * 2654435761u;
				rgba[(y * 64 + x) * 4 + 3] = std::byte{ static_cast<std::uint8_t>((hash >> 16) % 100 < 35 ? 255 : 0) };
			}
		}

		return rgba;
	}

	// 2 x 2 box filter.
	std::vector<std::byte> Half(const std::vector<std::byte>& rgba, int size)
	{
		const int half = size / 2;
		std::vector<std::byte> out(half * half * 4, std::byte{ 0 });

		for (int y = 0; y < half; ++y)
		{
			for (int x = 0; x < half; ++x)
			{
				int sum = 0;

				for (int i = 0; i < 4; ++i)
				{
					sum += std::to_integer<int>(rgba[((2 * y + i / 2) * size + 2 * x + i % 2) * 4 + 3]);
				}

				out[(y * half + x) * 4 + 3] = std::byte{ static_cast<std::uint8_t>((sum + 2) / 4) };
			}
		}

		return out;
	}

} // namespace

SWIM_TEST("Engine.AlphaCoverage", "MipsKeepTheTopMipsCutoutCoverage")
{
	constexpr float Cutoff = 0.5f;
	const auto top = Leaves();
	const float target = AlphaCoverage(top, Cutoff);
	SWIM_CHECK(target > 0.3f && target < 0.4f);

	// Averaged down twice, most leaves fall under the cutoff: the card thins away.
	auto mip = Half(Half(top, 64), 32);
	SWIM_CHECK(AlphaCoverage(mip, Cutoff) < 0.5f * target);

	PreserveAlphaCoverage(mip, Cutoff, target);
	SWIM_CHECK(std::abs(AlphaCoverage(mip, Cutoff) - target) < 0.06f);

	// Off (cutoff 0) and empty input leave the data alone.
	auto untouched = Half(top, 64);
	const auto before = untouched;
	PreserveAlphaCoverage(untouched, 0.0f, 1.0f);
	SWIM_CHECK(untouched == before);
	std::vector<std::byte> empty;
	PreserveAlphaCoverage(empty, Cutoff, 0.5f);
	SWIM_CHECK(AlphaCoverage(empty, Cutoff) == 0.0f);
}
