#pragma once

namespace Engine
{

	class OpenGLBuffer
	{

	public:

		OpenGLBuffer() = default;
		~OpenGLBuffer();

		// Creates and uploads vertex/index buffer data
		void Create(const void* vertexData, size_t vertexSize, const void* indexData, size_t indexSize);

		void Bind();

		// Frees the GL buffer memory
		void Free();

		// Getters for VAO and index count
		inline GLuint GetVAO() const { return vao; }
		inline GLuint GetIndexCount() const { return indexCount; }

	private:

		GLuint vao = 0;
		GLuint vbo = 0;
		GLuint ebo = 0;
		GLuint indexCount = 0;

	};

}
