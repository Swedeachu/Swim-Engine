# Text fixture

`SwimTextFixture.ttf` is a renamed subset of DejaVu Sans, used only by the portable Text/UI regression suites. `LICENSE-DejaVu.txt` retains the original package's copyright/license notices, including the Bitstream Vera permission notice. Do not use this deliberately incomplete test font as the application's fallback font.

The subset contains U+0020..U+007E, U+0600..U+06FF, U+0301, U+00E9, U+FFFD and U+1F600, plus the glyph closure needed by the font's retained shaping tables. It covers kerning, Latin ligatures, combining marks, contextual Arabic and a non-BMP glyph without relying on installed fonts.

Generated using fontTools 4.61.1 `subset.Options()` defaults, `Subsetter.populate(unicodes=...)`, `Subsetter.subset(font)`, then renaming name table IDs 1/4/6 to `SwimTextFixture` before saving. All tests refer to it through the CMake-defined absolute `SWIM_TEXT_FONT_FIXTURE_PATH`, independent of the test process's current directory.

# Fallback fixture

`SwimTextFallbackFixture.ttf` is a second renamed subset of DejaVu Sans (same license, same `LICENSE-DejaVu.txt`). It covers Greek (U+0370..U+03FF, less the unassigned points) and Hebrew (U+05B0..U+05C7, U+05D0..U+05EA, U+05F0..U+05F4) and deliberately **no** Latin or Arabic, so `FontCollection` tests can prove per-cluster fallback: `LoadTextFontChain()` returns `{ SwimTextFixture, SwimTextFallbackFixture }`, and Hebrew or Greek clusters must select face 1 while neutrals stay in the surrounding run's face.

Generated with fontTools 4.61.1 exactly like the primary fixture, with those ranges as the unicodes and name IDs 1/4/6 renamed to `SwimTextFallbackFixture`. Tests refer to it through the CMake-defined absolute `SWIM_TEXT_FALLBACK_FONT_FIXTURE_PATH`.
