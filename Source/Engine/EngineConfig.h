#pragma once

#include "Engine/EngineState.h"
#include "Engine/Platform/Window.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Engine
{

	enum class GraphicsBackend : uint8_t
	{
		Auto,
		Vulkan,
		OpenGLLegacy,
		D3D12,
		Metal
	};

	enum class PhysicsBackend : uint8_t
	{
		Auto,
		PhysX,
		Jolt
	};

	struct EngineConfig
	{
		EngineConfig()
		{
			Window.Title = "Swim Engine";
		}

		GraphicsBackend Graphics{ GraphicsBackend::Vulkan };
		PhysicsBackend Physics{ PhysicsBackend::PhysX };
		Swim::Platform::WindowDesc Window{};
		EngineState InitialState{ EngineState::Editing };
		bool UseOpenGLShaderToy{ false };
	};

	struct EngineConfigParseResult
	{
		EngineConfig Config{};
		std::vector<std::string> Errors;

		bool IsValid() const { return Errors.empty(); }
		explicit operator bool() const { return IsValid(); }
	};

	GraphicsBackend ResolveGraphicsBackend(GraphicsBackend backend);
	PhysicsBackend ResolvePhysicsBackend(PhysicsBackend backend);

	std::string_view ToString(GraphicsBackend backend);
	std::string_view ToString(PhysicsBackend backend);

	EngineConfigParseResult ParseEngineConfigArgs(int argc, char** argv);

}
