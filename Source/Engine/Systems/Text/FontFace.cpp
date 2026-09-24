#include "Engine/Systems/Text/FontFace.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H
#include <hb.h>
#include <hb-ot.h>
#include <msdfgen.h>

#include <algorithm>
#include <cmath>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <string>

namespace Swim::Text
{
	namespace
	{
		void ValidateSize(float size)
		{
			if (!std::isfinite(size) || size <= 0.0f || size > 16384.0f)
			{
				throw std::invalid_argument("Font size must be finite and in (0, 16384]");
			}
		}

		struct OutlineBuilder
		{
			msdfgen::Shape Shape;
			msdfgen::Point2 Start;
			msdfgen::Point2 Current;
			double Scale = 1.0;
			std::exception_ptr Error;

			msdfgen::Point2 Point(const FT_Vector* point) const { return { point->x * Scale, point->y * Scale }; }

			void Close()
			{
				if (!Shape.contours.empty() && Current != Start)
				{
					Shape.contours.back().addEdge(msdfgen::EdgeHolder(Current, Start));
				}
			}

			// Exceptions must not unwind through FreeType's C callbacks.
			template <typename Function> int Apply(Function&& function) noexcept
			{
				try
				{
					function();
					return 0;
				}
				catch (...)
				{
					Error = std::current_exception();
					return 1;
				}
			}
		};
	} // namespace

	struct FontFace::Impl
	{
		std::vector<std::byte> Bytes;
		FT_Library Library = nullptr;
		FT_Face Face = nullptr;
		hb_font_t* Font = nullptr;
		mutable std::mutex Mutex;

		~Impl()
		{
			if (Font)
			{
				hb_font_destroy(Font);
			}
			if (Face)
			{
				FT_Done_Face(Face);
			}
			if (Library)
			{
				FT_Done_FreeType(Library);
			}
		}
	};

	FontFace::FontFace(std::span<const std::byte> bytes, std::uint32_t faceIndex) : impl(std::make_unique<Impl>())
	{
		// Bound both FreeType's signed length and HarfBuzz's unsigned length.
		if (bytes.empty() || bytes.size() > 256u * 1024u * 1024u || faceIndex > 65535)
		{
			throw std::invalid_argument("Invalid font bytes or collection index");
		}
		impl->Bytes.assign(bytes.begin(), bytes.end());
		if (FT_Init_FreeType(&impl->Library) != 0 ||
			FT_New_Memory_Face(impl->Library, reinterpret_cast<const FT_Byte*>(impl->Bytes.data()),
				static_cast<FT_Long>(impl->Bytes.size()), static_cast<FT_Long>(faceIndex), &impl->Face) != 0)
		{
			throw std::invalid_argument("FreeType could not load the font face");
		}
		if (!FT_IS_SCALABLE(impl->Face) || !FT_IS_SFNT(impl->Face) || impl->Face->units_per_EM == 0 ||
			FT_Select_Charmap(impl->Face, FT_ENCODING_UNICODE) != 0)
		{
			throw std::invalid_argument("Text requires a scalable Unicode OpenType font");
		}
		hb_blob_t* blob = hb_blob_create(reinterpret_cast<const char*>(impl->Bytes.data()), static_cast<unsigned>(impl->Bytes.size()),
			HB_MEMORY_MODE_READONLY, nullptr, nullptr);
		hb_face_t* face = hb_face_create(blob, faceIndex);
		impl->Font = hb_font_create(face);
		hb_face_destroy(face);
		hb_blob_destroy(blob);
		hb_ot_font_set_funcs(impl->Font);
		hb_font_set_scale(impl->Font, impl->Face->units_per_EM, impl->Face->units_per_EM);
		hb_font_make_immutable(impl->Font);
		if (hb_face_get_glyph_count(hb_font_get_face(impl->Font)) == 0)
		{
			throw std::invalid_argument("HarfBuzz could not load the font face");
		}
	}

	FontFace::~FontFace() = default;

	FontMetrics FontFace::GetMetrics(float size) const
	{
		ValidateSize(size);
		const float scale = size / impl->Face->units_per_EM;
		return { impl->Face->ascender * scale, impl->Face->descender * scale, impl->Face->height * scale };
	}

	std::uint32_t FontFace::GetGlyph(char32_t codePoint) const
	{
		hb_codepoint_t glyph = 0;
		hb_font_get_nominal_glyph(impl->Font, static_cast<hb_codepoint_t>(codePoint), &glyph);
		return glyph;
	}

