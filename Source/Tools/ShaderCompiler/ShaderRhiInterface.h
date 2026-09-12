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
	// Accepts graphics stages or one fixed-local-size compute entry, including read/write storage buffers.
	// Sampled 2D color images retain Float/Uint/Sint numeric classes.
	// Typed 2D storage images require explicit format qualifiers and matching numeric types.
	// Supports resolved global/entry descriptors and one-dimensional fixed descriptor arrays,
	// plus one global push-constant buffer.
	// Globals use all program stages; entry descriptors use only their declaring stage.
	// The parser resolves nested structs/explicit parameter groups and retains uniform byte ranges.
	// All duplicate (space, binding) declarations reject. Arrays of resource-bearing groups,
	// implicit uniform scope containers, entry-local push constants, nested arrays and
	// runtime-sized descriptor arrays fail explicitly.
	ShaderRhiInterfaceResult BuildRhiShaderInterface(const ShaderReflection& reflection);

} // namespace Swim::ShaderCompiler
