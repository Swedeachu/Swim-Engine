#pragma once

namespace Engine
{
	
	class Texture2D;

	class OpenGLRenderer : public Machine
	{

	public:

		OpenGLRenderer(HWND hwnd, uint32_t width, uint32_t height);

		int Awake() override;
		int Init() override;
		void Update(double dt) override;
		void FixedUpdate(unsigned int tickThisSecond) override;
		int Exit() override;

		void SetSurfaceSize(uint32_t newWidth, uint32_t newHeight);
		void SetFramebufferResized();

	private:

		// Shader system
		GLuint LoadSPIRVShaderStage(const std::string& path, GLenum shaderStage);
		GLuint LinkShaderProgram(const std::vector<GLuint>& shaderStages);

		// Initing
		bool InitOpenGLContext();
		bool SetPixelFormatForHDC(HDC hdc);

		// Rendering
		void RenderFrame();
		void UpdateUniformBuffer();

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

	};

}
