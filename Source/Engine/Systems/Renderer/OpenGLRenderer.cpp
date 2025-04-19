#include "PCH.h"
#include "OpenGLRenderer.h"
#include "Engine/SwimEngine.h"

#include "Textures/TexturePool.h"
#include "Meshes/MeshPool.h"
#include "Library/glm/gtc/matrix_transform.hpp"
#include "Engine/Components/Transform.h"
#include "Engine/Components/Material.h"

#include <fstream>
#include <iostream>

namespace Engine
{

  const char* vertexSrc = R"(
#version 460 core
layout (location = 0) in vec3 position;
layout (location = 1) in vec3 color;
layout (location = 2) in vec2 uv;

uniform mat4 view;
uniform mat4 proj;
uniform mat4 model;

out vec3 fragColor;
out vec2 fragUV;

void main() {
    fragColor = color;
    fragUV = uv;
    gl_Position = proj * view * model * vec4(position, 1.0);
}
)";

  const char* fragmentSrc = R"(
#version 460 core
in vec3 fragColor;
in vec2 fragUV;

out vec4 FragColor;

void main() {
    FragColor = vec4(1.0, 0.0, 0.0, 1.0); // DEBUG: force red
}
)";


  OpenGLRenderer::OpenGLRenderer(HWND hwnd, uint32_t width, uint32_t height)
    : windowHandle(hwnd), windowWidth(width), windowHeight(height)
  {
    if (!windowHandle)
    {
      throw std::runtime_error("Invalid HWND passed to OpenGLRenderer.");
    }

    if (!InitOpenGLContext())
    {
      throw std::runtime_error("Failed to initialize OpenGL context.");
    }
  }

  static void* GetGLProcAddress(const char* name)
  {
    void* p = (void*)wglGetProcAddress(name);
    if (p == 0 ||
      p == (void*)0x1 || p == (void*)0x2 || p == (void*)0x3 ||
      p == (void*)-1)
    {
      HMODULE module = LoadLibraryA("opengl32.dll");
      p = (void*)GetProcAddress(module, name);
    }
    return p;
  }

  bool OpenGLRenderer::InitOpenGLContext()
  {
    deviceContext = GetDC(windowHandle);
    if (!deviceContext)
    {
      std::cerr << "Failed to get device context from HWND." << std::endl;
      return false;
    }

    if (!SetPixelFormatForHDC(deviceContext))
    {
      std::cerr << "Failed to set pixel format." << std::endl;
      return false;
    }

    // Create dummy OpenGL context to load WGL extensions
    HGLRC dummyContext = wglCreateContext(deviceContext);
    if (!dummyContext)
    {
      std::cerr << "Failed to create dummy OpenGL context." << std::endl;
      return false;
    }

    if (!wglMakeCurrent(deviceContext, dummyContext))
    {
      std::cerr << "Failed to activate dummy OpenGL context." << std::endl;
      wglDeleteContext(dummyContext);
      return false;
    }

    // Load WGL functions using glad
    if (!gladLoadWGL(deviceContext, (GLADloadfunc)wglGetProcAddress))
    {
      std::cerr << "Failed to load WGL functions with gladLoadWGL." << std::endl;
      wglDeleteContext(dummyContext);
      return false;
    }

    // Attributes for the real OpenGL 4.6 core profile context
    int attribs[] = {
      WGL_CONTEXT_MAJOR_VERSION_ARB, 4,
      WGL_CONTEXT_MINOR_VERSION_ARB, 6,
      WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
    #ifdef _DEBUG
      WGL_CONTEXT_FLAGS_ARB, WGL_CONTEXT_DEBUG_BIT_ARB,
    #endif
      0
    };

    glContext = wglCreateContextAttribsARB(deviceContext, nullptr, attribs);
    if (!glContext)
    {
      std::cerr << "Failed to create OpenGL 4.6 context." << std::endl;
      wglDeleteContext(dummyContext);
      return false;
    }

    // Cleanup dummy context
    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(dummyContext);

    if (!wglMakeCurrent(deviceContext, glContext))
    {
      std::cerr << "Failed to activate real OpenGL context." << std::endl;
      return false;
    }

    // Load OpenGL functions using glad
    if (!gladLoadGL((GLADloadfunc)GetGLProcAddress))
    {
      std::cerr << "Failed to load OpenGL functions with gladLoadGL." << std::endl;
      return false;
    }

    // glEnable(GL_DEPTH_TEST);
    // glDisable(GL_CULL_FACE);

    std::cout << "OpenGL Initialized: " << glGetString(GL_VERSION) << std::endl;
    return true;
  }

  bool OpenGLRenderer::SetPixelFormatForHDC(HDC hdc)
  {
    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.cStencilBits = 8;
    pfd.iLayerType = PFD_MAIN_PLANE;

    int pixelFormat = ChoosePixelFormat(hdc, &pfd);
    if (pixelFormat == 0 || !SetPixelFormat(hdc, pixelFormat, &pfd))
    {
      std::cerr << "Failed to set pixel format." << std::endl;
      return false;
    }

    return true;
  }

  int OpenGLRenderer::Awake()
  {
    // GLuint vert = LoadSPIRVShaderStage("Shaders\\VertexShaders\\vertex.spv", GL_VERTEX_SHADER);
    // GLuint frag = LoadSPIRVShaderStage("Shaders\\FragmentShaders\\fragment.spv", GL_FRAGMENT_SHADER);

    GLuint vert = CompileGLSLShader(GL_VERTEX_SHADER, vertexSrc);
    GLuint frag = CompileGLSLShader(GL_FRAGMENT_SHADER, fragmentSrc);

    shaderProgram = LinkShaderProgram({ vert, frag });

    glGenBuffers(1, &ubo);
    glBindBuffer(GL_UNIFORM_BUFFER, ubo);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(CameraUBO), nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, ubo);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);

    TexturePool::GetInstance().LoadAllRecursively();
    missingTexture = TexturePool::GetInstance().GetTexture2DLazy("mart");

    return 0;
  }

  GLuint OpenGLRenderer::CompileGLSLShader(GLenum stage, const char* source)
  {
    GLuint shader = glCreateShader(stage);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint success = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success)
    {
      char infoLog[512];
      glGetShaderInfoLog(shader, 512, nullptr, infoLog);
      std::string errorStage = (stage == GL_VERTEX_SHADER) ? "VERTEX" : "FRAGMENT";
      throw std::runtime_error("GLSL " + errorStage + " shader compile failed: " + std::string(infoLog));
    }

    return shader;
  }

  int OpenGLRenderer::Init()
  {
    cameraSystem = SwimEngine::GetInstance()->GetCameraSystem();
    return 0;
  }

  void OpenGLRenderer::SetSurfaceSize(uint32_t newWidth, uint32_t newHeight)
  {
    windowWidth = newWidth;
    windowHeight = newHeight;
    glViewport(0, 0, newWidth, newHeight);
  }

  void OpenGLRenderer::SetFramebufferResized()
  {
    framebufferResized = true;
  }

  void OpenGLRenderer::Update(double dt)
  {
    if (framebufferResized && cameraSystem)
    {
      framebufferResized = false;
      cameraSystem->RefreshAspect();
      glViewport(0, 0, windowWidth, windowHeight);
      return;
    }

    RenderFrame();
  }

  void OpenGLRenderer::FixedUpdate(unsigned int tickThisSecond)
  {
    // Optional fixed timestep logic
  }

  void OpenGLRenderer::RenderFrame()
  {
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glUseProgram(shaderProgram);
    UpdateUniformBuffer();

    auto& scene = SwimEngine::GetInstance()->GetSceneSystem()->GetActiveScene();
    if (!scene) return;

    auto& registry = scene->GetRegistry();
    auto view = registry.view<Transform, Material>();

    for (auto entity : view)
    {
      auto& transform = view.get<Transform>(entity);
      auto& mat = view.get<Material>(entity).data;
      auto& meshData = *mat->mesh->meshBufferData;

      glm::mat4 model = transform.GetModelMatrix();
      glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "model"), 1, GL_FALSE, &model[0][0]);

      GLuint texID = (mat->albedoMap ? mat->albedoMap : missingTexture)->GetTextureID();
      glBindTexture(GL_TEXTURE_2D, texID);

      meshData.glBuffer->Bind();

      glDrawElements(GL_TRIANGLES, meshData.indexCount, GL_UNSIGNED_SHORT, nullptr); 
    }

    SwapBuffers(deviceContext);
  }

  void OpenGLRenderer::UpdateUniformBuffer()
  {
    CameraUBO uboData{};
    uboData.view = cameraSystem->GetViewMatrix();
    uboData.proj = cameraSystem->GetProjectionMatrix();

    glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "view"), 1, GL_FALSE, &uboData.view[0][0]);
    glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "proj"), 1, GL_FALSE, &uboData.proj[0][0]);

    glBindBuffer(GL_UNIFORM_BUFFER, ubo);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(CameraUBO), &uboData);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
  }

  int OpenGLRenderer::Exit()
  {
    glDeleteProgram(shaderProgram);
    glDeleteBuffers(1, &ubo);
    MeshPool::GetInstance().Flush();
    TexturePool::GetInstance().Flush();
    return 0;
  }

  GLuint OpenGLRenderer::LoadSPIRVShaderStage(const std::string& path, GLenum shaderStage)
  {
    std::string fullPath = SwimEngine::GetExecutableDirectory() + "\\" + path;
    std::ifstream file(fullPath, std::ios::ate | std::ios::binary);
    if (!file.is_open())
    {
      throw std::runtime_error("Failed to load SPIR-V shader: " + fullPath);
    }

    size_t fileSize = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();

    std::cout << "Loaded SPIR-V shader stage: " << fullPath << std::endl;

    GLuint shader = glCreateShader(shaderStage);
    glShaderBinary(1, &shader, GL_SHADER_BINARY_FORMAT_SPIR_V, buffer.data(), static_cast<GLsizei>(buffer.size()));
    glSpecializeShader(shader, "main", 0, nullptr, nullptr);

    GLint success = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success)
    {
      char infoLog[512];
      glGetShaderInfoLog(shader, 512, nullptr, infoLog);
      throw std::runtime_error("SPIR-V specialization failed: " + std::string(infoLog));
    }

    return shader;
  }

  GLuint OpenGLRenderer::LinkShaderProgram(const std::vector<GLuint>& shaderStages)
  {
    GLuint program = glCreateProgram();
    for (GLuint shader : shaderStages)
    {
      glAttachShader(program, shader);
    }

    glLinkProgram(program);

    GLint success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success)
    {
      char infoLog[512];
      glGetProgramInfoLog(program, 512, nullptr, infoLog);
      throw std::runtime_error("Shader program linking failed: " + std::string(infoLog));
    }

    for (GLuint shader : shaderStages)
    {
      glDetachShader(program, shader);
      glDeleteShader(shader);
    }

    return program;
  }

} // namespace Engine
