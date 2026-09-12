#pragma once

#include <cstdint>

namespace Swim::ShaderCompiler::Detail
{

	struct ShaderParameterOffsets
	{
		std::uint32_t Descriptor = 0;
		std::uint32_t Space = 0;
		std::uint32_t RegisterSpace = 0;
		std::uint32_t Uniform = 0;
		std::uint32_t UniformSize = 0;
		bool HasDescriptor = false;
		bool HasRegisterSpace = false;
		bool HasUniform = false;
	};

}
