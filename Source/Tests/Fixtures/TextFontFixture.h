#pragma once

#include "Engine/Systems/Text/FontFace.h"

#include <fstream>
#include <iterator>
#include <stdexcept>

namespace Swim::Testing
{
	inline std::shared_ptr<const Text::FontFace> LoadTextFontFixture()
	{
		std::ifstream stream(SWIM_TEXT_FONT_FIXTURE_PATH, std::ios::binary);
		if (!stream)
		{
			throw std::runtime_error("Missing SwimTextFixture.ttf");
		}
		std::vector<char> bytes((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
		return std::make_shared<Text::FontFace>(std::as_bytes(std::span(bytes)));
	}
} // namespace Swim::Testing
