#pragma once 

#include "Engine/Machine.h"
#include "Core/Environment/CubeMapController.h"

namespace Engine
{

	// The point of this class is to force all other renderers to have the same feature set getters and public interface, such as the cubemap controller.
	// This lets the gameplay programmers ambigously manage the graphics systems as needed without worrying about the specific renderer they get from SwimEngine::GetRenderer().
	class Renderer : public Machine
	{

	public:

		virtual void Create(HWND hwnd, uint32_t width, uint32_t height) = 0;

		virtual std::unique_ptr<CubeMapController>& GetCubeMapController() = 0;

	};

}
