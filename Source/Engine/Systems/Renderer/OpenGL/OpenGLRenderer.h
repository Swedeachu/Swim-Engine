#pragma once

#include "Engine/Systems/Renderer/Renderer.h"

namespace Engine
{
	
	// Forward decalre
	class Texture2D; 

	class OpenGLRenderer : public Renderer
	{

		// yea this is ugly and stupid and dumb but this makes it easy on us to hack together our derived shader toy renderer
		friend class ShaderToyRendererGL;

	public:

		void Create(HWND hwnd, uint32_t width, uint32_t height) override;

		int Awake() override;
		int Init() override;
		void Update(double dt) override;
		void FixedUpdate(unsigned int tickThisSecond) override;
		int Exit() override;

		void SetSurfaceSize(uint32_t newWidth, uint32_t newHeight);
		void SetFramebufferResized();

		static std::string LoadTextFile(const std::string& relativePath);
		GLuint CompileGLSLShader(GLenum stage, const char* source);
		GLuint LinkShaderProgram(const std::vector<GLuint>& shaderStages);

		std::unique_ptr<CubeMapController>& GetCubeMapController() override { return cubemapController; }

	private:

		// Unused
		GLuint LoadSPIRVShaderStage(const std::string& path, GLenum shaderStage);

		// Initing
		bool InitOpenGLContext();
		bool SetPixelFormatForHDC(HDC hdc);

		// Rendering
		void RenderFrame();
		void UpdateUniformBuffer();

		void RenderWorldSpace(std::shared_ptr<Scene>& scene, entt::registry& registry, const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix);
		void RenderScreenSpace(entt::registry& registry);

		void DrawEntity(entt::entity entity, entt::registry& registry, const glm::mat4& viewMatrix, const glm::mat4& projMatrix);
		void DrawEntityWithMVP(entt::entity entity, entt::registry& registry, const glm::mat4& mvp);

		void RenderWireframeDebug(std::shared_ptr<Scene>& scene);

		HDC deviceContext = nullptr;
		HGLRC glContext = nullptr;

		HWND windowHandle;
		uint32_t windowWidth;
		uint32_t windowHeight;
		bool framebufferResized = false;

		GLuint shaderProgram = 0;
		GLuint ubo = 0;

		std::shared_ptr<Texture2D> missingTexture;
		std::shared_ptr<CameraSystem> cameraSystem;

		std::unique_ptr<CubeMapController> cubemapController;

		// Shader Cached uniform locations (kinda gross)
		GLint loc_mvp = -1;
		GLint loc_view = -1;
		GLint loc_proj = -1;
		GLint loc_hasTexture = -1;
		GLint loc_albedoTex = -1;

		// --- Decorator UI shader program ---
		GLuint uiDecoratorShader = 0;

		// Uniform locations for UI-specific uniforms
		GLint loc_ui_mvp = -1;
		GLint loc_ui_fillColor = -1;
		GLint loc_ui_strokeColor = -1;
		GLint loc_ui_strokeWidth = -1;
		GLint loc_ui_cornerRadius = -1;
		GLint loc_ui_enableStroke = -1;
		GLint loc_ui_enableFill = -1;
		GLint loc_ui_roundCorners = -1;

	};

}