	ShapedRun FontFace::Shape(std::string_view utf8, float size, const ShapeOptions& options) const
	{
		ShapedRun result;
		result.Metrics = GetMetrics(size);
		if (utf8.size() > 1024u * 1024u || options.Script.size() > 4 || options.Language.size() > 128)
		{
			throw std::invalid_argument("Text run or shaping properties exceed their limits");
		}
		if (options.Direction != TextDirection::Auto && options.Direction != TextDirection::LeftToRight &&
			options.Direction != TextDirection::RightToLeft)
		{
			throw std::invalid_argument("Invalid horizontal text direction");
		}
		std::unique_ptr<hb_buffer_t, decltype(&hb_buffer_destroy)> buffer(hb_buffer_create(), hb_buffer_destroy);
		hb_buffer_set_cluster_level(buffer.get(), HB_BUFFER_CLUSTER_LEVEL_MONOTONE_GRAPHEMES);
		hb_buffer_set_language(buffer.get(),
			hb_language_from_string(options.Language.empty() ? "und" : options.Language.data(),
				options.Language.empty() ? 3 : static_cast<int>(options.Language.size())));
		if (!options.Script.empty())
		{
			hb_buffer_set_script(buffer.get(), hb_script_from_string(options.Script.data(), static_cast<int>(options.Script.size())));
		}
		if (options.Direction != TextDirection::Auto)
		{
			hb_buffer_set_direction(buffer.get(), options.Direction == TextDirection::RightToLeft ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
		}
		hb_buffer_add_utf8(buffer.get(), utf8.empty() ? "" : utf8.data(), static_cast<int>(utf8.size()), 0, static_cast<int>(utf8.size()));
		hb_buffer_guess_segment_properties(buffer.get());
		const hb_feature_t features[] = { { HB_TAG('k', 'e', 'r', 'n'), options.Kerning ? 1u : 0u, 0, HB_FEATURE_GLOBAL_END },
			{ HB_TAG('l', 'i', 'g', 'a'), options.Ligatures ? 1u : 0u, 0, HB_FEATURE_GLOBAL_END },
			{ HB_TAG('c', 'l', 'i', 'g'), options.Ligatures ? 1u : 0u, 0, HB_FEATURE_GLOBAL_END } };
		hb_shape(impl->Font, buffer.get(), features, 3);
		if (!hb_buffer_allocation_successful(buffer.get()))
		{
			throw std::bad_alloc();
		}
		result.Direction =
			hb_buffer_get_direction(buffer.get()) == HB_DIRECTION_RTL ? TextDirection::RightToLeft : TextDirection::LeftToRight;
		unsigned count = 0;
		const auto* glyphs = hb_buffer_get_glyph_infos(buffer.get(), &count);
		const auto* positions = hb_buffer_get_glyph_positions(buffer.get(), nullptr);
		const float scale = size / impl->Face->units_per_EM;
		result.Glyphs.reserve(count);
		for (unsigned i = 0; i < count; ++i)
		{
			result.Glyphs.push_back({ glyphs[i].codepoint, glyphs[i].cluster, positions[i].x_advance * scale,
				positions[i].y_advance * scale, positions[i].x_offset * scale, positions[i].y_offset * scale });
			result.AdvanceX += result.Glyphs.back().AdvanceX;
			result.MissingGlyphs += glyphs[i].codepoint == 0 ? 1u : 0u;
		}
		return result;
	}

	FontFace::GlyphBitmap FontFace::Rasterize(std::uint32_t glyph, float emSize, float range, std::uint32_t maxDimension) const
	{
		std::lock_guard lock(impl->Mutex);
		if (glyph >= static_cast<std::uint32_t>(impl->Face->num_glyphs))
		{
			throw std::out_of_range("Glyph index is outside the font");
		}
		// Size at units-per-em without hinting also preserves fractional coordinates
		// in variable fonts, unlike FT_LOAD_NO_SCALE's integer font units.
		if (FT_Set_Char_Size(impl->Face, 0, impl->Face->units_per_EM * 64L, 72, 72) != 0 ||
			FT_Load_Glyph(impl->Face, glyph, FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP) != 0 ||
			impl->Face->glyph->format != FT_GLYPH_FORMAT_OUTLINE)
		{
			throw std::runtime_error("Unable to load glyph outline");
		}
		GlyphBitmap result;
		if (impl->Face->glyph->outline.n_points == 0)
		{
			return result; // Spaces advance but consume no atlas texels.
		}
		OutlineBuilder builder;
		builder.Scale = static_cast<double>(emSize) / (64.0 * impl->Face->units_per_EM);
		FT_Outline_Funcs callbacks{};
		callbacks.move_to = [](const FT_Vector* to, void* user)
		{
			auto& b = *static_cast<OutlineBuilder*>(user);
			return b.Apply(
				[&]
				{
					b.Close();
					b.Shape.addContour();
					b.Current = b.Start = b.Point(to);
				});
		};
		callbacks.line_to = [](const FT_Vector* to, void* user)
		{
			auto& b = *static_cast<OutlineBuilder*>(user);
			return b.Apply(
				[&]
				{
					b.Shape.contours.back().addEdge(msdfgen::EdgeHolder(b.Current, b.Point(to)));
					b.Current = b.Point(to);
				});
		};
		callbacks.conic_to = [](const FT_Vector* control, const FT_Vector* to, void* user)
		{
			auto& b = *static_cast<OutlineBuilder*>(user);
			return b.Apply(
				[&]
				{
					b.Shape.contours.back().addEdge(msdfgen::EdgeHolder(b.Current, b.Point(control), b.Point(to)));
					b.Current = b.Point(to);
				});
		};
		callbacks.cubic_to = [](const FT_Vector* first, const FT_Vector* second, const FT_Vector* to, void* user)
		{
			auto& b = *static_cast<OutlineBuilder*>(user);
			return b.Apply(
				[&]
				{
					b.Shape.contours.back().addEdge(msdfgen::EdgeHolder(b.Current, b.Point(first), b.Point(second), b.Point(to)));
					b.Current = b.Point(to);
				});
		};
		const int error = FT_Outline_Decompose(&impl->Face->glyph->outline, &callbacks, &builder);
		if (builder.Error)
		{
			std::rethrow_exception(builder.Error);
		}
		if (error != 0)
		{
			throw std::runtime_error("Unable to decompose glyph outline");
		}
		builder.Close();
		builder.Shape.normalize();
		builder.Shape.orientContours();
		if (!builder.Shape.validate())
		{
			throw std::runtime_error("Invalid MSDF outline");
		}
		const auto bounds = builder.Shape.getBounds();
		const double padding = std::ceil(range * 0.5) + 1.0;
		const double left = std::floor(bounds.l - padding);
		const double bottom = std::floor(bounds.b - padding);
		const double right = std::ceil(bounds.r + padding);
		const double top = std::ceil(bounds.t + padding);
		if (!std::isfinite(left + bottom + right + top) || right <= left || top <= bottom || right - left > maxDimension ||
			top - bottom > maxDimension)
		{
			throw std::length_error("Glyph exceeds the atlas page size");
		}
		result.Width = static_cast<std::uint32_t>(right - left);
		result.Height = static_cast<std::uint32_t>(top - bottom);
		result.Left = static_cast<float>(left);
		result.Top = static_cast<float>(top);
		msdfgen::edgeColoringSimple(builder.Shape, 3.0);
		msdfgen::Bitmap<float, 3> bitmap(static_cast<int>(result.Width), static_cast<int>(result.Height));
		msdfgen::generateMSDF(bitmap, builder.Shape,
			msdfgen::SDFTransformation(msdfgen::Projection(msdfgen::Vector2(1.0), msdfgen::Vector2(-left, -bottom)),
				msdfgen::DistanceMapping(msdfgen::Range(range))));
		result.Pixels.resize(static_cast<std::size_t>(result.Width) * result.Height * 3);
		for (std::uint32_t y = 0; y < result.Height; ++y)
		{
			for (std::uint32_t x = 0; x < result.Width; ++x)
			{
				const float* sample = bitmap(static_cast<int>(x), static_cast<int>(result.Height - y - 1));
				for (std::uint32_t c = 0; c < 3; ++c)
				{
					if (!std::isfinite(sample[c]))
					{
						throw std::runtime_error("MSDF generation produced a non-finite distance");
					}
					result.Pixels[(static_cast<std::size_t>(y) * result.Width + x) * 3 + c] =
						static_cast<std::uint8_t>(std::lround(std::clamp(sample[c], 0.0f, 1.0f) * 255.0f));
				}
			}
		}
		return result;
	}
} // namespace Swim::Text
