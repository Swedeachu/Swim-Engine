#pragma once

#include "Tools/ShaderCompiler/ShaderReflection.h"
#include "Engine/Systems/Renderer/RHI/RhiContracts.h"

namespace Swim::ShaderCompiler
{

	// Appends one flat descriptor. Every occupied (space, binding) rejects, even
	// when types agree: independent declarations are not implicit resource aliases.
	std::string AppendRhiDescriptorBinding(const ShaderBindingReflection& parameter,
		Rhi::ShaderStageMask stages, Rhi::ShaderProgramInterface& interface);

} // namespace Swim::ShaderCompiler
