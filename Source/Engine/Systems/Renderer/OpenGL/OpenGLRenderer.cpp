#include "PCH.h"
#include "OpenGLRenderer.h"
#include "Engine/SwimEngine.h"
#include "Engine/Systems/Renderer/Core/Textures/TexturePool.h"
#include "Engine/Systems/Renderer/Core/Meshes/MeshPool.h"
#include "Library/glm/gtc/matrix_transform.hpp"
#include "Engine/Components/Transform.h"
#include "Engine/Components/Material.h"
#include "Engine/Systems/Renderer/Core/Camera/CameraSystem.h"
#include "Engine/Systems/Renderer/Core/Camera/Frustum.h"

#include <fstream>
#include <sstream>
#include <iostream>

namespace Engine
{

	void OpenGLRenderer::Create(HWND hwnd, uint32_t width, uint32_t height)
	{
		windowWidth = width;
		windowHeight = height;
		windowHandle = hwnd;

		if (!windowHandle)
		{
			throw std::runtime_error("Invalid HWND passed to OpenGLRenderer.");
		}

		if (!InitOpenGLContext())
		{
			throw std::runtime_error("Failed to initialize OpenGL context.");
		}
	}

	// cool thanks this is a hack and stupid
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

		// Set VSync OFF (0 = disable vsync, 1 = enable vsync)
		typedef BOOL(APIENTRY* PFNWGLSWAPINTERVALEXTPROC)(int);
		PFNWGLSWAPINTERVALEXTPROC wglSwapIntervalEXT = nullptr;

		wglSwapIntervalEXT = (PFNWGLSWAPINTERVALEXTPROC)wglGetProcAddress("wglSwapIntervalEXT");

		if (wglSwapIntervalEXT)
		{
			wglSwapIntervalEXT(0); // 0 to disable VSync
		}

		glEnable(GL_DEPTH_TEST);   // 3D depth buffer 
		glEnable(GL_DEPTH_CLAMP);  // clamped culling
		glEnable(GL_CULL_FACE);    // back face culling
		glEnable(GL_MULTISAMPLE);  // MSAA
		glEnable(GL_STENCIL_TEST); // will be needed for outline stuff later on

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

	std::string OpenGLRenderer::LoadTextFile(const std::string& relativePath)
	{
		std::string fullPath = SwimEngine::GetExecutableDirectory() + "\\" + relativePath;

		std::ifstream file(fullPath);
		if (!file.is_open())
		{
			throw std::runtime_error("Failed to load shader: " + fullPath);
		}

		std::stringstream buffer;
		buffer << file.rdbuf();
		return buffer.str();
	}

	int OpenGLRenderer::Awake()
	{
		// --- 1) Compile & link ---
		std::string vertSource = LoadTextFile("Shaders/OpenGL/vertex.glsl");
		std::string fragSource = LoadTextFile("Shaders/OpenGL/fragment.glsl");
		GLuint vert = CompileGLSLShader(GL_VERTEX_SHADER, vertSource.c_str());
		GLuint frag = CompileGLSLShader(GL_FRAGMENT_SHADER, fragSource.c_str());
		shaderProgram = LinkShaderProgram({ vert, frag });

		// --- 2) Bind the Camera UBO block to binding point 0 ---
		//    This tells the shader's "layout(binding=0) uniform Camera" to read from our buffer.
		GLuint cameraBlockIndex = glGetUniformBlockIndex(shaderProgram, "Camera");
		if (cameraBlockIndex == GL_INVALID_INDEX)
		{
			std::cerr << "Warning: 'Camera' UBO not found in shader\n";
		}
		else
		{
			glUniformBlockBinding(shaderProgram, cameraBlockIndex, 0);
		}

		// --- 3) Cache all the *other* uniforms we still use each frame ---
		loc_mvp = glGetUniformLocation(shaderProgram, "mvp");
		loc_hasTexture = glGetUniformLocation(shaderProgram, "hasTexture");
		loc_albedoTex = glGetUniformLocation(shaderProgram, "albedoTex");

		// --- 4) Create and allocate the GPU buffer for our CameraUBO struct ---
		glGenBuffers(1, &ubo);
		glBindBuffer(GL_UNIFORM_BUFFER, ubo);
		// allocate enough space but don't fill yet
		glBufferData(GL_UNIFORM_BUFFER, sizeof(CameraUBO), nullptr, GL_DYNAMIC_DRAW);
		// bind to slot 0
		glBindBufferBase(GL_UNIFORM_BUFFER, 0, ubo);
		// unbind
		glBindBuffer(GL_UNIFORM_BUFFER, 0);

		// load textures, etc.
		TexturePool& pool = TexturePool::GetInstance();
		pool.LoadAllRecursively();
		missingTexture = pool.GetTexture2DLazy("mart");

		// Now set up the cubemap
		cubemapController = std::make_unique<CubeMapController>(
			"Shaders/OpenGL/skybox_vert.glsl",
			"Shaders/OpenGL/skybox_frag.glsl"
		);
		cubemapController->SetEnabled(false);

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
		// nop
	}

