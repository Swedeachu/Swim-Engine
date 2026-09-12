#pragma once

#include "Tools/ShaderCompiler/ShaderReflection.h"

#include <simdjson.h>

namespace Swim::ShaderCompiler::Detail
{

	// Resolve relative struct/parameter-group layouts to absolute descriptor leaves.
	// Failure appends one unsupported marker, never a partial parameter interface.
	void ParseSlangParameterLayout(simdjson::dom::object parameter,
		std::vector<ShaderBindingReflection>& outParameters);

}
