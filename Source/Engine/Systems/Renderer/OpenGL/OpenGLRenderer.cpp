#include "PCH.h"
#include "OpenGLRenderer.h"
#include "Engine/SwimEngine.h"
#include "Engine/Systems/Renderer/Core/Textures/TexturePool.h"
#include "Engine/Systems/Renderer/Core/Font/FontPool.h"
#include "Engine/Systems/Renderer/Core/Meshes/MeshPool.h"
#include "Library/glm/gtc/matrix_transform.hpp"
#include "Engine/Components/Transform.h"
#include "Engine/Components/CompositeMaterial.h"
#include "Engine/Components/Material.h"
#include "Engine/Components/MeshDecorator.h"
#include "Engine/Components/TextComponent.h"
#include "Engine/Systems/Renderer/Core/Camera/CameraSystem.h"
#include "Engine/Systems/Renderer/Core/Camera/Frustum.h"
#include "Engine/Systems/Renderer/Core/Font/TextLayout.h"

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

		// glEnable(GL_BLEND); // will be enabled for decorator pass
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

		// --- 4) Load and compile Decorator Shader ---
		std::string uiVertSrc = LoadTextFile("Shaders/OpenGL/decorator_vertex.glsl");
		std::string uiFragSrc = LoadTextFile("Shaders/OpenGL/decorator_fragment.glsl");

		GLuint uiVert = CompileGLSLShader(GL_VERTEX_SHADER, uiVertSrc.c_str());
		GLuint uiFrag = CompileGLSLShader(GL_FRAGMENT_SHADER, uiFragSrc.c_str());

		decoratorShader = LinkShaderProgram({ uiVert, uiFrag });

		// Cache uniform locations for decorator shader
		loc_dec_mvp = glGetUniformLocation(decoratorShader, "mvp");
		loc_dec_fillColor = glGetUniformLocation(decoratorShader, "fillColor");
		loc_dec_strokeColor = glGetUniformLocation(decoratorShader, "strokeColor");
		loc_dec_strokeWidth = glGetUniformLocation(decoratorShader, "strokeWidth");
		loc_dec_cornerRadius = glGetUniformLocation(decoratorShader, "cornerRadius");
		loc_dec_enableStroke = glGetUniformLocation(decoratorShader, "enableStroke");
		loc_dec_enableFill = glGetUniformLocation(decoratorShader, "enableFill");
		loc_dec_roundCorners = glGetUniformLocation(decoratorShader, "roundCorners");
		loc_dec_resolution = glGetUniformLocation(decoratorShader, "resolution");
		loc_dec_quadSize = glGetUniformLocation(decoratorShader, "quadSize");
		loc_dec_useTexture = glGetUniformLocation(decoratorShader, "useTexture");
		loc_dec_albedoTex = glGetUniformLocation(decoratorShader, "albedoTex");
		loc_dec_isWorldSpace = glGetUniformLocation(decoratorShader, "isWorldSpace");

		// --- 5) Load default texture ---
		TexturePool& pool = TexturePool::GetInstance();
		// In the future we won't do this because the active scene file assets should determine which textures and models get loaded in, everything being loaded like this is just temporary behavior.
		// We will have a proper asset streaming threaded service later on.
		pool.LoadAllRecursively();
		missingTexture = pool.GetTexture2DLazy("mart");

		// --- 6) Cubemap setup ---
		cubemapController = std::make_unique<CubeMapController>(
			"Shaders/OpenGL/skybox_vert.glsl",
			"Shaders/OpenGL/skybox_frag.glsl"
		);
		cubemapController->SetEnabled(false);

		// --- 7) Load and compile MSDF Text Shader ---
		std::string txtVertSrc = LoadTextFile("Shaders/OpenGL/text_vertex.glsl");
		std::string txtFragSrc = LoadTextFile("Shaders/OpenGL/text_fragment.glsl");

		GLuint tVert = CompileGLSLShader(GL_VERTEX_SHADER, txtVertSrc.c_str());
		GLuint tFrag = CompileGLSLShader(GL_FRAGMENT_SHADER, txtFragSrc.c_str());
		textShader = LinkShaderProgram({ tVert, tFrag });

		// Cache uniform locations
		loc_txt_mvp = glGetUniformLocation(textShader, "mvp");
		loc_txt_pxToModel = glGetUniformLocation(textShader, "pxToModel");
		loc_txt_emScalePx = glGetUniformLocation(textShader, "emScalePx");
		loc_txt_isWorldSpace = glGetUniformLocation(textShader, "isWorldSpace");
		loc_txt_msdfAtlas = glGetUniformLocation(textShader, "msdfAtlas");
		loc_txt_atlasSize = glGetUniformLocation(textShader, "atlasSize");
		loc_txt_pxRange = glGetUniformLocation(textShader, "pxRange");
		loc_txt_fillColor = glGetUniformLocation(textShader, "fillColor");
		loc_txt_strokeColor = glGetUniformLocation(textShader, "strokeColor");
		loc_txt_strokeWidth = glGetUniformLocation(textShader, "strokeWidthPx");
		loc_txt_distanceRange = glGetUniformLocation(textShader, "msdfPixelRange");

		// --- 8) Create dynamic buffers for text quads ---
		glGenVertexArrays(1, &textVAO);
		glBindVertexArray(textVAO);
		glGenBuffers(1, &textVBO);
		glBindBuffer(GL_ARRAY_BUFFER, textVBO);
		glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
		glGenBuffers(1, &textEBO);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, textEBO);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
		// layout(location=0) vec2 inPosEm
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(TextVertex), reinterpret_cast<void*>(offsetof(TextVertex, posEm)));
		// layout(location=1) vec2 inUV
		glEnableVertexAttribArray(1);
		glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(TextVertex), reinterpret_cast<void*>(offsetof(TextVertex, uv)));
		glBindVertexArray(0);

		CreateMegaMeshBuffer(); // this feels like something we should do earlier

		// Load all fonts (later on will not be done here and instead be done via threaded asset streaming service on demand)
		FontPool::GetInstance().LoadAllRecursively();

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

	void OpenGLRenderer::UploadMeshToMegaBuffer(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices, MeshBufferData& meshData)
	{
		size_t vertexSize = vertices.size() * sizeof(Vertex);
		size_t indexSize = indices.size() * sizeof(uint32_t);

		// Check buffer capacity
		if (currentVertexOffset + vertexSize > megaVertexBufferSize || currentIndexOffset + indexSize > megaIndexBufferSize)
		{
			GrowMegaBuffers(vertexSize, indexSize);
		}

		// Upload vertex data
		glBindBuffer(GL_ARRAY_BUFFER, megaVBO);
		glBufferSubData(GL_ARRAY_BUFFER, currentVertexOffset, vertexSize, vertices.data());

		// Upload index data
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, megaEBO);
		glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, currentIndexOffset, indexSize, indices.data());

		// Save offsets in meshData for later use
		meshData.vertexOffsetInMegaBuffer = currentVertexOffset;
		meshData.indexOffsetInMegaBuffer = currentIndexOffset;
		meshData.indexCount = static_cast<GLsizei>(indices.size());

		// Advance buffer pointers
		currentVertexOffset += vertexSize;
		currentIndexOffset += indexSize;
	}

	void OpenGLRenderer::GrowMegaBuffers(size_t requiredVertex, size_t requiredIndex)
	{
		size_t newVertexSize = megaVertexBufferSize + std::max(requiredVertex, MESH_BUFFER_GROWTH_SIZE);
		size_t newIndexSize = megaIndexBufferSize + std::max(requiredIndex, MESH_BUFFER_GROWTH_SIZE);

		// Backup existing buffer contents
		std::vector<uint8_t> vertexBackup(megaVertexBufferSize);
		std::vector<uint8_t> indexBackup(megaIndexBufferSize);

		glBindBuffer(GL_ARRAY_BUFFER, megaVBO);
		glGetBufferSubData(GL_ARRAY_BUFFER, 0, megaVertexBufferSize, vertexBackup.data());

		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, megaEBO);
		glGetBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, megaIndexBufferSize, indexBackup.data());

		// Resize vertex buffer
		glBindBuffer(GL_ARRAY_BUFFER, megaVBO);
		glBufferData(GL_ARRAY_BUFFER, newVertexSize, nullptr, GL_DYNAMIC_DRAW);
		glBufferSubData(GL_ARRAY_BUFFER, 0, megaVertexBufferSize, vertexBackup.data());

		// Resize index buffer
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, megaEBO);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, newIndexSize, nullptr, GL_DYNAMIC_DRAW);
		glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, megaIndexBufferSize, indexBackup.data());

		// Update sizes
		megaVertexBufferSize = newVertexSize;
		megaIndexBufferSize = newIndexSize;
	}

	void OpenGLRenderer::CreateMegaMeshBuffer()
	{
		glGenVertexArrays(1, &globalVAO);
		glBindVertexArray(globalVAO);

		// Mega vertex buffer
		glGenBuffers(1, &megaVBO);
		glBindBuffer(GL_ARRAY_BUFFER, megaVBO);
		glBufferData(GL_ARRAY_BUFFER, MESH_BUFFER_INITIAL_SIZE, nullptr, GL_DYNAMIC_DRAW);
		megaVertexBufferSize = MESH_BUFFER_INITIAL_SIZE;

		// Mega index buffer
		glGenBuffers(1, &megaEBO);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, megaEBO);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, MESH_BUFFER_INITIAL_SIZE, nullptr, GL_DYNAMIC_DRAW);
		megaIndexBufferSize = MESH_BUFFER_INITIAL_SIZE;

		Vertex::SetupOpenGLAttributes(); // assumes this is bound to VAO already

		glBindVertexArray(0);
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

		// 1) World meshes (opaque/regular)
		RenderWorldSpace(scene, registry, view, proj);

		// 2) World text (transparent, depth test ON, depth write OFF)
		RenderTextMSDFWorld(registry, view, proj);

		// 3) UI meshes (decorators etc.)
		RenderScreenSpaceAndDecoratedMeshes(registry, view, proj, true);

		// 4) Screen-space text last 
		RenderTextMSDFScreen(registry, view, proj);

		// #ifdef _DEBUG
		SceneDebugDraw* debugDraw = scene->GetSceneDebugDraw();
		if (debugDraw && debugDraw->IsEnabled())
		{
			RenderScreenSpaceAndDecoratedMeshes(debugDraw->GetRegistry(), view, proj, false);
		}
		// #endif

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

			// Skip decorator elements — they go in separate pass
			if (registry.any_of<MeshDecorator>(entity))
			{
				return;
			}

			DrawEntity(entity, registry, viewMatrix, projectionMatrix);
		});
	}

	void OpenGLRenderer::DrawEntity(entt::entity entity, entt::registry& registry, const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix)
	{
		const Transform& transform = registry.get<Transform>(entity);
		const glm::mat4& model = transform.GetModelMatrix();
		const glm::mat4 mvp = projectionMatrix * viewMatrix * model;

		// === CompositeMaterial handling ===
		if (registry.any_of<CompositeMaterial>(entity))
		{
			const auto& composite = registry.get<CompositeMaterial>(entity);
			for (const auto& mat : composite.subMaterials)
			{
				const MeshBufferData& meshData = *mat->mesh->meshBufferData;

				glUniformMatrix4fv(loc_mvp, 1, GL_FALSE, &mvp[0][0]);

				bool usesTexture = (mat->albedoMap != nullptr);
				glUniform1f(loc_hasTexture, usesTexture ? 1.0f : 0.0f);

				GLuint texID = usesTexture ? mat->albedoMap->GetTextureID() : missingTexture->GetTextureID();
				glActiveTexture(GL_TEXTURE0);
				glBindTexture(GL_TEXTURE_2D, texID);
				glUniform1i(loc_albedoTex, 0);

				glBindVertexArray(globalVAO);
				glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, megaEBO);

				glDrawElementsBaseVertex(
					GL_TRIANGLES,
					meshData.indexCount,
					GL_UNSIGNED_INT,
					reinterpret_cast<void*>(meshData.indexOffsetInMegaBuffer),
					static_cast<GLint>(meshData.vertexOffsetInMegaBuffer / sizeof(Vertex))
				);
			}

			return;
		}

		// === Regular Material handling ===
		const auto& mat = registry.get<Material>(entity).data;
		const MeshBufferData& meshData = *mat->mesh->meshBufferData;

		glUniformMatrix4fv(loc_mvp, 1, GL_FALSE, &mvp[0][0]);

		bool usesTexture = (mat->albedoMap != nullptr);
		glUniform1f(loc_hasTexture, usesTexture ? 1.0f : 0.0f);

		GLuint texID = usesTexture ? mat->albedoMap->GetTextureID() : missingTexture->GetTextureID();
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, texID);
		glUniform1i(loc_albedoTex, 0);

		glBindVertexArray(globalVAO);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, megaEBO);

		glDrawElementsBaseVertex(
			GL_TRIANGLES,
			meshData.indexCount,
			GL_UNSIGNED_INT,
			reinterpret_cast<void*>(meshData.indexOffsetInMegaBuffer),
			static_cast<GLint>(meshData.vertexOffsetInMegaBuffer / sizeof(Vertex))
		);
	}

	// Draws all screen space objects (typically UI) and also regular transforms that happen to be in screen space.
	// Also draws all world space objects with mesh decorators.
	void OpenGLRenderer::RenderScreenSpaceAndDecoratedMeshes(entt::registry& registry, const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix, bool cull)
	{
		const Frustum& frustum = Frustum::Get();

		glUseProgram(decoratorShader);
		glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE);
		glEnable(GL_BLEND);
		glDisable(GL_CULL_FACE);

		// Render world-space decorators first
		glEnable(GL_DEPTH_TEST);
		glDepthMask(GL_TRUE);

		registry.view<Transform, Material>().each([&](entt::entity entity, Transform& tf, Material& matComp)
		{
			if (tf.GetTransformSpace() != TransformSpace::World)
			{
				return;
			}

			if (!registry.any_of<MeshDecorator>(entity))
			{
				return;
			}

			DrawUIEntity(entity, tf, matComp, registry, frustum, viewMatrix, projectionMatrix, cull);
		});

		// Render screen-space UI last (no depth test)
		glDisable(GL_DEPTH_TEST);
		glDepthMask(GL_FALSE);

		registry.view<Transform, Material>().each([&](entt::entity entity, Transform& tf, Material& matComp)
		{
			if (tf.GetTransformSpace() != TransformSpace::Screen)
			{
				return;
			}

			DrawUIEntity(entity, tf, matComp, registry, frustum, viewMatrix, projectionMatrix, cull);
		});

		// Restore states
		glEnable(GL_DEPTH_TEST);
		glDepthMask(GL_TRUE);
		glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
		glDisable(GL_BLEND);
		glEnable(GL_CULL_FACE);
	}

	void OpenGLRenderer::DrawUIEntity
	(
		entt::entity entity,
		const Transform& tf,
		const Material& matComp,
		entt::registry& registry,
		const Frustum& frustum,
		const glm::mat4& viewMatrix,
		const glm::mat4& projectionMatrix,
		bool cull
	)
	{
		const std::shared_ptr<MaterialData>& mat = matComp.data;
		bool hasDecorator = registry.any_of<MeshDecorator>(entity);
		TransformSpace space = tf.GetTransformSpace();
		const glm::vec3& pos = tf.GetPosition();
		const glm::vec3& scale = tf.GetScale();
		const glm::mat4& model = tf.GetModelMatrix();

		bool isWorld = (space == TransformSpace::World);

		// Frustum or screen clip culling
		if (cull)
		{
			if (isWorld)
			{
				if (!frustum.IsVisibleLazy(mat->mesh->meshBufferData->aabbMin, mat->mesh->meshBufferData->aabbMax, model))
				{
					return;
				}
			}
			else
			{
				glm::vec2 screenScale = glm::vec2(
					static_cast<float>(windowWidth) / VirtualCanvasWidth,
					static_cast<float>(windowHeight) / VirtualCanvasHeight
				);

				glm::vec2 halfSize = glm::vec2(scale) * 0.5f;
				glm::vec2 center = glm::vec2(pos) * screenScale;
				glm::vec2 halfSizePx = halfSize * screenScale;
				glm::vec2 minPx = center - halfSizePx;
				glm::vec2 maxPx = center + halfSizePx;

				if (maxPx.x < 0.0f || maxPx.y < 0.0f || minPx.x > windowWidth || minPx.y > windowHeight)
				{
					return;
				}
			}
		}

		glm::mat4 mvp;
		glm::vec2 quadSizeInPixels;
		glm::vec2 radiusPx(0.0f), strokePx(0.0f);

		if (isWorld)
		{
			glm::vec4 viewPos = viewMatrix * glm::vec4(pos, 1.0f);
			float absZ = std::max(std::abs(viewPos.z), 0.0001f);

			float wppX = (2.0f * absZ * cameraUBO.camParams.x) / static_cast<float>(windowWidth);
			float wppY = (2.0f * absZ * cameraUBO.camParams.y) / static_cast<float>(windowHeight);

			quadSizeInPixels = glm::vec2(scale.x / wppX, scale.y / wppY);

			if (hasDecorator)
			{
				const MeshDecorator& deco = registry.get<MeshDecorator>(entity);
				glm::vec2 scaler = glm::vec2(250.0f);
				radiusPx = glm::min((deco.cornerRadius / scaler) / glm::vec2(wppX, wppY), quadSizeInPixels * 0.5f);
				strokePx = glm::min((deco.strokeWidth / scaler) / glm::vec2(wppX, wppY), quadSizeInPixels * 0.5f);
			}

			mvp = projectionMatrix * viewMatrix * model;
		}
		else
		{
			glm::vec2 screenScale = glm::vec2(
				static_cast<float>(windowWidth) / VirtualCanvasWidth,
				static_cast<float>(windowHeight) / VirtualCanvasHeight
			);

			quadSizeInPixels = glm::vec2(scale) * screenScale;

			if (hasDecorator)
			{
				const MeshDecorator& deco = registry.get<MeshDecorator>(entity);
				radiusPx = glm::min(deco.cornerRadius * screenScale, quadSizeInPixels * 0.5f);
				strokePx = glm::min(deco.strokeWidth * screenScale, quadSizeInPixels * 0.5f);
			}

			mvp = cameraUBO.screenProj * model;
		}

		// Upload core uniforms
		glUniformMatrix4fv(loc_dec_mvp, 1, GL_FALSE, &mvp[0][0]);
		glUniform2fv(loc_dec_resolution, 1, &cameraUBO.viewportSize[0]);
		glUniform2fv(loc_dec_quadSize, 1, &quadSizeInPixels[0]);
		glUniform1i(loc_dec_isWorldSpace, isWorld ? 1 : 0);

		if (hasDecorator)
		{
			const MeshDecorator& deco = registry.get<MeshDecorator>(entity);

			glUniform4fv(loc_dec_fillColor, 1, &deco.fillColor[0]);
			glUniform4fv(loc_dec_strokeColor, 1, &deco.strokeColor[0]);
			glUniform2fv(loc_dec_cornerRadius, 1, &radiusPx[0]);
			glUniform2fv(loc_dec_strokeWidth, 1, &strokePx[0]);
			glUniform1i(loc_dec_enableStroke, deco.enableStroke ? 1 : 0);
			glUniform1i(loc_dec_enableFill, deco.enableFill ? 1 : 0);
			glUniform1i(loc_dec_roundCorners, deco.roundCorners ? 1 : 0);
			glUniform1i(loc_dec_useTexture, (deco.useMaterialTexture && mat->albedoMap) ? 1 : 0);
		}
		else
		{
			glUniform4f(loc_dec_fillColor, -1.0f, -1.0f, -1.0f, 1.0f);
			glUniform1i(loc_dec_enableFill, 1);
			glUniform1i(loc_dec_enableStroke, 0);
			glUniform1i(loc_dec_roundCorners, 0);
			glUniform1i(loc_dec_useTexture, mat->albedoMap ? 1 : 0);
			glUniform2fv(loc_dec_cornerRadius, 1, glm::value_ptr(glm::vec2(0.0f)));
			glUniform2fv(loc_dec_strokeWidth, 1, glm::value_ptr(glm::vec2(0.0f)));
			glUniform4f(loc_dec_strokeColor, 0, 0, 0, 1);
		}

		GLuint texID = mat->albedoMap ? mat->albedoMap->GetTextureID() : missingTexture->GetTextureID();
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, texID);
		glUniform1i(loc_dec_albedoTex, 0);

		const MeshBufferData& mesh = *mat->mesh->meshBufferData;

		glBindVertexArray(globalVAO);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, megaEBO);

		glDrawElementsBaseVertex(
			GL_TRIANGLES,
			mesh.indexCount,
			GL_UNSIGNED_INT,
			reinterpret_cast<void*>(mesh.indexOffsetInMegaBuffer),
			static_cast<GLint>(mesh.vertexOffsetInMegaBuffer / sizeof(Vertex))
		);

		glBindVertexArray(0);
	}

	// ---------------------------------------------------------------
	// World-space MSDF text
	// - Depth test ON, depth write OFF (so world text can overlay opaque meshes
	//   correctly but won't prevent later transparent things).
	// - Premultiplied alpha blending to avoid halo/box artifacts.
	// - emScale = world-units per EM (uses Transform.scale.y).
	// - pxToModel = (1,1)  (no screen-pixel mapping in world space).
	// ---------------------------------------------------------------
	void OpenGLRenderer::RenderTextMSDFWorld
	(
		entt::registry& registry,
		const glm::mat4& viewMatrix,
		const glm::mat4& projectionMatrix
	)
	{
		glUseProgram(textShader);
		glEnable(GL_BLEND);
		glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
		glEnable(GL_DEPTH_TEST);
		glDepthMask(GL_FALSE);
		glDisable(GL_CULL_FACE);

		registry.view<Transform, TextComponent>().each(
			[&](entt::entity, Transform& tf, TextComponent& tc)
		{
			if (tf.GetTransformSpace() != TransformSpace::World) return;
			if (!tc.GetFont() || !tc.GetFont()->msdfAtlas) return;

			const FontInfo& fi = *tc.GetFont();
			MsdfTextGpuInstanceData s = BuildMsdfStateWorld(tf, tc, fi, 0);

			std::vector<TextVertex> V;
			std::vector<uint32_t> I;
			V.reserve(tc.GetUtf32().size() * 4);
			I.reserve(tc.GetUtf32().size() * 6);

			EmitMsdf(tc, fi, s, [&](uint32_t, const GlyphQuad& q, const MsdfTextGpuInstanceData&)
			{
				uint32_t base = (uint32_t)V.size();
				V.push_back({ {q.plane.x,q.plane.y},{q.uv.x,q.uv.y} });
				V.push_back({ {q.plane.z,q.plane.y},{q.uv.z,q.uv.y} });
				V.push_back({ {q.plane.z,q.plane.w},{q.uv.z,q.uv.w} });
				V.push_back({ {q.plane.x,q.plane.w},{q.uv.x,q.uv.w} });
				I.insert(I.end(), { base,base + 1,base + 2,base + 2,base + 3,base });
			});

			if (V.empty()) return;

			glBindVertexArray(textVAO);
			glBindBuffer(GL_ARRAY_BUFFER, textVBO);
			glBufferData(GL_ARRAY_BUFFER, V.size() * sizeof(TextVertex), V.data(), GL_DYNAMIC_DRAW);
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, textEBO);
			glBufferData(GL_ELEMENT_ARRAY_BUFFER, I.size() * sizeof(uint32_t), I.data(), GL_DYNAMIC_DRAW);

			glm::mat4 mvp = projectionMatrix * viewMatrix * s.modelTR;
			glUniformMatrix4fv(loc_txt_mvp, 1, GL_FALSE, &mvp[0][0]);
			glUniform2fv(loc_txt_pxToModel, 1, &s.pxToModel[0]);
			glUniform1f(loc_txt_emScalePx, s.emScalePx);
			glUniform1i(loc_txt_isWorldSpace, 1);
			glUniform4fv(loc_txt_fillColor, 1, &s.fillColor[0]);
			glUniform4fv(loc_txt_strokeColor, 1, &s.strokeColor[0]);
			glUniform1f(loc_txt_strokeWidth, s.strokeWidthPx);
			glUniform1f(loc_txt_distanceRange, s.msdfPixelRange);

			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, tc.GetFont()->msdfAtlas->GetTextureID());
			glUniform1i(loc_txt_msdfAtlas, 0);

			glDrawElements(GL_TRIANGLES, (GLsizei)I.size(), GL_UNSIGNED_INT, nullptr);
			glBindVertexArray(0);
		});

		glDepthMask(GL_TRUE);
		glEnable(GL_DEPTH_TEST);
		glDisable(GL_BLEND);
	}

	// ---------------------------------------------------------------
	// Screen-space MSDF text
	// - No depth test/writes (pure overlay).
	// - Premultiplied alpha blending.
	// - emScale = pixels per EM (scale.y in VirtualCanvas units -> convert to px).
	// - pxToModel = 1/screenScale to map pixels into our ortho model space.
	// ---------------------------------------------------------------
	void OpenGLRenderer::RenderTextMSDFScreen(entt::registry& registry, const glm::mat4&, const glm::mat4&)
	{
		glUseProgram(textShader);
		glEnable(GL_BLEND);
		glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
		glDisable(GL_DEPTH_TEST);
		glDepthMask(GL_FALSE);
		glDisable(GL_CULL_FACE);

		registry.view<Transform, TextComponent>().each(
			[&](entt::entity, Transform& tf, TextComponent& tc)
		{
			if (tf.GetTransformSpace() != TransformSpace::Screen) return;
			if (!tc.GetFont() || !tc.GetFont()->msdfAtlas) return;

			const FontInfo& fi = *tc.GetFont();
			MsdfTextGpuInstanceData s = BuildMsdfStateScreen(tf, tc, fi,
				windowWidth, windowHeight, VirtualCanvasWidth, VirtualCanvasHeight, 0);

			std::vector<TextVertex> V;
			std::vector<uint32_t> I;
			V.reserve(tc.GetUtf32().size() * 4);
			I.reserve(tc.GetUtf32().size() * 6);

			EmitMsdf(tc, fi, s, [&](uint32_t, const GlyphQuad& q, const MsdfTextGpuInstanceData&)
			{
				uint32_t base = (uint32_t)V.size();
				V.push_back({ {q.plane.x,q.plane.y},{q.uv.x,q.uv.y} });
				V.push_back({ {q.plane.z,q.plane.y},{q.uv.z,q.uv.y} });
				V.push_back({ {q.plane.z,q.plane.w},{q.uv.z,q.uv.w} });
				V.push_back({ {q.plane.x,q.plane.w},{q.uv.x,q.uv.w} });
				I.insert(I.end(), { base,base + 1,base + 2,base + 2,base + 3,base });
			});

			if (V.empty()) return;

			glBindVertexArray(textVAO);
			glBindBuffer(GL_ARRAY_BUFFER, textVBO);
			glBufferData(GL_ARRAY_BUFFER, V.size() * sizeof(TextVertex), V.data(), GL_DYNAMIC_DRAW);
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, textEBO);
			glBufferData(GL_ELEMENT_ARRAY_BUFFER, I.size() * sizeof(uint32_t), I.data(), GL_DYNAMIC_DRAW);

			glm::mat4 mvp = cameraUBO.screenProj * s.modelTR;
			glUniformMatrix4fv(loc_txt_mvp, 1, GL_FALSE, &mvp[0][0]);
			glUniform2fv(loc_txt_pxToModel, 1, &s.pxToModel[0]);
			glUniform1f(loc_txt_emScalePx, s.emScalePx);
			glUniform1i(loc_txt_isWorldSpace, 0);
			glUniform4fv(loc_txt_fillColor, 1, &s.fillColor[0]);
			glUniform4fv(loc_txt_strokeColor, 1, &s.strokeColor[0]);
			glUniform1f(loc_txt_strokeWidth, s.strokeWidthPx);
			glUniform1f(loc_txt_distanceRange, s.msdfPixelRange);

			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, tc.GetFont()->msdfAtlas->GetTextureID());
			glUniform1i(loc_txt_msdfAtlas, 0);

			glDrawElements(GL_TRIANGLES, (GLsizei)I.size(), GL_UNSIGNED_INT, nullptr);
			glBindVertexArray(0);
		});

		glDepthMask(GL_TRUE);
		glEnable(GL_DEPTH_TEST);
		glDisable(GL_BLEND);
	}

	int OpenGLRenderer::Exit()
	{
		if (megaVBO) { glDeleteBuffers(1, &megaVBO); megaVBO = 0; }
		if (megaEBO) { glDeleteBuffers(1, &megaEBO); megaEBO = 0; }
		if (globalVAO) { glDeleteVertexArrays(1, &globalVAO); globalVAO = 0; }

		megaVertexBufferSize = megaIndexBufferSize = 0;
		currentVertexOffset = currentIndexOffset = 0;

		glDeleteProgram(shaderProgram);
		glDeleteProgram(decoratorShader);
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
