#include "PCH.h"
#include "FontPool.h"
#include "Engine/Systems/Renderer/Core/Textures/TexturePool.h"

#include <fstream>
#include <sstream>
#include <iostream>

namespace Engine
{

	FontPool& FontPool::GetInstance()
	{
		static FontPool instance;
		return instance;
	}

	void FontPool::LoadAllRecursively()
	{
		namespace fs = std::filesystem;

		const fs::path fontsRoot{ "Assets\\Font" };

		if (!fs::exists(fontsRoot))
		{
			std::cerr << "[FontPool] Fonts root does not exist: " << fontsRoot << "\n";
			return;
		}

		// 1) Collect directories first (no lock, fast)
		std::vector<fs::path> dirs;
		for (fs::recursive_directory_iterator it(fontsRoot), end; it != end; ++it)
		{
			if (it->is_directory())
			{
				dirs.emplace_back(it->path());
			}
		}

		// 2) Parse each directory (no lock during heavy work)
		for (const auto& dir : dirs)
		{
			ParseFontDirectory(dir);
		}

	#ifdef _DEBUG
		// 3) Print summary (takes its own lock)
		PrintMapDebug();
	#endif
	}

	void FontPool::PrintMapDebug() const
	{
		std::lock_guard<std::mutex> lock(poolMutex);

		auto YOriginToString = [](FontYOrigin y) -> const char*
		{
			switch (y)
			{
				case FontYOrigin::Top: { return "top"; }
				case FontYOrigin::Bottom: { return "bottom"; }
				default: { return "unknown"; }
			}
		};

		auto CpToPretty = [](uint32_t cp) -> std::string
		{
			if (cp >= 32 && cp < 127)
			{
				return std::string(1, static_cast<char>(cp));
			}
			else
			{
				std::ostringstream oss;
				oss << "U+" << std::uppercase << std::hex << cp;
				return oss.str();
			}
		};

		std::cout << "[FontPool] ---- Loaded fonts (" << fontPool.size() << ") ----\n";
		for (const auto& kv : fontPool)
		{
			const std::string& key = kv.first;
			const auto& fi = kv.second;

			std::cout << " \"" << key << "\" (name: " << fi->fontName << ")\n";
			std::cout << "    Atlas: "
				<< fi->atlasWidth << "x" << fi->atlasHeight
				<< ", EM=" << fi->atlasEMSize
				<< ", pxRange=" << fi->distanceRange
				<< ", yOrigin=" << YOriginToString(fi->yOrigin)
				<< ", texture=" << (fi->msdfAtlas ? "OK" : "NULL") << "\n";

			std::cout << "    Metrics: "
				<< "lineHeight=" << fi->lineHeight
				<< ", asc=" << fi->ascender
				<< ", desc=" << fi->descender
				<< ", underlineY=" << fi->underlineY
				<< ", underlineThick=" << fi->underlineThickness << "\n";

			std::cout << "    Glyphs: " << fi->glyphs.size()
				<< ", Kerning pairs: " << fi->kerning.size()
				<< "\n Printing 'A, a, 0' values as demo:\n";

			const uint32_t samples[] = { 'A', 'a', '0' };
			for (uint32_t cp : samples)
			{
				auto it = fi->glyphs.find(cp);
				if (it != fi->glyphs.end())
				{
					const Glyph& g = it->second;
					std::cout << "      " << CpToPretty(cp)
						<< "  advance=" << g.advance
						<< "  plane=(" << g.plane.left << "," << g.plane.bottom
						<< ")-(" << g.plane.right << "," << g.plane.top << ")"
						<< "  uv=(" << g.uv.left << "," << g.uv.bottom
						<< ")-(" << g.uv.right << "," << g.uv.top << ")\n";
				}
			}
		}

		std::cout.flush();
	}

	std::shared_ptr<FontInfo> FontPool::GetFontInfo(const std::string& name) const
	{
		std::lock_guard<std::mutex> lock(poolMutex);

		auto it = fontPool.find(name);
		if (it == fontPool.end())
		{
			throw std::runtime_error("[FontPool] Font not found: " + name);
		}
		else
		{
			return it->second;
		}
	}

