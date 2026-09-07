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
	// Supports flat global descriptors and one global push-constant buffer with a uniform element layout.
	// Nested parameter blocks and entry-point resource parameters fail explicitly.
	ShaderRhiInterfaceResult BuildRhiShaderInterface(const ShaderReflection& reflection);

} // namespace Swim::ShaderCompiler
