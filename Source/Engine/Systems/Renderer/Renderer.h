#pragma once 

#include "Engine/Machine.h"
#include "Core/Environment/CubeMapController.h"
#include "Core/Meshes/Vertex.h"

namespace Engine
{

	struct MeshBufferData;

	// The point of this class is to force all other renderers to have the same feature set getters and public interface, such as the cubemap controller.
	// This lets the gameplay programmers ambigously manage the graphics systems as needed without worrying about the specific renderer they get from SwimEngine::GetRenderer().
	class Renderer : public Machine
	{

	public:

		virtual void Create(HWND hwnd, uint32_t width, uint32_t height) = 0;

		virtual std::unique_ptr<CubeMapController>& GetCubeMapController() = 0;

		virtual void UploadMeshToMegaBuffer(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices, MeshBufferData& meshData) = 0;

		// For consistent UI scaling across the whole engine:

		constexpr static float VirtualCanvasWidth = 1920.0f;
		constexpr static float VirtualCanvasHeight = 1080.0f;

	};

}
