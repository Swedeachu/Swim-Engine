#pragma once

#include "OpenGLCubeMap.h"

namespace Engine
{
	
	// Forward decalre
	class Texture2D; 

	class OpenGLRenderer : public Machine
	{

		// yea this is ugly and stupid and dumb but this makes it easy on us to hack together our derived shader toy renderer
		friend class ShaderToyRendererGL;

	public:

		OpenGLRenderer(HWND hwnd, uint32_t width, uint32_t height);

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

	private:

		// Unused
		GLuint LoadSPIRVShaderStage(const std::string& path, GLenum shaderStage);

		// Initing
		bool InitOpenGLContext();
		bool SetPixelFormatForHDC(HDC hdc);

		// Rendering
		void RenderFrame();
		void UpdateUniformBuffer();
		void DrawEntity(entt::entity entity, entt::registry& registry);

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

		std::unique_ptr<OpenGLCubeMap> cubemap;

		// Shader Cached uniform locations (kinda gross)
		GLint loc_model = -1;
		GLint loc_view = -1;
		GLint loc_proj = -1;
		GLint loc_hasTexture = -1;
		GLint loc_albedoTex = -1;

	};

}
