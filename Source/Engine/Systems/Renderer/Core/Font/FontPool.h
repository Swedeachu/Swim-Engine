#pragma once

#include "FontData.h"
#include <unordered_map>
#include <mutex>
#include <filesystem>
#include "Library/json/json.hpp"

namespace Engine
{

  class FontPool
  {

  public:

    static FontPool& GetInstance();

    // Delete copy/move to enforce singleton semantics.
    FontPool(const FontPool&) = delete;
    FontPool& operator=(const FontPool&) = delete;
    FontPool(FontPool&&) = delete;
    FontPool& operator=(FontPool&&) = delete;

    // Iterates Assets/Fonts recursively. Each subdirectory is expected to contain
    // exactly one *.json describing the MSDF atlas and a sibling *.png with the same stem.
    void LoadAllRecursively();

    std::shared_ptr<FontInfo> GetFontInfo(const std::string& name) const;

  private:

    FontPool() = default;

    mutable std::mutex poolMutex; // mutable so we can lock in const getters.

    // Per-directory loader that finds JSON + PNG and registers a FontData.
    void ParseFontDirectory(const std::filesystem::path& dirPath);

    // Helpers
    static bool LoadJsonFile(const std::filesystem::path& path, std::string& out);
    static FontYOrigin ParseYOrigin(const std::string& s);

    // Populates FontData fields from parsed json (nlohmann) and PNG path.
    void PopulateFromJson(FontInfo& out,
      const nlohmann::json& j,
      const std::filesystem::path& pngPath,
      const std::string& fontName);

    void PrintMapDebug() const;

    std::unordered_map<std::string, std::shared_ptr<FontInfo>> fontPool;

  };

}
