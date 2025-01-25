#pragma once

#include "Library/glm/glm.hpp"
#include <vulkan/vulkan.hpp>

namespace Engine
{

  struct Vertex
  {
    glm::vec3 position; // Location 0 in the shader
    glm::vec3 color;    // Location 1 in the shader
    glm::vec2 uv;       // Location 2 in the shader (new)

    static VkVertexInputBindingDescription GetBindingDescription()
    {
      VkVertexInputBindingDescription bindingDescription{};
      bindingDescription.binding = 0;
      bindingDescription.stride = sizeof(Vertex);
      bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
      return bindingDescription;
    }

    static std::array<VkVertexInputAttributeDescription, 3> GetAttributeDescriptions()
    {
      std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions{};

      // Position -> location=0
      attributeDescriptions[0].binding = 0;
      attributeDescriptions[0].location = 0;
      attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
      attributeDescriptions[0].offset = offsetof(Vertex, position);

      // Color -> location=1
      attributeDescriptions[1].binding = 0;
      attributeDescriptions[1].location = 1;
      attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
      attributeDescriptions[1].offset = offsetof(Vertex, color);

      // UV -> location=2
      attributeDescriptions[2].binding = 0;
      attributeDescriptions[2].location = 2;
      attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
      attributeDescriptions[2].offset = offsetof(Vertex, uv);

      return attributeDescriptions;
    }

  };

}
