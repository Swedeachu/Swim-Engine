#pragma once
#include "OpenGLRenderer.h"
#include <Windows.h>

namespace Engine
{

  // ShaderToyRendererGL extends OpenGLRenderer and each frame hard code renders a full-screen vertex shader triangle for the fragment shader to draw on.
  // TODO: imgui stuff to select and hot reload the desired fragment shader via windows file gui dialogue.
  class ShaderToyRendererGL : public OpenGLRenderer
  {

  public:

    int Awake() override;
    int Init() override;
    void Update(double dt) override;
    int Exit() override;

  private:

    GLuint shadertoyShaderProgram = 0;

    // OpenGL requires a VAO be binded no matter what so we have a blank dummy one
    GLuint dummyVAO = 0;

    // Uniform locations for the ShaderToy-style shader
    GLint loc_iTime = -1;
    GLint loc_iResolution = -1;

  };

}
