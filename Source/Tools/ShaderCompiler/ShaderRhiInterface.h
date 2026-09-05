#pragma once

#include "Tools/ShaderCompiler/ShaderReflection.h"
#include "Engine/Systems/Renderer/RHI/RhiContracts.h"

namespace Swim::ShaderCompiler
{

	struct ShaderRhiInterfaceResult
	{
		Rhi::ShaderProgramInterface Interface;
		std::string Error;

		explicit operator bool() const
		{
			return Error.empty();
		}
	};

	// Tool-side conversion. Runtime RHI consumes the owned result, never Slang/JSON types.
	// Flat global descriptors are supported; nested parameter blocks and push constants fail explicitly.
	ShaderRhiInterfaceResult BuildRhiShaderInterface(const ShaderReflection& reflection);

} // namespace Swim::ShaderCompiler
