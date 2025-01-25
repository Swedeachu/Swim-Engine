#pragma once

#include <memory>
#include "Engine/Systems/Renderer/Buffer/VulkanBuffer.h"

namespace Engine
{

	struct MeshBufferData
	{

		std::unique_ptr<VulkanBuffer> vertexBuffer;
		std::unique_ptr<VulkanBuffer> indexBuffer;
		uint32_t indexCount = 0;

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