	void OpenGLRenderer::RenderFrame()
	{
		glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		UpdateUniformBuffer(); 
		glUseProgram(shaderProgram);

		std::shared_ptr<Scene>& scene = SwimEngine::GetInstance()->GetSceneSystem()->GetActiveScene();
		if (!scene)
		{
			SwapBuffers(deviceContext);
			return;
		}

		std::shared_ptr<CameraSystem> camera = scene->GetCameraSystem();
		const glm::mat4& view = camera->GetViewMatrix();
		const glm::mat4& proj = camera->GetProjectionMatrix();
		Frustum::SetCameraMatrices(view, proj);

		entt::registry& registry = scene->GetRegistry();

		RenderWorldSpace(scene, registry, view, proj);

		RenderScreenSpace(registry);

	#ifdef _DEBUG
		RenderWireframeDebug(scene);
	#endif

		if (cubemapController)
		{
			cubemapController->Render(view, proj);
		}

		SwapBuffers(deviceContext);
	}

	void OpenGLRenderer::RenderWorldSpace(std::shared_ptr<Scene>& scene, entt::registry& registry, const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix)
	{
		const Frustum& frustum = Frustum::Get();

		scene->GetSceneBVH()->QueryFrustumCallback(frustum, [&](entt::entity entity)
		{
			const Transform& transform = registry.get<Transform>(entity);
			if (transform.GetTransformSpace() == TransformSpace::World)
			{
				DrawEntity(entity, registry, viewMatrix, projectionMatrix);
			}
		});
	}

	void OpenGLRenderer::RenderScreenSpace(entt::registry& registry)
	{
		// We use this virtual canvas approach as values to offset things as to stay consistent-ish on most aspect ratios
		constexpr float virtualWidth = 1920.0f;
		constexpr float virtualHeight = 1080.0f;

		// Orthographic projection for virtual canvas
		glm::mat4 orthoProj = glm::ortho(
			0.0f, virtualWidth,
			0.0f, virtualHeight,
			-1.0f, 1.0f
		);

		// Compute scale from actual window size
		float scaleX = static_cast<float>(windowWidth) / virtualWidth;
		float scaleY = static_cast<float>(windowHeight) / virtualHeight;

		// Apply scale matrix so all UI is resolution-independent
		glm::mat4 resolutionScale = glm::scale(glm::mat4(1.0f), glm::vec3(scaleX, scaleY, 1.0f));
		glm::mat4 identityView = glm::mat4(1.0f);

		// Render all screen-space UI
		registry.view<Transform>().each([&](entt::entity entity, Transform& transform)
		{
			if (transform.GetTransformSpace() == TransformSpace::Screen)
			{
				const glm::mat4& model = transform.GetModelMatrix();
				glm::mat4 scaledModel = resolutionScale * model;
				glm::mat4 mvp = orthoProj * identityView * scaledModel;
				DrawEntityWithMVP(entity, registry, mvp);
			}
		});
	}

	void OpenGLRenderer::DrawEntity(entt::entity entity, entt::registry& registry, const glm::mat4& viewMatrix, const glm::mat4& projMatrix)
	{
		const Transform& transform = registry.get<Transform>(entity);
		const std::shared_ptr<MaterialData>& mat = registry.get<Material>(entity).data;
		const MeshBufferData& meshData = *mat->mesh->meshBufferData;

		// Compute model matrix and full MVP
		const glm::mat4& model = transform.GetModelMatrix();
		const glm::mat4 mvp = projMatrix * viewMatrix * model;

		// Upload matrix to shader
		glUniformMatrix4fv(loc_mvp, 1, GL_FALSE, &mvp[0][0]);

		// Bind texture if available
		bool usesTexture = (mat->albedoMap != nullptr);
		glUniform1f(loc_hasTexture, usesTexture ? 1.0f : 0.0f);

		GLuint texID = usesTexture ? mat->albedoMap->GetTextureID() : missingTexture->GetTextureID();
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, texID);
		glUniform1i(loc_albedoTex, 0);

