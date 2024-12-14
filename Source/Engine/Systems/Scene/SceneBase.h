#pragma once

#include "Scene.h"

// this is currently unused and kind of just an old idea from 2023
namespace Engine
{

  // A CRTP base class to inherit constructors automatically
  template <typename Derived>
  class SceneBase : public Scene
  {
  public:
    using Scene::Scene; // Inherit constructors from Engine::Scene
  };

}