	void FontPool::ParseFontDirectory(const std::filesystem::path& dirPath)
	{
		namespace fs = std::filesystem;

		if (!fs::exists(dirPath) || !fs::is_directory(dirPath))
		{
			return;
		}

		const std::string dirStem = dirPath.stem().string();
		fs::path jsonPath;
		fs::path pngPath;

		// Prefer JSON whose stem matches the directory name.
		for (const auto& entry : fs::directory_iterator(dirPath))
		{
			if (entry.is_regular_file() && entry.path().extension() == ".json")
			{
				if (entry.path().stem().string() == dirStem)
				{
					jsonPath = entry.path();
					break;
				}
			}
		}

		if (jsonPath.empty())
		{
			for (const auto& entry : fs::directory_iterator(dirPath))
			{
				if (entry.is_regular_file() && entry.path().extension() == ".json")
				{
					jsonPath = entry.path();
					break;
				}
			}
		}

		if (jsonPath.empty())
		{
			return; // not a font directory
		}

		// Prefer PNG matching the JSON stem; else first PNG.
		const fs::path preferredPng = jsonPath.parent_path() / (jsonPath.stem().string() + ".png");
		if (fs::exists(preferredPng))
		{
			pngPath = preferredPng;
		}
		else
		{
			for (const auto& entry : fs::directory_iterator(dirPath))
			{
				if (entry.is_regular_file() && entry.path().extension() == ".png")
				{
					pngPath = entry.path();
					break;
				}
			}
		}
		if (pngPath.empty())
		{
			std::cerr << "[FontPool] No PNG atlas found for " << jsonPath << "\n";
			return;
		}

		// Read JSON
		std::string jsonText;
		if (!LoadJsonFile(jsonPath, jsonText))
		{
			std::cerr << "[FontPool] Failed to read JSON: " << jsonPath << "\n";
			return;
		}

		// Parse JSON
		nlohmann::json j;
		try
		{
			j = nlohmann::json::parse(jsonText);
		}
		catch (const std::exception& e)
		{
			std::cerr << "[FontPool] JSON parse error in " << jsonPath << " : " << e.what() << "\n";
			return;
		}

		// Populate the shared object in-place to avoid copying big maps.
		auto fi = std::make_shared<FontInfo>();
		try
		{
			PopulateFromJson(*fi, j, pngPath, dirStem);
		}
		catch (const std::exception& e)
		{
			std::cerr << "[FontPool] Populate error in " << jsonPath << " : " << e.what() << "\n";
			return;
		}

		// Insert under lock (short critical section)
		{
			std::lock_guard<std::mutex> lock(poolMutex);
			fontPool[fi->fontName] = std::move(fi);
		}
	}

	bool FontPool::LoadJsonFile(const std::filesystem::path& path, std::string& out)
	{
		std::ifstream f(path, std::ios::binary);
		if (!f)
		{
			return false;
		}

		std::ostringstream ss;
		ss << f.rdbuf();
		out = ss.str();

		return true;
	}

	FontYOrigin FontPool::ParseYOrigin(const std::string& s)
	{
		if (s == "top")
		{
			return FontYOrigin::Top;
		}
		else
		{
			// Default to "bottom" if unknown/missing.
			return FontYOrigin::Bottom;
		}
	}

