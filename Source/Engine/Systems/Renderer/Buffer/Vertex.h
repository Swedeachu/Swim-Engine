#pragma once

#include "Library/glm/glm.hpp"
#include <vulkan/vulkan.hpp>

namespace Engine
{

  struct Vertex
  {
    glm::vec3 position; // Corresponds to the "position" in vertex shader
    glm::vec3 color;    // Corresponds to the "color" in vertex shader

    // Get binding description for this vertex layout
    static VkVertexInputBindingDescription GetBindingDescription()
    {
      VkVertexInputBindingDescription bindingDescription{};
      bindingDescription.binding = 0; // Index of the binding in the shader
      bindingDescription.stride = sizeof(Vertex); // Size of one vertex
      bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX; // Per-vertex data
      return bindingDescription;
    }

    // Get attribute descriptions for position and color
    static std::array<VkVertexInputAttributeDescription, 2> GetAttributeDescriptions()
    {
      std::array<VkVertexInputAttributeDescription, 2> attributeDescriptions{};

      // Position (location 0 in vertex shader)
      attributeDescriptions[0].binding = 0;
      attributeDescriptions[0].location = 0;
      attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT; // vec3
      attributeDescriptions[0].offset = offsetof(Vertex, position);

      // Color (location 1 in vertex shader)
      attributeDescriptions[1].binding = 0;
      attributeDescriptions[1].location = 1;
      attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT; // vec3
      attributeDescriptions[1].offset = offsetof(Vertex, color);

      return attributeDescriptions;
    }
  };

}
