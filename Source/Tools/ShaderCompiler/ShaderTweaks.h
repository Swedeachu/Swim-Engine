#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Swim::ShaderCompiler
{

	// A push-constant field marked [Tweak(min, max)] (Source/Shaders/Slang/Tweak.slang).
	struct ShaderTweakReflection
	{
		std::string Name;		// Nested struct members are "Outer.Inner".
		std::uint32_t Offset = 0; // Bytes from the start of the push-constant block.
		std::string ScalarType; // "float32", "int32" or "uint32".
		double Min = 0.0;
		double Max = 1.0;
	};

	// Every tweakable push-constant field of a Slang reflection JSON (global and
	// entry-point parameters). Empty when the JSON does not parse.
	std::vector<ShaderTweakReflection> FindSlangTweaks(std::string_view jsonText);

} // namespace Swim::ShaderCompiler
