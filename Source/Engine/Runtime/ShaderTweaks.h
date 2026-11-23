#pragma once

#include "Engine/Commands/CommandRegistry.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Engine
{

	struct RuntimeComputeProgram;
	struct RuntimeTweak;

	// Push-constant fields marked [Tweak(min, max)] (Source/Shaders/Slang/Tweak.slang) as
	// console variables shader_<program>_<field> (lower case, category "Developer/Shaders"),
	// registered the first time the program dispatches. A variable at its default (min)
	// leaves the value the program's owner wrote; any other value replaces it. Install
	// Apply with FrameRenderer::SetConstantsOverride.
	class ShaderTweaks
	{

	  public:

		explicit ShaderTweaks(Swim::Commands::CommandRegistry& commands);

		ShaderTweaks(const ShaderTweaks&) = delete;

		ShaderTweaks& operator=(const ShaderTweaks&) = delete;

		void Apply(std::string_view program, const RuntimeComputeProgram& compiled, std::span<std::byte> constants);

		static std::string VariableName(std::string_view program, std::string_view field);

	  private:

		struct Field
		{
			Swim::Commands::ConsoleVariable* Variable = nullptr;
			std::uint32_t Offset = 0;
			std::uint8_t Type = 0;
		};

		struct Program
		{
			const RuntimeComputeProgram* Compiled = nullptr; // Fields are rebuilt when the program is replaced.
			std::vector<Field> Fields;
		};

		Swim::Commands::CommandRegistry& commands;
		std::unordered_map<std::string, Program> programs;

	};

} // namespace Engine
