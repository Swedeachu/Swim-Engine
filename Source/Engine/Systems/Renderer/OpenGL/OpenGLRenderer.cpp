#include "PCH.h"
#include "OpenGLRenderer.h"
#include "Engine/SwimEngine.h"
#include "Engine/Systems/Renderer/Core/Textures/TexturePool.h"
#include "Engine/Systems/Renderer/Core/Meshes/MeshPool.h"
#include "Library/glm/gtc/matrix_transform.hpp"
#include "Engine/Components/Transform.h"
#include "Engine/Components/Material.h"
#include "Engine/Components/DecoratorUI.h"
#include "Engine/Systems/Renderer/Core/Camera/CameraSystem.h"
#include "Engine/Systems/Renderer/Core/Camera/Frustum.h"

#include <fstream>
#include <sstream>
#include <iostream>

namespace Engine
{

	PFNWGLCHOOSEPIXELFORMATARBPROC g_wglChoosePixelFormatARB = nullptr;

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

	HWND CreateDummyWindow(HINSTANCE hInstance)
	{
		WNDCLASSA wc = {};
		wc.style = CS_OWNDC;
		wc.lpfnWndProc = DefWindowProcA;
		wc.hInstance = hInstance;
		wc.lpszClassName = "DummyWGLWindow";

		RegisterClassA(&wc);

		return CreateWindowA(
			"DummyWGLWindow", "Dummy", WS_OVERLAPPEDWINDOW,
			0, 0, 1, 1, nullptr, nullptr, hInstance, nullptr
		);
	}

	bool OpenGLRenderer::InitOpenGLContext()
	{
		// --- Step 1: Create dummy window/context to load WGL extensions ---
		HINSTANCE hInstance = GetModuleHandle(nullptr);
		HWND dummyHwnd = CreateDummyWindow(hInstance);
		HDC dummyDC = GetDC(dummyHwnd);

		PIXELFORMATDESCRIPTOR dummyPFD = {};
		dummyPFD.nSize = sizeof(dummyPFD);
		dummyPFD.nVersion = 1;
		dummyPFD.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
		dummyPFD.iPixelType = PFD_TYPE_RGBA;
		dummyPFD.cColorBits = 32;
		dummyPFD.cDepthBits = 24;
		dummyPFD.cStencilBits = 8;
		dummyPFD.iLayerType = PFD_MAIN_PLANE;

		int dummyFormat = ChoosePixelFormat(dummyDC, &dummyPFD);
		SetPixelFormat(dummyDC, dummyFormat, &dummyPFD);

		HGLRC dummyContext = wglCreateContext(dummyDC);
		wglMakeCurrent(dummyDC, dummyContext);

		// --- Step 2: Load WGL extensions ---
		if (!gladLoadWGL(dummyDC, (GLADloadfunc)wglGetProcAddress))
		{
			std::cerr << "Failed to load WGL extensions.\n";
			return false;
		}

		// Load wglChoosePixelFormatARB now that dummy context is current ---
		g_wglChoosePixelFormatARB = (PFNWGLCHOOSEPIXELFORMATARBPROC)wglGetProcAddress("wglChoosePixelFormatARB");

		if (!g_wglChoosePixelFormatARB)
		{
			std::cerr << "[FATAL] wglChoosePixelFormatARB is NULL despite extension being reported.\n";
			return false;
		}

		// --- Step 3: Clean up dummy context ---
		wglMakeCurrent(nullptr, nullptr);
		wglDeleteContext(dummyContext);
		ReleaseDC(dummyHwnd, dummyDC);
		DestroyWindow(dummyHwnd);

		// --- Step 4: Get actual device context ---
		deviceContext = GetDC(windowHandle);
		if (!deviceContext)
		{
			std::cerr << "Failed to get device context from window.\n";
			return false;
		}

		if (!SetPixelFormatForHDC(deviceContext))
		{
			std::cerr << "Failed to set pixel format (MSAA).\n";
			return false;
		}

		// --- Step 5: Create real context ---
		int contextAttribs[] = {
			WGL_CONTEXT_MAJOR_VERSION_ARB, 4,
			WGL_CONTEXT_MINOR_VERSION_ARB, 6,
			WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
		#ifdef _DEBUG
			WGL_CONTEXT_FLAGS_ARB, WGL_CONTEXT_DEBUG_BIT_ARB,
		#endif
			0
		};

		glContext = wglCreateContextAttribsARB(deviceContext, nullptr, contextAttribs);
		if (!glContext)
		{
			std::cerr << "Failed to create OpenGL 4.6 context.\n";
			return false;
		}

		if (!wglMakeCurrent(deviceContext, glContext))
		{
			std::cerr << "Failed to make OpenGL context current.\n";
			return false;
		}

		if (!gladLoadGL((GLADloadfunc)GetGLProcAddress))
		{
			std::cerr << "Failed to load OpenGL functions via glad.\n";
			return false;
		}

		// --- Step 6: Runtime GL settings ---
		auto wglSwapIntervalEXT = (BOOL(APIENTRY*)(int))wglGetProcAddress("wglSwapIntervalEXT");
		if (wglSwapIntervalEXT) { wglSwapIntervalEXT(0); }

		glEnable(GL_DEPTH_TEST);
		glEnable(GL_DEPTH_CLAMP);
		glEnable(GL_CULL_FACE);
		glEnable(GL_MULTISAMPLE);
		glEnable(GL_STENCIL_TEST);

		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		std::cout << "[INFO] OpenGL Initialized: " << glGetString(GL_VERSION) << "\n";

		GLint samples = 0;
		glGetIntegerv(GL_SAMPLES, &samples);
		std::cout << "[INFO] MSAA samples: " << samples << "\n";

		return true;
	}

