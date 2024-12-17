#pragma once

#include <memory>
#include "Engine/Systems/Renderer/Buffer/VulkanBuffer.h"

namespace Engine
{

	struct MeshBufferData
	{

		std::unique_ptr<VulkanBuffer> vertexBuffer;
		std::unique_ptr<VulkanBuffer> indexBuffer;
		size_t indexCount;

		// Free all Vulkan buffers
		void Free()
		{
			if (vertexBuffer)
			{
				vertexBuffer->Free();
				vertexBuffer.reset();
			}
			if (indexBuffer)
			{
				indexBuffer->Free();
				indexBuffer.reset();
			}
		}

	};

}
