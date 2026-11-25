#pragma once

#include "Engine/Systems/Text/FontFace.h"

#include <atomic>
#include <memory>

namespace Swim::Testing
{

	// A synthetic 16 x 16 grid pixel font (8-texel cells, 1 font pixel per texel at scale 1).
	// Block 0: 'A' columns 0-4 (advance 6), 'i' column 0 (advance 2), 'l' columns 0-1
	// (advance 3), 'W' columns 0-6 (advance 8), '|' column 1 (advance 3), all rows 1-6
	// opaque white; U+00E9 is empty (falls back). Block 1 is an all-red 'Ā' (U+0100) 4
	// columns wide. Other blocks are absent. `loads` counts LoadPage calls when given.
	inline std::shared_ptr<const Text::FontFace> MakeBitmapFontFixture(std::shared_ptr<std::atomic<int>> loads = {},
		float texelScale = 0.0f, std::uint32_t texelsPerCell = 8)
	{
		Text::BitmapFontDesc desc;
		desc.LoadPage = [loads, texelScale, texelsPerCell](std::uint32_t block) -> std::optional<Text::BitmapFontPage>
		{
			if (loads)
			{
				++*loads;
			}

			if (block > 1)
			{
				return std::nullopt;
			}

			const std::uint32_t cell = texelsPerCell;
			const std::uint32_t scale = cell / 8;
			Text::BitmapFontPage page;
			page.Width = page.Height = cell * 16;
			page.Rgba.assign(std::size_t(page.Width) * page.Height * 4, 0);
			page.FontPixelsPerTexel = texelScale;
			const auto fill = [&](std::uint32_t index, std::uint32_t firstColumn, std::uint32_t columns, std::uint8_t r, std::uint8_t g,
								  std::uint8_t b)
			{
				const std::uint32_t x0 = (index % 16) * cell;
				const std::uint32_t y0 = (index / 16) * cell;

				for (std::uint32_t y = 1 * scale; y < 7 * scale; ++y)
				{
					for (std::uint32_t x = firstColumn * scale; x < (firstColumn + columns) * scale; ++x)
					{
						auto* p = &page.Rgba[((std::size_t(y0) + y) * page.Width + x0 + x) * 4];
						p[0] = r;
						p[1] = g;
						p[2] = b;
						p[3] = 255;
					}
				}
			};

			if (block == 0)
			{
				fill('A', 0, 5, 255, 255, 255);
				fill('i', 0, 1, 255, 255, 255);
				fill('l', 0, 2, 255, 255, 255);
				fill('W', 0, 7, 255, 255, 255);
				fill('|', 1, 1, 255, 255, 255);
			}
			else
			{
				fill(0, 0, 4, 255, 0, 0);
			}

			return page;
		};
		return std::make_shared<Text::FontFace>(std::move(desc));
	}

} // namespace Swim::Testing
