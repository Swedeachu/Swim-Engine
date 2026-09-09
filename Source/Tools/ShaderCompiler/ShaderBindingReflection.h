#pragma once

#include "Tools/ShaderCompiler/ShaderReflection.h"

#include <simdjson.h>

namespace Swim::ShaderCompiler::Detail
{

	ShaderBindingReflection ParseSlangBindingParameter(simdjson::dom::object parameter);

} // namespace Swim::ShaderCompiler::Detail
