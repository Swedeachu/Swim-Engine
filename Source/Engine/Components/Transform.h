#pragma once

#include "Library/glm/glm.hpp"
#include "Library/glm/gtc/quaternion.hpp"

// #define GLM_ENABLE_EXPERIMENTAL
// #include "Library/glm/gtx/quaternion.hpp"

namespace Engine
{

  class Transform
  {

  private:

    glm::vec3 position{ 0.0f, 0.0f, 0.0f };
    glm::vec3 scale{ 1.0f, 1.0f, 1.0f };
    glm::quat rotation{ 1.0f, 0.0f, 0.0f, 0.0f }; // Identity quaternion

    mutable bool dirty = true; // Marks if the transform has changed and if we need to recompute the model matrix next time GetModelMatrix() is called
    mutable glm::mat4 modelMatrix{ 1.0f };

    // Helper to mark as dirty (yes its a one liner but this future proofs the design more so)
    void MarkDirty() { dirty = true; }

  public:

    Transform() = default;

    Transform(const glm::vec3& pos, const glm::vec3& scl, const glm::quat& rot = glm::quat())
      : position(pos), scale(scl), rotation(rot), dirty(true), modelMatrix(1.0f)
    {}

    // Getters with public access
    const glm::vec3& GetPosition() const { return position; }
    const glm::vec3& GetScale() const { return scale; }
    const glm::quat& GetRotation() const { return rotation; }

    // Public setters that mark the transform as dirty

    void SetPosition(const glm::vec3& pos)
    {
      if (position != pos) 
      {
        position = pos;
        MarkDirty();
      }
    }

    void SetScale(const glm::vec3& scl)
    {
      if (scale != scl)
      {
        scale = scl;
        MarkDirty();
      }
    }

    void SetRotation(const glm::quat& rot)
    {
      if (rotation != rot)
      {
        rotation = rot;
        MarkDirty();
      }
    }

    void SetRotationEuler(float pitch, float yaw, float roll)
    {
      glm::quat newRotation = glm::quat(glm::vec3(glm::radians(pitch), glm::radians(yaw), glm::radians(roll)));
      SetRotation(newRotation);
    }

    // Returns pitch, yaw, roll in degrees
    glm::vec3 GetRotationEuler() const
    {
      glm::vec3 euler = glm::degrees(glm::eulerAngles(rotation));
      return euler; 
    }

    // Special reference getters to allow modification while marking dirty:

    glm::vec3& GetPositionRef()
    {
      MarkDirty();
      return position;
    }

    glm::vec3& GetScaleRef()
    {
      MarkDirty();
      return scale;
    }

    glm::quat& GetRotationRef()
    {
      MarkDirty();
      return rotation;
    }

    // Getter for model matrix using dirty flag pattern for performance
    const glm::mat4& GetModelMatrix() const
    {
      if (dirty)
      {
        modelMatrix = glm::translate(glm::mat4(1.0f), position)
          * glm::mat4_cast(rotation)
          * glm::scale(glm::mat4(1.0f), scale);
        dirty = false;
      }
      return modelMatrix;
    }

  };

}