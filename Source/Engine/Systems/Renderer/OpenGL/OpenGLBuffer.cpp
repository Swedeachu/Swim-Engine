#include "PCH.h"
#include "OpenGLBuffer.h"
#include "Engine/Systems/Renderer/Core/Meshes/Vertex.h"

namespace Engine
{

	void OpenGLBuffer::Create(const void* vertexData, size_t vertexSize, const void* indexData, size_t indexSize)
	{
		// Save the number of indices (assuming 4-byte uint32_t)
		indexCount = static_cast<GLuint>(indexSize / sizeof(uint32_t));

		glGenVertexArrays(1, &vao);
		glGenBuffers(1, &vbo);
		glGenBuffers(1, &ebo);

		glBindVertexArray(vao);

		// Vertex buffer
		glBindBuffer(GL_ARRAY_BUFFER, vbo);
		glBufferData(GL_ARRAY_BUFFER, vertexSize, vertexData, GL_STATIC_DRAW);

		// Index buffer
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, indexSize, indexData, GL_STATIC_DRAW);

		// Set up the vertex attribute layout using the Vertex struct
		Vertex::SetupOpenGLAttributes();

		// Unbind VAO
		glBindVertexArray(0);
	}

	void OpenGLBuffer::Bind()
	{
		glBindVertexArray(vao);
	}

	OpenGLBuffer::~OpenGLBuffer()
	{
		Free();
	}

	void OpenGLBuffer::Free()
	{
		if (ebo) { glDeleteBuffers(1, &ebo); ebo = 0; }
		if (vbo) { glDeleteBuffers(1, &vbo); vbo = 0; }
		if (vao) { glDeleteVertexArrays(1, &vao); vao = 0; }
	}

}