	bool OpenGLRenderer::SetPixelFormatForHDC(HDC hdc)
	{
		if (!g_wglChoosePixelFormatARB)
		{
			std::cerr << "g_wglChoosePixelFormatARB is not set.\n";
			return false;
		}

		const int pixelAttribs[] = {
			WGL_DRAW_TO_WINDOW_ARB, GL_TRUE,
			WGL_SUPPORT_OPENGL_ARB, GL_TRUE,
			WGL_DOUBLE_BUFFER_ARB, GL_TRUE,
			WGL_PIXEL_TYPE_ARB, WGL_TYPE_RGBA_ARB,
			WGL_COLOR_BITS_ARB, 32,
			WGL_DEPTH_BITS_ARB, 24,
			WGL_STENCIL_BITS_ARB, 8,
			WGL_SAMPLE_BUFFERS_ARB, 1,   // Enable MSAA
			WGL_SAMPLES_ARB, 4,          // Request 4x MSAA
			0
		};

		int pixelFormat = 0;
		UINT numFormats = 0;

		if (!g_wglChoosePixelFormatARB(hdc, pixelAttribs, nullptr, 1, &pixelFormat, &numFormats) || numFormats == 0)
		{
			std::cerr << "wglChoosePixelFormatARB failed.\n";
			return false;
		}

		PIXELFORMATDESCRIPTOR pfd = {};
		if (!DescribePixelFormat(hdc, pixelFormat, sizeof(pfd), &pfd))
		{
			std::cerr << "DescribePixelFormat failed.\n";
			return false;
		}

		if (!SetPixelFormat(hdc, pixelFormat, &pfd))
		{
			std::cerr << "SetPixelFormat failed for multisample format.\n";
			return false;
		}

		std::cout << "[INFO] MSAA-capable pixel format set successfully.\n";
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
		// --- 1) Compile and link main shader ---
		std::string vertSource = LoadTextFile("Shaders/OpenGL/vertex.glsl");
		std::string fragSource = LoadTextFile("Shaders/OpenGL/fragment.glsl");
		GLuint vert = CompileGLSLShader(GL_VERTEX_SHADER, vertSource.c_str());
		GLuint frag = CompileGLSLShader(GL_FRAGMENT_SHADER, fragSource.c_str());
		shaderProgram = LinkShaderProgram({ vert, frag });

		// --- 2) Camera block and uniforms ---
		GLuint cameraBlockIndex = glGetUniformBlockIndex(shaderProgram, "Camera");
		if (cameraBlockIndex != GL_INVALID_INDEX)
		{
			glUniformBlockBinding(shaderProgram, cameraBlockIndex, 0);
		}
		loc_mvp = glGetUniformLocation(shaderProgram, "mvp");
		loc_hasTexture = glGetUniformLocation(shaderProgram, "hasTexture");
		loc_albedoTex = glGetUniformLocation(shaderProgram, "albedoTex");

		// --- 3) Camera UBO setup ---
		glGenBuffers(1, &ubo);
		glBindBuffer(GL_UNIFORM_BUFFER, ubo);
		glBufferData(GL_UNIFORM_BUFFER, sizeof(CameraUBO), nullptr, GL_DYNAMIC_DRAW);
		glBindBufferBase(GL_UNIFORM_BUFFER, 0, ubo);
		glBindBuffer(GL_UNIFORM_BUFFER, 0);

		// --- 4) Load and compile Decorator UI Shader ---
		std::string uiVertSrc = LoadTextFile("Shaders/OpenGL/ui_vertex.glsl");
		std::string uiFragSrc = LoadTextFile("Shaders/OpenGL/ui_fragment.glsl");

		GLuint uiVert = CompileGLSLShader(GL_VERTEX_SHADER, uiVertSrc.c_str());
		GLuint uiFrag = CompileGLSLShader(GL_FRAGMENT_SHADER, uiFragSrc.c_str());

		uiDecoratorShader = LinkShaderProgram({ uiVert, uiFrag });

		// Cache uniform locations for UI shader
		loc_ui_mvp = glGetUniformLocation(uiDecoratorShader, "mvp");
		loc_ui_fillColor = glGetUniformLocation(uiDecoratorShader, "fillColor");
		loc_ui_strokeColor = glGetUniformLocation(uiDecoratorShader, "strokeColor");
		loc_ui_strokeWidth = glGetUniformLocation(uiDecoratorShader, "strokeWidth");
		loc_ui_cornerRadius = glGetUniformLocation(uiDecoratorShader, "cornerRadius");
		loc_ui_enableStroke = glGetUniformLocation(uiDecoratorShader, "enableStroke");
		loc_ui_enableFill = glGetUniformLocation(uiDecoratorShader, "enableFill");
		loc_ui_roundCorners = glGetUniformLocation(uiDecoratorShader, "roundCorners");
		loc_ui_resolution = glGetUniformLocation(uiDecoratorShader, "resolution");
		loc_ui_quadSize = glGetUniformLocation(uiDecoratorShader, "quadSize");
		loc_ui_useTexture = glGetUniformLocation(uiDecoratorShader, "useTexture");
		loc_ui_albedoTex = glGetUniformLocation(uiDecoratorShader, "albedoTex");
		loc_ui_isWorldSpace = glGetUniformLocation(uiDecoratorShader, "isWorldSpace");

		// --- 5) Load default texture ---
		TexturePool& pool = TexturePool::GetInstance();
		pool.LoadAllRecursively();
		missingTexture = pool.GetTexture2DLazy("mart");

		// --- 6) Cubemap setup ---
		cubemapController = std::make_unique<CubeMapController>(
			"Shaders/OpenGL/skybox_vert.glsl",
			"Shaders/OpenGL/skybox_frag.glsl"
		);
		cubemapController->SetEnabled(false);

		GLint samples = 0;
		glGetIntegerv(GL_SAMPLES, &samples);
		std::cout << "MSAA Samples: " << samples << std::endl;

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
			std::cout << "GLSL " + errorStage + " shader compile failed: " + std::string(infoLog) << std::endl;
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

		auto scene = SwimEngine::GetInstance()->GetSceneSystem()->GetActiveScene();
		if (!scene)
		{
			SwapBuffers(deviceContext);
			return;
		}

		const glm::mat4& view = cameraSystem->GetViewMatrix();
		const glm::mat4& proj = cameraSystem->GetProjectionMatrix();
		Frustum::SetCameraMatrices(view, proj);

		auto& registry = scene->GetRegistry();

		if (cubemapController)
		{
			cubemapController->Render(view, proj);
		}

		RenderWorldSpace(scene, registry, view, proj);
		RenderScreenAndDecoratorUI(registry, view, proj);

	#ifdef _DEBUG
		RenderWireframeDebug(scene);
	#endif

		SwapBuffers(deviceContext);
	}

