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
	};

}
