#pragma once

#include "Engine/Systems/Text/FontCollection.h"
#include "Engine/Systems/Text/FontFace.h"

#include <fstream>
#include <iterator>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace Swim::Testing
{
	inline std::shared_ptr<const Text::FontFace> LoadFontFile(const char* path)
	{
		std::ifstream stream(path, std::ios::binary);
		if (!stream)
		{
			throw std::runtime_error(std::string("Missing font fixture ") + path);
		}
		std::vector<char> bytes((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
		return std::make_shared<Text::FontFace>(std::as_bytes(std::span(bytes)));
	}

	// Latin, Arabic, U+0301, U+00E9, U+FFFD and U+1F600 (SwimTextFixture.ttf).
	inline std::shared_ptr<const Text::FontFace> LoadTextFontFixture()
	{
		return LoadFontFile(SWIM_TEXT_FONT_FIXTURE_PATH);
	}

	// Hebrew and Greek only, no space or Latin (SwimTextFallbackFixture.ttf).
	inline std::shared_ptr<const Text::FontFace> LoadTextFallbackFontFixture()
	{
		return LoadFontFile(SWIM_TEXT_FALLBACK_FONT_FIXTURE_PATH);
	}

	// Primary fixture first, then the Hebrew/Greek fallback.
	inline std::shared_ptr<const Text::FontCollection> LoadTextFontChain()
	{
		return std::make_shared<const Text::FontCollection>(
			std::vector<std::shared_ptr<const Text::FontFace>>{ LoadTextFontFixture(), LoadTextFallbackFontFixture() });
	}
} // namespace Swim::Testing