	static bool hasUploadedOrtho = false;

	void OpenGLRenderer::UpdateUniformBuffer()
	{
		cameraUBO.view = cameraSystem->GetViewMatrix();
		cameraUBO.proj = cameraSystem->GetProjectionMatrix();

		const Camera& camera = cameraSystem->GetCamera();

		// Calculate half FOV tangents - make sure signs are correct
		float tanHalfFovY = tan(glm::radians(camera.GetFOV() * 0.5f));
		float tanHalfFovX = tanHalfFovY * camera.GetAspect();

		cameraUBO.camParams.x = tanHalfFovX;
		cameraUBO.camParams.y = tanHalfFovY;
		cameraUBO.camParams.z = camera.GetNearClip();
		cameraUBO.camParams.w = camera.GetFarClip();

		// Since this projection is always the exact same values, we only have to do it once 
		if (!hasUploadedOrtho)
		{
			cameraUBO.screenView = glm::mat4(1.0f); // Identity

			cameraUBO.screenProj = glm::ortho(
				0.0f, VirtualCanvasWidth,
				0.0f, VirtualCanvasHeight,
				-1.0f, 1.0f
			);

			hasUploadedOrtho = true;
		}

		cameraUBO.viewportSize = glm::vec2(windowWidth, windowHeight);

		// Send them to the GPU UBO at binding 0
		glBindBuffer(GL_UNIFORM_BUFFER, ubo);
		glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(CameraUBO), &cameraUBO);
		glBindBuffer(GL_UNIFORM_BUFFER, 0);
	}

	// Draws all non decorated world space objects
	void OpenGLRenderer::RenderWorldSpace(std::shared_ptr<Scene>& scene, entt::registry& registry, const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix)
	{
		const Frustum& frustum = Frustum::Get();

		glUseProgram(shaderProgram); // main shader 
		glEnable(GL_CULL_FACE);

		scene->GetSceneBVH()->QueryFrustumCallback(frustum, [&](entt::entity entity)
		{
			const Transform& tf = registry.get<Transform>(entity);
			if (tf.GetTransformSpace() != TransformSpace::World)
			{
				return;
			}

			// Skip decorator UI elements — they go in separate pass
			if (registry.any_of<DecoratorUI>(entity))
			{
				return;
			}

			DrawEntity(entity, registry, viewMatrix, projectionMatrix);
		});
	}

	// Draws all screen space objects (typically UI) and also regular transforms that happen to be in screen space
	void OpenGLRenderer::RenderScreenAndDecoratorUI(entt::registry& registry, const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix)
	{
		// We still want to do some simple per object culling
		const Frustum& frustum = Frustum::Get();

		glUseProgram(uiDecoratorShader);
		glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE); // proper blending in transparent spots with other UI objects behind each other
		glDisable(GL_CULL_FACE); // for vulkan parity, since the vulkan UI pipeline currently does not have back face culling enabled

		glm::vec2 screenScale = glm::vec2(
			static_cast<float>(windowWidth) / VirtualCanvasWidth,
			static_cast<float>(windowHeight) / VirtualCanvasHeight
		);

		registry.view<Transform, Material>().each([&](entt::entity entity, Transform& tf, Material& matComp)
		{
			const std::shared_ptr<MaterialData>& mat = matComp.data;
			bool hasDecorator = registry.any_of<DecoratorUI>(entity);
			TransformSpace space = tf.GetTransformSpace();

			if (!hasDecorator && space != TransformSpace::Screen)
			{
				return; // Not UI, not decorated, so was either already rendered or culled
			}

			const glm::vec3& pos = tf.GetPosition();
			const glm::vec3& scale = tf.GetScale();

			bool isWorld = (space == TransformSpace::World);

			const glm::mat4& model = tf.GetModelMatrix();

			// First do a simple cull check
			if (isWorld)
			{
				if (!frustum.IsVisibleLazy(matComp.data->mesh->meshBufferData->aabbMin, matComp.data->mesh->meshBufferData->aabbMax, model))
				{
					return;
				}
			}
			else
			{
				// Sceen space 2D check using window width and height
				
				// Convert quad AABB to pixel-space (by scaling virtual units up)
				glm::vec2 screenScale = glm::vec2(
					static_cast<float>(windowWidth) / VirtualCanvasWidth,
					static_cast<float>(windowHeight) / VirtualCanvasHeight
				);

				glm::vec2 halfSize = glm::vec2(scale) * 0.5f;
				glm::vec2 center = glm::vec2(pos) * screenScale;
				glm::vec2 halfSizePx = halfSize * screenScale;

				glm::vec2 minPx = center - halfSizePx;
				glm::vec2 maxPx = center + halfSizePx;

				// Clamp against the actual framebuffer
				if (maxPx.x < 0.0f || maxPx.y < 0.0f)
				{
					return; // off left or bottom
				}

				if (minPx.x > windowWidth || minPx.y > windowHeight)
				{
					return; // off right or top
				}
			}

			glm::mat4 mvp;
			glm::vec2 quadSizeInPixels;
			glm::vec2 radiusPx, strokePx;

			if (isWorld)
			{
				// World-space projection scaling
				glm::vec4 viewPos = viewMatrix * glm::vec4(pos, 1.0f);
				float absZ = std::max(std::abs(viewPos.z), 0.0001f);

				float wppX = (2.0f * absZ * cameraUBO.camParams.x) / static_cast<float>(windowWidth);
				float wppY = (2.0f * absZ * cameraUBO.camParams.y) / static_cast<float>(windowHeight);

				quadSizeInPixels = glm::vec2(scale.x / wppX, scale.y / wppY);

				glm::vec2 scaler = glm::vec2(250.0f);
				if (hasDecorator)
				{
					const DecoratorUI& deco = registry.get<DecoratorUI>(entity);
					radiusPx = glm::min((deco.cornerRadius / scaler) / glm::vec2(wppX, wppY), quadSizeInPixels * 0.5f);
					strokePx = glm::min((deco.strokeWidth / scaler) / glm::vec2(wppX, wppY), quadSizeInPixels * 0.5f);
				}

				mvp = projectionMatrix * viewMatrix * model;
			}
			else
			{
				quadSizeInPixels = glm::vec2(scale) * screenScale;

				if (hasDecorator)
				{
					const DecoratorUI& deco = registry.get<DecoratorUI>(entity);
					radiusPx = glm::min(deco.cornerRadius * screenScale, quadSizeInPixels * 0.5f);
					strokePx = glm::min(deco.strokeWidth * screenScale, quadSizeInPixels * 0.5f);
				}

				mvp = cameraUBO.screenProj * model;
			}

			glUniformMatrix4fv(loc_ui_mvp, 1, GL_FALSE, &mvp[0][0]);
			glUniform2fv(loc_ui_resolution, 1, &cameraUBO.viewportSize[0]);
			glUniform2fv(loc_ui_quadSize, 1, &quadSizeInPixels[0]);
			glUniform1i(loc_ui_isWorldSpace, isWorld ? 1 : 0);

			if (hasDecorator)
			{
				const DecoratorUI& deco = registry.get<DecoratorUI>(entity);

				glUniform4fv(loc_ui_fillColor, 1, &deco.fillColor[0]);
				glUniform4fv(loc_ui_strokeColor, 1, &deco.strokeColor[0]);
				glUniform2fv(loc_ui_cornerRadius, 1, &radiusPx[0]);
				glUniform2fv(loc_ui_strokeWidth, 1, &strokePx[0]);
				glUniform1i(loc_ui_enableStroke, deco.enableStroke ? 1 : 0);
				glUniform1i(loc_ui_enableFill, deco.enableFill ? 1 : 0);
				glUniform1i(loc_ui_roundCorners, deco.roundCorners ? 1 : 0);
				glUniform1i(loc_ui_useTexture, (deco.useMaterialTexture && mat->albedoMap) ? 1 : 0);
			}
			else
			{
				// No decorator -> use vertex color
				glUniform4f(loc_ui_fillColor, -1.0f, -1.0f, -1.0f, 1.0f);
				glUniform1i(loc_ui_enableFill, 1);
				glUniform1i(loc_ui_enableStroke, 0);
				glUniform1i(loc_ui_roundCorners, 0);
				glUniform1i(loc_ui_useTexture, mat->albedoMap ? 1 : 0);
				glUniform2fv(loc_ui_cornerRadius, 1, glm::value_ptr(glm::vec2(0.0f)));
				glUniform2fv(loc_ui_strokeWidth, 1, glm::value_ptr(glm::vec2(0.0f)));
				glUniform4f(loc_ui_strokeColor, 0, 0, 0, 1);
			}

			// Bind albedo texture
			GLuint texID = mat->albedoMap ? mat->albedoMap->GetTextureID() : missingTexture->GetTextureID();
			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, texID);
			glUniform1i(loc_ui_albedoTex, 0);

			const MeshBufferData& mesh = *mat->mesh->meshBufferData;
			glBindVertexArray(mesh.GetGLVAO());
			glDrawElements(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_SHORT, nullptr);
			glBindVertexArray(0);
		});

		glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE); // turn back off
	}

	void OpenGLRenderer::DrawEntity(entt::entity entity, entt::registry& registry, const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix)
	{
		const Transform& transform = registry.get<Transform>(entity);
		const std::shared_ptr<MaterialData>& mat = registry.get<Material>(entity).data;
		const MeshBufferData& meshData = *mat->mesh->meshBufferData;

		// Compute model matrix and full MVP
		const glm::mat4& model = transform.GetModelMatrix();
		const glm::mat4 mvp = projectionMatrix * viewMatrix * model;

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

	void OpenGLRenderer::RenderWireframeDebug(std::shared_ptr<Scene>& scene)
	{
		SceneDebugDraw* debugDraw = scene->GetSceneDebugDraw();
		constexpr bool cullWireframe = false;

		if (!debugDraw || !debugDraw->IsEnabled())
		{
			return;
		}

		glUseProgram(shaderProgram);

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

	int OpenGLRenderer::Exit()
	{
		glDeleteProgram(shaderProgram);
		glDeleteProgram(uiDecoratorShader);
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
