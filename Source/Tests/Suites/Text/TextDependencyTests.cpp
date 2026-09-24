// Item 79 dependency gate: FreeType, HarfBuzz and msdfgen are pinned, built and
// linked. These cases only prove that each library is usable from Swim's build;
// the text module and its real coverage come with the rest of Phase 20.

#include "Tests/Framework/Test.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb.h>
#include <msdfgen.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>

SWIM_TEST("Text.Dependencies", "FreeTypeInitializesAndRejectsNonFontData")
{
	FT_Library library = nullptr;
	SWIM_REQUIRE_EQUAL(FT_Init_FreeType(&library), FT_Err_Ok);

	FT_Int major = 0;
	FT_Int minor = 0;
	FT_Int patch = 0;
	FT_Library_Version(library, &major, &minor, &patch);
	SWIM_CHECK_EQUAL(major, 2);
	SWIM_CHECK_EQUAL(minor, 14);
	SWIM_CHECK_EQUAL(patch, 3);

	// Every compiled-in driver refuses the bytes (which one answers, and with which
	// format error, is FreeType's business).
	const std::array<FT_Byte, 256> notAFont{};
	FT_Face face = nullptr;
	SWIM_CHECK(FT_New_Memory_Face(library, notAFont.data(), FT_Long(notAFont.size()), 0, &face) != FT_Err_Ok);
	SWIM_CHECK(face == nullptr);

	SWIM_CHECK_EQUAL(FT_Done_FreeType(library), FT_Err_Ok);
}

SWIM_TEST("Text.Dependencies", "HarfBuzzSegmentsAndShapesUtf8")
{
	SWIM_CHECK(hb_version_atleast(14, 5, 0));

	// "Hi" plus a four-byte code point (U+1F600): three code points, UTF-8 clusters 0, 1 and 2.
	constexpr std::string_view latin = "Hi\xF0\x9F\x98\x80";
	hb_buffer_t* buffer = hb_buffer_create();
	hb_buffer_add_utf8(buffer, latin.data(), int(latin.size()), 0, -1);
	hb_buffer_guess_segment_properties(buffer);
	SWIM_CHECK_EQUAL(hb_buffer_get_length(buffer), 3u);
	SWIM_CHECK(hb_buffer_get_direction(buffer) == HB_DIRECTION_LTR);
	SWIM_CHECK(hb_buffer_get_script(buffer) == HB_SCRIPT_LATIN);

	// No real font yet: a font over an empty face maps every code point to glyph 0 (.notdef).
	hb_face_t* face = hb_face_create(hb_blob_get_empty(), 0);
	hb_font_t* font = hb_font_create(face);
	hb_shape(font, buffer, nullptr, 0);
	unsigned int count = 0;
	const hb_glyph_info_t* glyphs = hb_buffer_get_glyph_infos(buffer, &count);
	SWIM_REQUIRE_EQUAL(count, 3u);
	for (unsigned int i = 0; i < count; ++i)
	{
		SWIM_CHECK_EQUAL(glyphs[i].codepoint, 0u);
	}
	SWIM_CHECK_EQUAL(glyphs[1].cluster, 1u);
	SWIM_CHECK_EQUAL(glyphs[2].cluster, 2u);
	hb_buffer_destroy(buffer);
	hb_font_destroy(font);
	hb_face_destroy(face);

	// Built-in Unicode data: Arabic is detected and runs right to left.
	constexpr std::string_view arabic = "\xD8\xB3\xD9\x84\xD8\xA7\xD9\x85";
	hb_buffer_t* rtl = hb_buffer_create();
	hb_buffer_add_utf8(rtl, arabic.data(), int(arabic.size()), 0, -1);
	hb_buffer_guess_segment_properties(rtl);
	SWIM_CHECK(hb_buffer_get_script(rtl) == HB_SCRIPT_ARABIC);
	SWIM_CHECK(hb_buffer_get_direction(rtl) == HB_DIRECTION_RTL);
	hb_buffer_destroy(rtl);
}

SWIM_TEST("Text.Dependencies", "MsdfgenRendersASquareDistanceField")
{
	// A unit square in shape units, rendered into 16 x 16 pixels with a
	// 4-pixel margin: 8 pixels per unit, a distance range of 0.5 units.
	msdfgen::Shape shape;
	msdfgen::Contour& contour = shape.addContour();
	contour.addEdge(msdfgen::EdgeHolder(msdfgen::Point2(0, 0), msdfgen::Point2(1, 0)));
	contour.addEdge(msdfgen::EdgeHolder(msdfgen::Point2(1, 0), msdfgen::Point2(1, 1)));
	contour.addEdge(msdfgen::EdgeHolder(msdfgen::Point2(1, 1), msdfgen::Point2(0, 1)));
	contour.addEdge(msdfgen::EdgeHolder(msdfgen::Point2(0, 1), msdfgen::Point2(0, 0)));
	SWIM_REQUIRE(shape.validate());
	shape.normalize();
	shape.orientContours(); // Outer contours wind as msdfgen expects, whatever the source winding.
	msdfgen::edgeColoringSimple(shape, 3.0);

	msdfgen::Bitmap<float, 3> field(16, 16);
	const msdfgen::SDFTransformation transformation(
		msdfgen::Projection(msdfgen::Vector2(8.0), msdfgen::Vector2(0.5)), msdfgen::DistanceMapping(msdfgen::Range(0.5)));
	msdfgen::generateMSDF(field, shape, transformation);

	const auto median = [&field](int x, int y)
	{
		const float* texel = field(x, y);
		return std::max(std::min(texel[0], texel[1]), std::min(std::max(texel[0], texel[1]), texel[2]));
	};
	// Encoded distance: 0.5 on the edge, above inside, below outside.
	SWIM_CHECK(median(8, 8) > 0.5f);
	SWIM_CHECK(median(0, 0) < 0.5f);
	SWIM_CHECK(median(15, 15) < 0.5f);
	SWIM_CHECK(median(8, 1) < 0.5f);
	SWIM_CHECK(median(8, 6) > 0.5f);
}
