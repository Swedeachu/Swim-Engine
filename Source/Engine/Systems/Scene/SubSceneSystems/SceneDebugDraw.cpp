#include "PCH.h"
#include "SceneDebugDraw.h"
#include "Engine/Systems/Renderer/Core/Meshes/MeshPool.h"
#include "Engine/Components/Transform.h"

namespace Engine
{

  // This is 100% immediate mode, all submitted wireframe boxes are cleared at the end of each frame via Clear()
  // We should however do a reverse double buffer type approach where we save all the transforms for the next frame 
  // before clearing everything to avoid recreating the same entity.
  // For example if SubmitWireframeBox is called two frames in a row with the same args, it can just use that previous frames created entity.

  // This is cursed and technically very inefficent, its still normal vertices and indices composing triangles to make a bevelled 3D cuboid mesh.
  // While a fine hack for now that doesn't matter much for debug mode stuff, it won't scale at all the second we want true wireframe drawing of all our other unique meshes.
  void SceneDebugDraw::Init()
  {
    std::vector<Vertex> vertices;
    std::vector<uint16_t> indices;

    // 8 corners of unit cube
    glm::vec3 corners[8] = {
        {-0.5f, -0.5f, -0.5f}, {0.5f, -0.5f, -0.5f},
        {0.5f, 0.5f, -0.5f}, {-0.5f, 0.5f, -0.5f},
        {-0.5f, -0.5f, 0.5f}, {0.5f, -0.5f, 0.5f},
        {0.5f, 0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f}
    };

    // 12 edges of a cube (pairs of corner indices)
    int edges[12][2] = {
        {0,1}, {1,2}, {2,3}, {3,0}, // bottom
        {4,5}, {5,6}, {6,7}, {7,4}, // top
        {0,4}, {1,5}, {2,6}, {3,7}  // sides
    };

    float thickness = 0.02f;
    uint16_t indexOffset = 0;

    for (int i = 0; i < 12; i++)
    {
      glm::vec3 start = corners[edges[i][0]];
      glm::vec3 end = corners[edges[i][1]];
      glm::vec3 center = (start + end) * 0.5f;
      glm::vec3 dir = end - start;
      float length = glm::length(dir);
      glm::vec3 axis = glm::normalize(dir);

      glm::vec3 scale = glm::vec3(thickness);
      if (fabs(axis.x) > 0.9f) { scale.x = length; }
      else if (fabs(axis.y) > 0.9f) { scale.y = length; }
      else if (fabs(axis.z) > 0.9f) { scale.z = length; }

      glm::vec3 min = center - scale * 0.5f;
      glm::vec3 max = center + scale * 0.5f;

      glm::vec3 boxCorners[8] = {
          {min.x, min.y, min.z}, {max.x, min.y, min.z},
          {max.x, max.y, min.z}, {min.x, max.y, min.z},
          {min.x, min.y, max.z}, {max.x, min.y, max.z},
          {max.x, max.y, max.z}, {min.x, max.y, max.z}
      };

      uint16_t boxIndices[36] = {
          0,1,2, 2,3,0, // bottom
          4,5,6, 6,7,4, // top
          0,1,5, 5,4,0, // front
          2,3,7, 7,6,2, // back
          1,2,6, 6,5,1, // right
          3,0,4, 4,7,3  // left
      };

      for (int j = 0; j < 8; j++)
      {
        Vertex v{};
        v.position = boxCorners[j];
        v.color = glm::vec3(1.0f, 0.0f, 0.0f); // hard-coded red
        v.uv = glm::vec2(0.0f); // not used
        vertices.push_back(v);
      }

      for (int j = 0; j < 36; j++)
      {
        indices.push_back(indexOffset + boxIndices[j]);
      }

      indexOffset += 8;
    }

    wireframeCubeMesh = MeshPool::GetInstance().RegisterMesh("DebugWireframeCubeBevelledRed", vertices, indices);
  }

  void SceneDebugDraw::Clear()
  {
    debugRegistry.clear();
  }

  void SceneDebugDraw::SubmitWireframeBoxAABB
  (
    const glm::vec3& min, 
    const glm::vec3& max,
    const glm::vec3& color
  )
  {
    // Compute center and size for Transform
    glm::vec3 center = (min + max) * 0.5f;
    glm::vec3 size = (max - min);

    entt::entity entity = debugRegistry.create();

    // Add Transform component
    debugRegistry.emplace<Transform>(entity, center, size);

    // Add DebugWireBoxData component
    debugRegistry.emplace<DebugWireBoxData>(entity, DebugWireBoxData{ color });
  }

  void SceneDebugDraw::SubmitWireframeBox
  (
    const glm::vec3& position, 
    const glm::vec3& scale,
    float pitchDegrees, 
    float yawDegrees, 
    float rollDegrees,
    const glm::vec3& color
  )
  {
    // Build quaternion from Euler angles
    glm::vec3 eulerRadians = glm::radians(glm::vec3(pitchDegrees, yawDegrees, rollDegrees));
    glm::quat rotationQuat = glm::quat(eulerRadians);

    entt::entity entity = debugRegistry.create();

    // Add Transform component
    debugRegistry.emplace<Transform>(entity, position, scale, rotationQuat);

    // Add DebugWireBoxData component
    debugRegistry.emplace<DebugWireBoxData>(entity, DebugWireBoxData{ color });
  }

  entt::registry& SceneDebugDraw::GetRegistry()
  {
    return debugRegistry;
  }

  const std::shared_ptr<Mesh>& SceneDebugDraw::GetWireframeCubeMesh() const
  {
    return wireframeCubeMesh;
  }

}
