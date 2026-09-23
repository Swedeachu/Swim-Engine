#pragma once
#include "Engine/Systems/Renderer/Materials/MaterialTemplate.h"
#include "Tools/ShaderCompiler/ShaderReflection.h"

#include <string>
#include <string_view>

namespace Swim::ShaderCompiler
{
	struct ShaderMaterialLayoutResult
	{
		Render::MaterialTemplateDesc Desc;
		std::string Error;

		explicit operator bool() const { return Error.empty(); }
	};

	// Tool-side conversion (critical-path item 58): the struct element of the
	// structured buffer `bindingName` becomes a material template layout. Leaf fields
	// map float32 scalars/vectors to Float..Float4, int32 to Int and uint32 to Uint;
	// a uint32 whose name ends in "Texture" or "Sampler" becomes a bindless
	// TextureIndex/SamplerIndex. Matrices, arrays, 16-bit/64-bit types and bool are
	// rejected, as is a missing binding. The runtime never sees Slang or JSON types.
	ShaderMaterialLayoutResult BuildMaterialTemplateDesc(
		const ShaderReflection& reflection, std::string_view bindingName, std::string templateName);
} // namespace Swim::ShaderCompiler
