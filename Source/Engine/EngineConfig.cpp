#include "Engine/EngineConfig.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdlib>

namespace Engine
{

	namespace
	{

		bool ParseGraphicsBackend(std::string_view value, GraphicsBackend& backend)
		{
			if (value == "auto")
			{
				backend = GraphicsBackend::Auto;
				return true;
			}
			if (value == "vulkan")
			{
				backend = GraphicsBackend::Vulkan;
				return true;
			}
			if (value == "opengl" || value == "opengl-legacy")
			{
				backend = GraphicsBackend::OpenGLLegacy;
				return true;
			}
			if (value == "d3d12")
			{
				backend = GraphicsBackend::D3D12;
				return true;
			}
			if (value == "metal")
			{
				backend = GraphicsBackend::Metal;
				return true;
			}
			return false;
		}

		bool ParsePhysicsBackend(std::string_view value, PhysicsBackend& backend)
		{
			if (value == "auto")
			{
				backend = PhysicsBackend::Auto;
				return true;
			}
			if (value == "physx")
			{
				backend = PhysicsBackend::PhysX;
				return true;
			}
			if (value == "jolt")
			{
				backend = PhysicsBackend::Jolt;
				return true;
			}
			return false;
		}

		bool ParseUnsigned(std::string_view value, uint64_t& result)
		{
			result = 0;
			const char* begin = value.data();
			const char* end = begin + value.size();
			auto [ptr, error] = std::from_chars(begin, end, result);
			return error == std::errc{} && ptr == end;
		}

		bool ParseEngineStateValue(std::string_view value, EngineState& state)
		{
			if (value.empty())
			{
				return false;
			}

			state = EngineState::None;
			std::size_t start = 0;
			while (start <= value.size())
			{
				const std::size_t delimiter = value.find_first_of(",|", start);
				std::string token = delimiter == std::string_view::npos
					? std::string(value.substr(start))
					: std::string(value.substr(start, delimiter - start));

				token.erase(token.begin(), std::find_if(token.begin(), token.end(),
					[](unsigned char ch) { return !std::isspace(ch); }));
				token.erase(std::find_if(token.rbegin(), token.rend(),
					[](unsigned char ch) { return !std::isspace(ch); }).base(), token.end());

				if (token.empty())
				{
					return false;
				}

				std::string normalized = token;
				std::transform(normalized.begin(), normalized.end(), normalized.begin(),
					[](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

				const EngineState tokenState = ParseEngineStateToken(token);
				if (tokenState == EngineState::None && normalized != "none")
				{
					char* end = nullptr;
					const unsigned long long numericValue = std::strtoull(normalized.c_str(), &end, 0);
					if (end == normalized.c_str() || *end != '\0' || numericValue != 0)
					{
						return false;
					}
				}

				state |= tokenState;

				if (delimiter == std::string_view::npos)
				{
					break;
				}
				start = delimiter + 1;
			}

			return true;
		}

		std::string_view ReadValue(int& index, int argc, char** argv, std::string_view argument, std::string_view prefix)
		{
			if (argument.rfind(prefix, 0) == 0)
			{
				return argument.substr(prefix.size());
			}

			if (index + 1 < argc)
			{
				return argv[++index];
			}

			return {};
		}

	}

	GraphicsBackend ResolveGraphicsBackend(GraphicsBackend backend)
	{
		if (backend == GraphicsBackend::Auto)
		{
			return GraphicsBackend::Vulkan;
		}
		return backend;
	}

	PhysicsBackend ResolvePhysicsBackend(PhysicsBackend backend)
	{
		if (backend == PhysicsBackend::Auto)
		{
			return PhysicsBackend::PhysX;
		}
		return backend;
	}

	std::string_view ToString(GraphicsBackend backend)
	{
		switch (backend)
		{
			case GraphicsBackend::Auto: return "Auto";
			case GraphicsBackend::Vulkan: return "Vulkan";
			case GraphicsBackend::OpenGLLegacy: return "OpenGL Legacy";
			case GraphicsBackend::D3D12: return "D3D12";
			case GraphicsBackend::Metal: return "Metal";
		}
		return "Unknown";
	}

	std::string_view ToString(PhysicsBackend backend)
	{
		switch (backend)
		{
			case PhysicsBackend::Auto: return "Auto";
			case PhysicsBackend::PhysX: return "PhysX";
			case PhysicsBackend::Jolt: return "Jolt";
		}
		return "Unknown";
	}

	EngineConfigParseResult ParseEngineConfigArgs(int argc, char** argv)
	{
		EngineConfigParseResult result{};
		result.Config.Window.Title = "Swim Engine";

		for (int i = 1; i < argc; ++i)
		{
			const std::string_view argument = argv[i];

			if (argument == "--graphics" || argument.rfind("--graphics=", 0) == 0)
			{
				const std::string_view value = ReadValue(i, argc, argv, argument, "--graphics=");
				if (value.empty() || !ParseGraphicsBackend(value, result.Config.Graphics))
				{
					result.Errors.emplace_back("Invalid --graphics value. Expected auto, vulkan, opengl, d3d12, or metal.");
				}
				continue;
			}

			if (argument == "--physics" || argument.rfind("--physics=", 0) == 0)
			{
				const std::string_view value = ReadValue(i, argc, argv, argument, "--physics=");
				if (value.empty() || !ParsePhysicsBackend(value, result.Config.Physics))
				{
					result.Errors.emplace_back("Invalid --physics value. Expected auto, physx, or jolt.");
				}
				continue;
			}

			if (argument == "--state" || argument.rfind("--state=", 0) == 0)
			{
				const std::string_view value = ReadValue(i, argc, argv, argument, "--state=");
				EngineState state = EngineState::None;
				if (!ParseEngineStateValue(value, state))
				{
					result.Errors.emplace_back("Invalid --state value.");
				}
				else
				{
					result.Config.InitialState = state;
				}
				continue;
			}

			if (argument == "--parent-hwnd" || argument.rfind("--parent-hwnd=", 0) == 0)
			{
				const std::string_view value = ReadValue(i, argc, argv, argument, "--parent-hwnd=");
				uint64_t nativeValue = 0;
				if (value.empty() || !ParseUnsigned(value, nativeValue) || nativeValue == 0)
				{
					result.Errors.emplace_back("Invalid --parent-hwnd value.");
				}
				else
				{
					result.Config.Window.ExternalParent = {
						Swim::Platform::NativeWindowType::Win32,
						reinterpret_cast<void*>(static_cast<std::uintptr_t>(nativeValue)),
						nullptr
					};
				}
				continue;
			}

			if (argument == "--opengl-shadertoy")
			{
				result.Config.UseOpenGLShaderToy = true;
				continue;
			}
		}

		return result;
	}

}