		// Draw
		glBindVertexArray(meshData.GetGLVAO());
		glDrawElements(GL_TRIANGLES, meshData.indexCount, GL_UNSIGNED_SHORT, nullptr);
		glBindVertexArray(0);
	}

	void OpenGLRenderer::DrawEntityWithMVP(entt::entity entity, entt::registry& registry, const glm::mat4& mvp)
	{
		const std::shared_ptr<MaterialData>& mat = registry.get<Material>(entity).data;
		const MeshBufferData& meshData = *mat->mesh->meshBufferData;

		glUniformMatrix4fv(loc_mvp, 1, GL_FALSE, &mvp[0][0]);

		bool usesTexture = (mat->albedoMap != nullptr);
		glUniform1f(loc_hasTexture, usesTexture ? 1.0f : 0.0f);

		GLuint texID = usesTexture ? mat->albedoMap->GetTextureID() : missingTexture->GetTextureID();
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, texID);
		glUniform1i(loc_albedoTex, 0);

		glBindVertexArray(meshData.GetGLVAO());
		glDrawElements(GL_TRIANGLES, meshData.indexCount, GL_UNSIGNED_SHORT, nullptr);
		glBindVertexArray(0);
	}

	void OpenGLRenderer::RenderWireframeDebug(std::shared_ptr<Scene>& scene)
	{
		SceneDebugDraw* debugDraw = scene->GetSceneDebugDraw();
		constexpr bool cullWireframe = false;

		if (!debugDraw || !debugDraw->IsEnabled())
		{
			return;
		}

		auto& debugRegistry = debugDraw->GetRegistry();
		const Frustum& frustum = Frustum::Get();

		// Wireframe uses camera view and projection
		const glm::mat4& view = scene->GetCameraSystem()->GetViewMatrix();
		const glm::mat4& proj = scene->GetCameraSystem()->GetProjectionMatrix();

		auto viewEntities = debugRegistry.view<Transform, DebugWireBoxData>();

		for (auto entity : viewEntities)
		{
			const Transform& transform = viewEntities.get<Transform>(entity);
			const DebugWireBoxData& box = viewEntities.get<DebugWireBoxData>(entity);
			const std::shared_ptr<Mesh>& mesh = debugDraw->GetWireframeCubeMesh(box.color);

			if constexpr (cullWireframe)
			{
				const glm::vec3& min = mesh->meshBufferData->aabbMin;
				const glm::vec3& max = mesh->meshBufferData->aabbMax;

				if (!frustum.IsVisibleLazy(min, max, transform.GetModelMatrix()))
				{
					continue;
				}
			}

			// Compute and set MVP
			glm::mat4 model = transform.GetModelMatrix();
			glm::mat4 mvp = proj * view * model;
			glUniformMatrix4fv(loc_mvp, 1, GL_FALSE, &mvp[0][0]);

			// No texture for debug wireframes
			glUniform1f(loc_hasTexture, 0.0f);
			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, 0);
			glUniform1i(loc_albedoTex, 0);

			// Draw mesh
			const MeshBufferData& meshData = *mesh->meshBufferData;
			glBindVertexArray(meshData.GetGLVAO());
			glDrawElements(GL_TRIANGLES, meshData.indexCount, GL_UNSIGNED_SHORT, nullptr);
			glBindVertexArray(0);
		}
	}

	void OpenGLRenderer::UpdateUniformBuffer()
	{
		// 1) Gather the latest camera matrices into our CPU side struct
		CameraUBO camData{};
		camData.view = cameraSystem->GetViewMatrix();
		camData.proj = cameraSystem->GetProjectionMatrix();

		// Extract fov from projection matrix (OpenGL version, no Y flip)
		float fovY = 2.0f * atanf(1.0f / camData.proj[1][1]);
		float aspect = camData.proj[1][1] / camData.proj[0][0];
		float fovX = 2.0f * atanf(tanf(fovY * 0.5f) * aspect);

		auto& camera = cameraSystem->GetCamera();
		camData.camParams = glm::vec4(fovX, fovY, camera.GetNearClip(), camera.GetFarClip());

		// 2) Send them to the GPU UBO at binding 0
		glBindBuffer(GL_UNIFORM_BUFFER, ubo);
		glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(CameraUBO), &camData);
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

	// Unused
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
