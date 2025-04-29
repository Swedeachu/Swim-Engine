#pragma once

#include <memory>
#include "Engine/Systems/Renderer/Core/Material/MaterialData.h"

namespace Engine
{

	// A component to give each entity for which shared material data to use at render time
  struct Material
  {

    std::shared_ptr<MaterialData> data;

    Material() = default;

    Material(std::shared_ptr<MaterialData> matPtr)
      : data(std::move(matPtr))
    {}

  };


}