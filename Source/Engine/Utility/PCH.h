#pragma once

// OS | TODO: Linux support since we are using Vulkan (this makes GLFW very, very tempting)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
// undefine min and max from windows.h because that collides with std::min std::max
// if we for some reason decide we want windows.h min and max back that means using std::min<T> and std::max<T> with type explicit passing to the templated function
#undef min
#undef max

// STL
#include <vector>
#include <string>
#include <map>
#include <unordered_map>
#include <iostream>
#include <memory> // For smart pointers

// Swim Engine
#include "Engine\Machine.h"
#include "Engine\Systems\Scene\Scene.h"
#include "Engine\Systems\Scene\SceneSystem.h"
#include "Engine\Systems\IO\InputManager.h"
#include "Engine\Systems\Renderer\CameraSystem.h"

// glm math
#include "Library/glm/glm.hpp"

// entt
#include "Library/EnTT/entt.hpp"

// vulkan
#define K_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_win32.h>

