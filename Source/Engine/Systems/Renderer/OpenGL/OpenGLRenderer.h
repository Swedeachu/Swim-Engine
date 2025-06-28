#pragma once

#include "Engine/Systems/Renderer/Renderer.h"

namespace Engine
{

	extern PFNWGLCHOOSEPIXELFORMATARBPROC g_wglChoosePixelFormatARB;

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

		void UploadMeshToMegaBuffer(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices, MeshBufferData& meshData) override;

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

		void GrowMegaBuffers(size_t requiredVertex, size_t requiredIndex);
		void CreateMegaMeshBuffer();

		// Rendering
		void RenderFrame();
		void UpdateUniformBuffer();

		void RenderWorldSpace(std::shared_ptr<Scene>& scene, entt::registry& registry, const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix);
		void RenderScreenSpaceAndDecoratedMeshes(entt::registry& registry, const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix, bool cull);

		void DrawEntity(entt::entity entity, entt::registry& registry, const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix);

		void DrawUIEntity(
			entt::entity entity,
			const Transform& tf,
			const Material& matComp,
			entt::registry& registry,
			const Frustum& frustum,
			const glm::mat4& viewMatrix,
			const glm::mat4& projectionMatrix,
			bool cull
		);

		CameraUBO cameraUBO{};

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

		// Decorator UI shader program
		GLuint decoratorShader = 0;

		// Uniform locations for UI-specific uniforms
		GLint loc_dec_mvp = -1;
		GLint loc_dec_fillColor = -1;
		GLint loc_dec_strokeColor = -1;
		GLint loc_dec_strokeWidth = -1;
		GLint loc_dec_cornerRadius = -1;
		GLint loc_dec_enableStroke = -1;
		GLint loc_dec_enableFill = -1;
		GLint loc_dec_roundCorners = -1;
		GLint loc_dec_resolution = -1;
		GLint loc_dec_quadSize = -1;
		GLint loc_dec_useTexture = -1;
		GLint loc_dec_albedoTex = -1;
		GLint loc_dec_isWorldSpace = -1;

		GLuint megaVBO = 0;          // Mega vertex buffer object
		GLuint megaEBO = 0;          // Mega element (index) buffer object
		GLuint globalVAO = 0;        // VAO used to bind VBO + instance attributes

		size_t megaVertexBufferSize = 0;
		size_t megaIndexBufferSize = 0;
		size_t currentVertexOffset = 0;
		size_t currentIndexOffset = 0;

		constexpr static size_t MESH_BUFFER_INITIAL_SIZE = 2 * 1024 * 1024; // 2MB per buffer
		constexpr static size_t MESH_BUFFER_GROWTH_SIZE = 1 * 1024 * 1024;  // Grow by 1MB

	};

}
