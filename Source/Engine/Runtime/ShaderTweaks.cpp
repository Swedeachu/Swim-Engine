#include "Engine/Runtime/ShaderTweaks.h"

#include "Engine/Systems/Renderer/Runtime/ShaderLibrary.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

namespace Engine
{

	ShaderTweaks::ShaderTweaks(Swim::Commands::CommandRegistry& commandsValue) : commands(commandsValue)
	{
	}

	std::string ShaderTweaks::VariableName(std::string_view program, std::string_view field)
	{
		std::string name = "shader_";
		name.append(program);
		name.push_back('_');
		name.append(field);

		for (auto& c : name)
		{
			c = c == '.' ? '_' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		}

		return name;
	}

	void ShaderTweaks::Apply(std::string_view program, const RuntimeComputeProgram& compiled, std::span<std::byte> constants)
	{
		auto& entry = programs[std::string(program)];

		if (entry.Compiled != &compiled)
		{
			entry.Compiled = &compiled;
			entry.Fields.clear();

			for (const auto& tweak : compiled.Tweaks)
			{
				const auto name = VariableName(program, tweak.Name);
				auto* variable = commands.FindVariable(name);

				if (!variable)
				{
					const std::string description = "Overrides " + tweak.Name + " of " + std::string(program) + " (at the minimum: unchanged)";

					if (tweak.Type == RuntimeTweak::Kind::Float)
					{
						variable = &commands.AddFloat(name, tweak.Min, tweak.Min, tweak.Max, description, "Developer/Shaders");
					}
					else
					{
						const auto min = static_cast<std::int64_t>(std::ceil(tweak.Min));
						const auto max = static_cast<std::int64_t>(std::floor(tweak.Max));
						variable = &commands.AddInt(name, min, min, std::max(min, max), description, "Developer/Shaders");
					}
				}

				entry.Fields.push_back({ variable, tweak.Offset, static_cast<std::uint8_t>(tweak.Type) });
			}
		}

		for (const auto& field : entry.Fields)
		{
			if (field.Variable->IsDefault() || field.Offset + 4 > constants.size())
			{
				continue;
			}

			const auto type = static_cast<RuntimeTweak::Kind>(field.Type);
			std::byte bytes[4];

			if (type == RuntimeTweak::Kind::Float)
			{
				const float value = field.Variable->GetFloat();
				std::memcpy(bytes, &value, 4);
			}
			else if (type == RuntimeTweak::Kind::Int)
			{
				const std::int32_t value = field.Variable->GetInt();
				std::memcpy(bytes, &value, 4);
			}
			else
			{
				const auto value = static_cast<std::uint32_t>(std::max(0.0, field.Variable->GetDouble()));
				std::memcpy(bytes, &value, 4);
			}

			std::memcpy(constants.data() + field.Offset, bytes, 4);
		}
	}

} // namespace Engine
