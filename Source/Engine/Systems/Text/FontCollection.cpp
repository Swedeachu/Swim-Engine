#include "Engine/Systems/Text/FontCollection.h"

#include <hb.h>

#include <stdexcept>

namespace Swim::Text
{
	FontCollection::FontCollection(std::vector<std::shared_ptr<const FontFace>> chain) : faces(std::move(chain))
	{
		if (faces.empty() || faces.size() > MaxFaces)
		{
			throw std::invalid_argument("A font collection needs 1 .. 64 faces");
		}
		for (const auto& face : faces)
		{
			if (!face)
			{
				throw std::invalid_argument("A font collection cannot hold a null face");
			}
		}
	}

	std::shared_ptr<const FontCollection> FontCollection::Single(std::shared_ptr<const FontFace> face)
	{
		return std::make_shared<const FontCollection>(std::vector<std::shared_ptr<const FontFace>>{ std::move(face) });
	}

	bool FontCollection::IsIgnorable(char32_t c)
	{
		// Controls, then Default_Ignorable_Code_Point ranges that matter for rendering.
		return c < 0x20 || (c >= 0x7F && c < 0xA0) || c == 0x00AD || c == 0x034F || c == 0x061C || (c >= 0x115F && c <= 0x1160) ||
			(c >= 0x17B4 && c <= 0x17B5) || (c >= 0x180B && c <= 0x180F) || (c >= 0x200B && c <= 0x200F) || (c >= 0x2028 && c <= 0x202E) ||
			(c >= 0x2060 && c <= 0x206F) || c == 0x3164 || (c >= 0xFE00 && c <= 0xFE0F) || c == 0xFEFF || c == 0xFFA0 ||
			(c >= 0xFFF0 && c <= 0xFFF8) || (c >= 0x1BCA0 && c <= 0x1BCA3) || (c >= 0x1D173 && c <= 0x1D17A) ||
			(c >= 0xE0000 && c <= 0xE0FFF);
	}

	bool FontCollection::IsNeutral(char32_t c)
	{
		switch (hb_unicode_general_category(hb_unicode_funcs_get_default(), c))
		{
		case HB_UNICODE_GENERAL_CATEGORY_SPACE_SEPARATOR:
		case HB_UNICODE_GENERAL_CATEGORY_LINE_SEPARATOR:
		case HB_UNICODE_GENERAL_CATEGORY_PARAGRAPH_SEPARATOR:
		case HB_UNICODE_GENERAL_CATEGORY_CONNECT_PUNCTUATION:
		case HB_UNICODE_GENERAL_CATEGORY_DASH_PUNCTUATION:
		case HB_UNICODE_GENERAL_CATEGORY_CLOSE_PUNCTUATION:
		case HB_UNICODE_GENERAL_CATEGORY_FINAL_PUNCTUATION:
		case HB_UNICODE_GENERAL_CATEGORY_INITIAL_PUNCTUATION:
		case HB_UNICODE_GENERAL_CATEGORY_OTHER_PUNCTUATION:
		case HB_UNICODE_GENERAL_CATEGORY_OPEN_PUNCTUATION:
		case HB_UNICODE_GENERAL_CATEGORY_CURRENCY_SYMBOL:
		case HB_UNICODE_GENERAL_CATEGORY_MODIFIER_SYMBOL:
		case HB_UNICODE_GENERAL_CATEGORY_MATH_SYMBOL:
		case HB_UNICODE_GENERAL_CATEGORY_OTHER_SYMBOL:
			return true;
		default:
			return false;
		}
	}

	bool FontCollection::Covers(std::uint32_t index, std::span<const char32_t> cluster) const
	{
		const auto& face = *faces.at(index);
		for (const char32_t c : cluster)
		{
			if (!IsIgnorable(c) && face.GetGlyph(c) == 0)
			{
				return false;
			}
		}
		return true;
	}

	std::uint32_t FontCollection::SelectFace(std::span<const char32_t> cluster, std::uint32_t preferred) const
	{
		if (cluster.empty())
		{
			return preferred < faces.size() ? preferred : 0;
		}
		if (preferred < faces.size() && IsNeutral(cluster.front()) && Covers(preferred, cluster))
		{
			return preferred;
		}
		for (std::uint32_t index = 0; index < faces.size(); ++index)
		{
			if (Covers(index, cluster))
			{
				return index;
			}
		}
		return 0;
	}
} // namespace Swim::Text
