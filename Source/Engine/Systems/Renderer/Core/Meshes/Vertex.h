#pragma once

#include "Library/glm/glm.hpp"

namespace Engine
{

  struct Vertex
  {

    glm::vec3 position; // Location 0 in the shader
    glm::vec3 color;    // Location 1 in the shader
    glm::vec2 uv;       // Location 2 in the shader 

    static VkVertexInputBindingDescription GetBindingDescription()
    {
      VkVertexInputBindingDescription bindingDescription{};
      bindingDescription.binding = 0;
      bindingDescription.stride = sizeof(Vertex);
      bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

      return bindingDescription;
    }

    // Regular Vulkan drawing
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

    // For Vulkan indexed drawing
    static std::vector<VkVertexInputAttributeDescription> GetInstanceAttributeDescriptions()
    {
      std::vector<VkVertexInputAttributeDescription> instanceAttribs;

      // mat4 model -> 4 vec4s -> locations 3,4,5,6
      for (uint32_t i = 0; i < 4; ++i)
      {
        VkVertexInputAttributeDescription attrib{};
        attrib.binding = 1;
        attrib.location = 3 + i;
        attrib.format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attrib.offset = sizeof(glm::vec4) * i;
        instanceAttribs.push_back(attrib);
      }

      // textureIndex (uint) -> location 7
      VkVertexInputAttributeDescription texIndex{};
      texIndex.binding = 1;
      texIndex.location = 7;
      texIndex.format = VK_FORMAT_R32_UINT;
      texIndex.offset = sizeof(glm::vec4) * 4;
      instanceAttribs.push_back(texIndex);

      // hasTexture (float) -> location 8
      VkVertexInputAttributeDescription hasTex{};
      hasTex.binding = 1;
      hasTex.location = 8;
      hasTex.format = VK_FORMAT_R32_SFLOAT;
      hasTex.offset = sizeof(glm::vec4) * 4 + sizeof(uint32_t);
      instanceAttribs.push_back(hasTex);

      return instanceAttribs;
    }

    // OpenGL attribute setup (to be called after VAO/VBO bind)
    static void SetupOpenGLAttributes()
    {
      // Position attribute (location = 0)
      glEnableVertexAttribArray(0);
      glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, position));

      // Color attribute (location = 1)
      glEnableVertexAttribArray(1);
      glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, color));

      // UV attribute (location = 2)
      glEnableVertexAttribArray(2);
      glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, uv));
    }

  };

}
