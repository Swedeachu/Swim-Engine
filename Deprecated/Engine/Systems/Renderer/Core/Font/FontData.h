#pragma once 

#include <string>
#include <memory>
#include "Engine/Systems/Renderer/Core/Textures/Texture2D.h"

namespace Engine
{

	// Simple vertex for text quads in EM space (used for rendering)
	struct TextVertex
	{
		glm::vec2 posEm; // planeBounds + pen offset (EM units)
		glm::vec2 uv;    // msdf atlas uv
	};

	// Simple rectangle used for both plane-space and UV/atlas bounds.
	struct FontRect
	{
		float left{ 0.0f };
		float bottom{ 0.0f };
		float right{ 0.0f };
		float top{ 0.0f };
	};

	// Glyph metrics stored per codepoint.
	struct Glyph
	{
		uint32_t codepoint{ 0 };  // Unicode scalar value.
		float    advance{ 0.0f }; // Advance in font units (same space as 'metrics').

		// Signed distance field geometry:
		FontRect plane;                 // Glyph quad in "plane" (font) space (usually normalized em).
		FontRect atlasPx;               // Pixel rectangle in the atlas texture.
		FontRect uv;                    // Normalized UVs derived from atlasPx / atlas size.
	};

	enum class FontYOrigin : uint8_t
	{
		Top,
		Bottom,
	};

	struct FontInfo
	{
		// ---- Identification / resources ----
		std::string fontName;                       // Usually folder name like "Roboto".
		std::shared_ptr<Texture2D> msdfAtlas;       // Shared atlas texture (RGB = MSDF, A optional).

		// ---- Atlas description (from "atlas") ----
		int   atlasWidth{ 0 };                // Pixels
		int   atlasHeight{ 0 };               // Pixels
		int   atlasEMSize{ 0 };               // e.g., "size": 128 (EM size used by the generator).
		float distanceRange{ 0.0f };          // MSDF pixel range (for shader 'pxRange').
		FontYOrigin yOrigin{ FontYOrigin::Bottom };   // "bottom" or "top" (affects v-UV flip if ever needed).

		// ---- Font metrics (from "metrics") ----
		float lineHeight{ 0.0f };
		float ascender{ 0.0f };
		float descender{ 0.0f };
		float underlineY{ 0.0f };
		float underlineThickness{ 0.0f };

		// ---- Glyphs & kerning ----
		std::unordered_map<uint32_t, Glyph> glyphs;

		// Kerning keyed by (left<<32 | right) -> adjustment in same space as 'advance'.
		std::unordered_map<uint64_t, float> kerning;

		static uint64_t PackKerningKey(uint32_t left, uint32_t right)
		{
			return (uint64_t(left) << 32) | uint64_t(right);
		}

		const Glyph* GetGlyph(uint32_t cp) const
		{
			auto it = glyphs.find(cp);
			if (it == glyphs.end())
			{
				return nullptr;
			}
			else
			{
				return &it->second;
			}
		}

		float GetKerning(uint32_t left, uint32_t right) const
		{
			auto it = kerning.find(PackKerningKey(left, right));
			if (it == kerning.end())
			{
				return 0.0f;
			}
			else
			{
				return it->second;
			}
		}

	};

} // Namespace Engine
