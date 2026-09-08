#pragma once

#include "Tools/ShaderCompiler/ShaderReflection.h"
#include "Engine/Systems/Renderer/RHI/RhiTypes.h"

namespace Swim::ShaderCompiler
{

	Rhi::Format GetRhiStorageTextureFormat(const ShaderBindingReflection& parameter);

} // namespace Swim::ShaderCompiler
