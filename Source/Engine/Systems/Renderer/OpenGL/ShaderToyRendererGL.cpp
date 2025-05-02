#include "PCH.h"
#include "ShaderToyRendererGL.h"

#include "Engine/Components/Transform.h"
#include "Engine/Components/Material.h"
#include "Engine/Systems/Renderer/Core/Material/MaterialPool.h"
#include "Engine/Systems/Renderer/Core/Meshes/MeshPool.h"
#include "Engine/Systems/Renderer/Core/Textures/TexturePool.h"
#include "Engine/SwimEngine.h"

namespace Engine
{

  ShaderToyRendererGL::ShaderToyRendererGL(HWND hwnd, uint32_t width, uint32_t height)
    : OpenGLRenderer(hwnd, width, height)
  {}

  int ShaderToyRendererGL::Awake()
  {
    // === Compile and link shaders ===
    std::string vertSource = LoadTextFile("Shaders/OpenGL/fullscreen_vert_shadertoy.glsl");

    // WARNING: this is hardcoded!!! We should probably have an imgui system to select the toy shader we want and reload it on the fly.
    // That would also let us mess with params and stuff in real time.
    std::string fragSource = LoadTextFile("Shaders/OpenGL/nuremberg.glsl"); 

    GLuint vert = CompileGLSLShader(GL_VERTEX_SHADER, vertSource.c_str());
    GLuint frag = CompileGLSLShader(GL_FRAGMENT_SHADER, fragSource.c_str());
    shadertoyShaderProgram = LinkShaderProgram({ vert, frag });

    // === Validate shader linking ===
    GLint success = 0;
    glGetProgramiv(shadertoyShaderProgram, GL_LINK_STATUS, &success);
    if (!success)
    {
      char log[1024];
      glGetProgramInfoLog(shadertoyShaderProgram, sizeof(log), nullptr, log);
      std::cerr << "[ShaderToyRendererGL] Shader link error:\n" << log << std::endl;
      return -1;
    }

    // === Cache uniform locations ===
    loc_iTime = glGetUniformLocation(shadertoyShaderProgram, "iTime");
    loc_iResolution = glGetUniformLocation(shadertoyShaderProgram, "iResolution");

    if (loc_iTime == -1 || loc_iResolution == -1)
    {
      std::cerr << "[ShaderToyRendererGL] Warning: Uniforms iTime or iResolution not found in shader!" << std::endl;
    }

    // === Create dummy VAO (required in Core Profile) ===
    glGenVertexArrays(1, &dummyVAO);
    glBindVertexArray(dummyVAO);

    // === Load engine textures so fallback can be used ===
    TexturePool::GetInstance().LoadAllRecursively();
    missingTexture = TexturePool::GetInstance().GetTexture2DLazy("mart");

    return 0;
  }

  int ShaderToyRendererGL::Init()
  {
    int err = OpenGLRenderer::Init(); 

    // since we are just a super quick and dirty shader toy renderer which exists in screen space, 
    // we don't need depth testing or any wild stuff a real 3D renderer needs, so we make sure to configure stuff here:
    glDisable(GL_DEPTH_TEST);   // 3D depth buffer 
    glDisable(GL_DEPTH_CLAMP);  // clamped culling
    glDisable(GL_CULL_FACE);    // back face culling
    glEnable(GL_MULTISAMPLE);   // MSAA (is actually appreciated!)
    glDisable(GL_STENCIL_TEST); // will be needed for outline stuff later on

    return err;
  }

  // for shader time param
  float GetTimeSeconds()
  {
    using clock = std::chrono::high_resolution_clock;
    static auto startTime = clock::now();
    auto now = clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::duration<float>>(now - startTime);

    return duration.count();
  }

  void ShaderToyRendererGL::Update(double dt)
  {
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // === Use ShaderToy shader ===
    glUseProgram(shadertoyShaderProgram);

    // === Ensure dummy VAO is bound ===
    glBindVertexArray(dummyVAO);

    // === Upload time and resolution uniforms ===
    glUniform1f(loc_iTime, GetTimeSeconds());
    glUniform2f(loc_iResolution, static_cast<float>(windowWidth), static_cast<float>(windowHeight));

    // === Clear old OpenGL errors ===
    while (glGetError() != GL_NO_ERROR);

    // === Perform fullscreen draw ===
    glDrawArrays(GL_TRIANGLES, 0, 3);

    // === Check for draw errors ===
    GLenum err = glGetError();
    if (err != GL_NO_ERROR)
    {
      std::cerr << "[ShaderToyRendererGL] glDrawArrays error: " << std::hex << err << std::endl;
    }

    // === Swap backbuffer ===
    SwapBuffers(deviceContext);
  }

  int ShaderToyRendererGL::Exit()
  {
    if (shadertoyShaderProgram != 0)
    {
      glDeleteProgram(shadertoyShaderProgram);
      shadertoyShaderProgram = 0;
    }

    if (dummyVAO)
    {
      glDeleteVertexArrays(1, &dummyVAO);
      dummyVAO = 0;
    }

    return OpenGLRenderer::Exit();
  }

}