	void FontPool::PopulateFromJson(FontInfo& out,
		const nlohmann::json& j,
		const std::filesystem::path& pngPath,
		const std::string& fontName)
	{
		out.fontName = fontName;

		// --- Atlas
		const auto& atlas = j.at("atlas");
		out.distanceRange = atlas.value("distanceRange", 0.0f);
		out.atlasEMSize = atlas.value("size", 0);
		out.atlasWidth = atlas.value("width", 0);
		out.atlasHeight = atlas.value("height", 0);
		out.yOrigin = ParseYOrigin(atlas.value("yOrigin", std::string("bottom")));

		// --- Metrics
		if (j.contains("metrics"))
		{
			const auto& m = j.at("metrics");
			out.lineHeight = m.value("lineHeight", 0.0f);
			out.ascender = m.value("ascender", 0.0f);
			out.descender = m.value("descender", 0.0f);
			out.underlineY = m.value("underlineY", 0.0f);
			out.underlineThickness = m.value("underlineThickness", 0.0f);
		}

		// --- Texture
		TexturePool& tp = TexturePool::GetInstance();
		out.msdfAtlas = tp.LoadTexture(pngPath.string(), false);
		if (!out.msdfAtlas)
		{
			throw std::runtime_error("Failed to load atlas texture: " + pngPath.string());
		}

		// --- Glyphs
		if (j.contains("glyphs") && j["glyphs"].is_array())
		{
			const float invW = (out.atlasWidth > 0) ? (1.0f / float(out.atlasWidth)) : 0.0f;
			const float invH = (out.atlasHeight > 0) ? (1.0f / float(out.atlasHeight)) : 0.0f;

			out.glyphs.clear();
			out.glyphs.reserve(j["glyphs"].size()); // avoid rehash

			for (const auto& g : j["glyphs"])
			{
				Glyph glyph{};
				glyph.codepoint = g.value("unicode", 0u);
				glyph.advance = g.value("advance", 0.0f);

				if (g.contains("planeBounds"))
				{
					const auto& p = g["planeBounds"];
					glyph.plane.left = p.value("left", 0.0f);
					glyph.plane.bottom = p.value("bottom", 0.0f);
					glyph.plane.right = p.value("right", 0.0f);
					glyph.plane.top = p.value("top", 0.0f);
				}

				if (g.contains("atlasBounds"))
				{
					const auto& a = g["atlasBounds"];
					glyph.atlasPx.left = a.value("left", 0.0f);
					glyph.atlasPx.bottom = a.value("bottom", 0.0f);
					glyph.atlasPx.right = a.value("right", 0.0f);
					glyph.atlasPx.top = a.value("top", 0.0f);

					glyph.uv.left = glyph.atlasPx.left * invW;
					glyph.uv.right = glyph.atlasPx.right * invW;

					if (out.yOrigin == FontYOrigin::Bottom)
					{
						glyph.uv.bottom = glyph.atlasPx.bottom * invH;
						glyph.uv.top = glyph.atlasPx.top * invH;
					}
					else
					{
						glyph.uv.bottom = 1.0f - glyph.atlasPx.top * invH;
						glyph.uv.top = 1.0f - glyph.atlasPx.bottom * invH;
					}
				}

				out.glyphs.emplace(glyph.codepoint, std::move(glyph));
			}
		}

		// --- Kerning
		out.kerning.clear();
		if (j.contains("kerning"))
		{
			const auto& k = j["kerning"];
			if (k.is_array())
			{
				out.kerning.reserve(k.size());
				for (const auto& item : k)
				{
					const uint32_t u1 = item.value("unicode1", 0u);
					const uint32_t u2 = item.value("unicode2", 0u);
					const float    adv = item.value("advance", 0.0f);
					out.kerning.emplace(FontInfo::PackKerningKey(u1, u2), adv);
				}
			}
			else if (k.is_object())
			{
				for (auto it1 = k.begin(); it1 != k.end(); ++it1)
				{
					// stoul may throw on bad keys; you can wrap these in try/catch if assets are untrusted
					uint32_t u1 = 0;
					try { u1 = static_cast<uint32_t>(std::stoul(it1.key())); }
					catch (...) { continue; }

					const auto& inner = it1.value();
					if (!inner.is_object())
					{
						continue;
					}

					for (auto it2 = inner.begin(); it2 != inner.end(); ++it2)
					{
						uint32_t u2 = 0;
						try { u2 = static_cast<uint32_t>(std::stoul(it2.key())); }
						catch (...) { continue; }
						const float adv = it2.value().get<float>();
						out.kerning.emplace(FontInfo::PackKerningKey(u1, u2), adv);
					}
				}
			}
		}
	}

}
