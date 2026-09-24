# Text fixture

`SwimTextFixture.ttf` is a renamed subset of DejaVu Sans, used only by the portable Text/UI regression suites. `LICENSE-DejaVu.txt` retains the original package's copyright/license notices, including the Bitstream Vera permission notice. Do not use this deliberately incomplete test font as the application's fallback font.

The subset contains U+0020..U+007E, U+0600..U+06FF, U+0301, U+00E9, U+FFFD and U+1F600, plus the glyph closure needed by the font's retained shaping tables. It covers kerning, Latin ligatures, combining marks, contextual Arabic and a non-BMP glyph without relying on installed fonts.

Generated using fontTools 4.61.1 `subset.Options()` defaults, `Subsetter.populate(unicodes=...)`, `Subsetter.subset(font)`, then renaming name table IDs 1/4/6 to `SwimTextFixture` before saving. All tests refer to it through the CMake-defined absolute `SWIM_TEXT_FONT_FIXTURE_PATH`, independent of the test process's current directory.
