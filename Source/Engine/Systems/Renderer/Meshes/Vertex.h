#pragma once

#include "Library/glm/glm.hpp"
#include <vulkan/vulkan.hpp>

namespace Engine
{

  struct Vertex
  {
    glm::vec3 position; // Corresponds to the "position" in vertex shader
    glm::vec3 color;    // Corresponds to the "color" in vertex shader

    static VkVertexInputBindingDescription GetBindingDescription()
    {
      VkVertexInputBindingDescription bindingDescription{};
      bindingDescription.binding = 0; // Binding index
      bindingDescription.stride = sizeof(Vertex);
      bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX; // Per-vertex data

      return bindingDescription;
    }

    static std::array<VkVertexInputAttributeDescription, 2> GetAttributeDescriptions()
    {
      std::array<VkVertexInputAttributeDescription, 2> attributeDescriptions{};

      // Position attribute
      attributeDescriptions[0].binding = 0;
      attributeDescriptions[0].location = 0; // Must match shader
      attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT; // glm::vec3
      attributeDescriptions[0].offset = offsetof(Vertex, position);

      // Color attribute
      attributeDescriptions[1].binding = 0;
      attributeDescriptions[1].location = 1; // Must match shader
      attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT; // glm::vec3
      attributeDescriptions[1].offset = offsetof(Vertex, color);

      return attributeDescriptions;
    }

  };

}
