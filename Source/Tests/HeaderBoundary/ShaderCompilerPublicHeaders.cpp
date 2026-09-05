#include "Tools/ShaderCompiler/ShaderRhiInterface.h"
#include "Tools/ShaderCompiler/ShaderReflection.h"

namespace
{

	[[maybe_unused]] void CompileShaderCompilerPublicHeaders()
	{
		Swim::ShaderCompiler::ShaderReflection reflection;
		Swim::ShaderCompiler::ShaderBindingReflection binding;
		Swim::ShaderCompiler::ShaderEntryPointReflection entryPoint;
		(void)reflection;
		(void)binding;
		(void)entryPoint;
	}

}
