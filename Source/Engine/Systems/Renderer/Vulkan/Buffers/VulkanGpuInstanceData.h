#pragma once

#include "Library/glm/glm.hpp"

namespace Engine
{

  struct alignas(16) GpuInstanceData
  {
    glm::mat4 model;

    glm::vec4 aabbMin;
    glm::vec4 aabbMax;

    uint32_t textureIndex;
    float hasTexture;
    uint32_t meshInfoIndex;
    uint32_t materialIndex;

    uint32_t indexCount;
    uint32_t space;
    VkDeviceSize vertexOffsetInMegaBuffer;
    VkDeviceSize indexOffsetInMegaBuffer;
  };

  // Static per-renderable data for the GPU cull path.
  // This now carries everything needed to rebuild a compacted GpuInstanceData packet on the GPU,
  // so the graphics path can consume the exact same instance layout as the stable CPU path.
  struct alignas(16) GpuWorldInstanceStaticData
  {
    glm::vec4 boundsCenterRadius;

    uint32_t textureIndex = 0;
    float hasTexture = 0.0f;
    uint32_t meshInfoIndex = 0;
    uint32_t materialIndex = 0;

    uint32_t indexCount = 0;
    uint32_t space = 0;
    uint32_t vertexOffsetInMegaBufferLo = 0;
    uint32_t vertexOffsetInMegaBufferHi = 0;

    uint32_t indexOffsetInMegaBufferLo = 0;
    uint32_t indexOffsetInMegaBufferHi = 0;
    uint32_t drawCommandIndex = 0;
    uint32_t outputBaseInstance = 0;
  };

  struct alignas(16) GpuWorldInstanceTransformData
  {
    glm::mat4 model{ 1.0f };
    uint32_t enabled = 1;
    uint32_t padA = 0;
    uint32_t padB = 0;
    uint32_t padC = 0;
  };

	struct alignas(16) MeshDecoratorGpuInstanceData
	{
		glm::vec4 fillColor;
		glm::vec4 strokeColor;
		glm::vec2 strokeWidth;
		glm::vec2 cornerRadius;
		int enableFill;
		int enableStroke;
		int roundCorners;
		int useTexture;
		glm::vec2 resolution;
		glm::vec2 quadSize;
    int renderOnTop;
	};

  struct alignas(16) MsdfTextGpuInstanceData
  {
    glm::mat4 modelTR;
    glm::vec4 plane;
    glm::vec4 uvRect;
    glm::vec4 fillColor;
    glm::vec4 strokeColor;
    float strokeWidthPx;
    float msdfPixelRange;
    float emScalePx;
    int space;
    glm::vec2 pxToModel;
    uint32_t  atlasTexIndex;
    uint32_t  _pad_;
  };

	struct alignas(16) GpuCullInputInstanceData
	{
		GpuInstanceData instance;
		uint32_t drawCommandIndex;
		uint32_t padA;
		uint32_t padB;
		uint32_t padC;
	};

	struct InstanceMeta
	{
		uint32_t instanceCount;
		uint32_t padA;
		uint32_t padB;
		uint32_t padC;
	};

  namespace GpuWorldInstanceFlags
  {
    static constexpr uint32_t HasTexture = 1u << 0;
  }

}
