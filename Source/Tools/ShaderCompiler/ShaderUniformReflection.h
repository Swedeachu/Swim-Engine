#pragma once

#include <cstdint>
#include <string>

namespace Swim::ShaderCompiler
{

	struct ShaderUniformReflection
	{
		std::string Name;
		std::uint32_t Offset = 0;
		std::uint32_t Size = 0;
		// Leaf type of structured-element fields: Slang scalar type ("float32",
		// "uint32", "int32", ...) and component count (1 for scalars, N for vectors).
		// Empty/0 for matrices, arrays and uniform-buffer ranges.
		std::string ScalarType = {};
		std::uint32_t ComponentCount = 0;
	};

} // namespace Swim::ShaderCompiler
