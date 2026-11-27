#pragma once

#include "Tools/ShaderCompiler/ShaderReflection.h"
#include "Engine/Systems/Renderer/RHI/RhiContracts.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Swim::ShaderCompiler
{

	struct ShaderNativeBindingsResult
	{
		std::vector<Rhi::NativeBinding> Bindings;
		std::string Error;

		explicit operator bool() const { return Error.empty(); }
	};

	// Where a register-binding target (the D3D register layout of the same program) places
	// each (space, binding) and the push-constant block of the binding-model reflection, by
	// matching parameter names. Native parameters without a counterpart (backend-reserved
	// registers) are ignored; a descriptor without a native register is an error.
	ShaderNativeBindingsResult BuildNativeBindings(const ShaderReflection& bindings, const ShaderReflection& registers);

	// Marks the structured buffers whose element count the shaders read, listed by the DXIL
	// build as "nativeSizeQueries" ({ class, index, space }) in the register reflection.
	// Returns an error, or an empty string.
	std::string ParseNativeSizeQueries(std::string_view registerReflectionJson, std::vector<Rhi::NativeBinding>& bindings);
	std::string LoadNativeSizeQueries(const std::filesystem::path& registerReflectionPath, std::vector<Rhi::NativeBinding>& bindings);

	// The "nativeSizeQueries" JSON array for a program's HLSL text (empty when there are none):
	// every global structured buffer whose element type a GetDimensions call reads, matched by
	// element type so a buffer passed to a function counts. cmake/SlangDxil.cmake runs the same
	// scan at build time.
	std::string FindNativeSizeQueries(std::string_view hlsl);

	struct ShaderNativeVertexInputsResult
	{
		std::vector<Rhi::NativeVertexInput> Inputs;
		std::string Error;

		explicit operator bool() const { return Error.empty(); }
	};

	// The vertex stage's input semantics by location, from the register-binding target's
	// reflection JSON (varying inputs of the vertex entry point, struct fields included).
	// System values are skipped; an input without a semantic is an error.
	ShaderNativeVertexInputsResult ParseNativeVertexInputs(std::string_view registerReflectionJson);
	ShaderNativeVertexInputsResult LoadNativeVertexInputs(const std::filesystem::path& registerReflectionPath);

} // namespace Swim::ShaderCompiler
