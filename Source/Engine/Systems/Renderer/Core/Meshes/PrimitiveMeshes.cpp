#include "PCH.h"
#include "PrimitiveMeshes.h"
#include "Engine/Utility/RandomUtils.h"

namespace Engine
{

	static constexpr std::array<glm::vec3, 8> cubeCorners = {
		glm::vec3{-0.5f, -0.5f, -0.5f},
		glm::vec3{ 0.5f, -0.5f, -0.5f},
		glm::vec3{ 0.5f,  0.5f, -0.5f},
		glm::vec3{-0.5f,  0.5f, -0.5f},
		glm::vec3{-0.5f, -0.5f,  0.5f},
		glm::vec3{ 0.5f, -0.5f,  0.5f},
		glm::vec3{ 0.5f,  0.5f,  0.5f},
		glm::vec3{-0.5f,  0.5f,  0.5f},
	};

	// Face definitions with CCW order and associated color
	static struct FaceDefinition
	{
		std::array<int, 4> c;
		glm::vec3 color;
	};

	static bool DegenerateTri(const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2)
	{
		return (glm::distance(v0, v1) < EPSILON ||
			glm::distance(v1, v2) < EPSILON ||
			glm::distance(v2, v0) < EPSILON);
	}

	static inline void PushVertex(std::vector<Engine::Vertex>& verts,
		const glm::vec3& p, const glm::vec3& c, const glm::vec2& uv)
	{
		Engine::Vertex v{};
		v.position = p;
		v.color = c;
		v.uv = uv;
		verts.push_back(v);
	}

	VertexesIndexesPair MakeCube()
	{
		static constexpr std::array<FaceDefinition, 6> faces = {
			// FRONT (+Z)
			FaceDefinition{{ {4, 5, 6, 7} }, glm::vec3(1.0f, 0.0f, 0.0f)}, // Red

			// BACK (-Z)
			FaceDefinition{{ {1, 0, 3, 2} }, glm::vec3(0.0f, 1.0f, 0.0f)}, // Green

			// LEFT (-X)
			FaceDefinition{{ {0, 4, 7, 3} }, glm::vec3(0.0f, 0.0f, 1.0f)}, // Blue

			// RIGHT (+X)
			FaceDefinition{{ {5, 1, 2, 6} }, glm::vec3(1.0f, 1.0f, 0.0f)}, // Yellow

			// TOP (+Y)
			FaceDefinition{{ {3, 7, 6, 2} }, glm::vec3(1.0f, 0.0f, 1.0f)}, // Magenta

			// BOTTOM (-Y)
			FaceDefinition{{ {4, 0, 1, 5} }, glm::vec3(0.0f, 1.0f, 1.0f)}, // Cyan
		};

		// Define UV coordinates for each vertex of a face
		static constexpr std::array<glm::vec2, 4> faceUVs = {
				glm::vec2(0.0f, 1.0f), // Bottom-left
				glm::vec2(1.0f, 1.0f), // Bottom-right
				glm::vec2(1.0f, 0.0f), // Top-right
				glm::vec2(0.0f, 0.0f)  // Top-left
		};

		// Build the 24 vertices (4 vertices per face)
		std::vector<Engine::Vertex> vertices;
		vertices.reserve(24);

		for (const auto& face : faces)
		{
			for (int i = 0; i < 4; i++)
			{
				Engine::Vertex v{};
				v.position = cubeCorners[face.c[i]];
				v.color = face.color;
				v.uv = faceUVs[i]; // Assign proper UVs
				vertices.push_back(v);
			}
		}

		// Build the 36 indices (6 faces * 6 indices per face)
		std::vector<uint32_t> indices;
		indices.reserve(36);
		for (int faceIdx = 0; faceIdx < 6; faceIdx++)
		{
			uint32_t base = faceIdx * 4;
			// First triangle of the face
			indices.push_back(base + 0);
			indices.push_back(base + 1);
			indices.push_back(base + 2);

			// Second triangle of the face
			indices.push_back(base + 2);
			indices.push_back(base + 3);
			indices.push_back(base + 0);
		}

		return { vertices, indices };
	}

	VertexesIndexesPair MakeRandomColoredCube()
	{
		static constexpr std::array<std::array<int, 4>, 6> faceIndices = {
			std::array<int,4>{4,5,6,7},  // Front
			std::array<int,4>{1,0,3,2},  // Back
			std::array<int,4>{0,4,7,3},  // Left
			std::array<int,4>{5,1,2,6},  // Right
			std::array<int,4>{3,7,6,2},  // Top
			std::array<int,4>{4,0,1,5},  // Bottom
		};

		static constexpr std::array<glm::vec2, 4> uvs = {
			glm::vec2(0.0f, 1.0f),
			glm::vec2(1.0f, 1.0f),
			glm::vec2(1.0f, 0.0f),
			glm::vec2(0.0f, 0.0f),
		};

		std::vector<Engine::Vertex> vertices;
		std::vector<uint32_t> indices;

		vertices.reserve(24);
		indices.reserve(36);

		for (int face = 0; face < 6; ++face)
		{
			glm::vec3 color = glm::vec3(
				Engine::RandFloat(0.2f, 1.0f),
				Engine::RandFloat(0.2f, 1.0f),
				Engine::RandFloat(0.2f, 1.0f)
			);

			for (int i = 0; i < 4; ++i)
			{
				Engine::Vertex v{};
				v.position = cubeCorners[faceIndices[face][i]];
				v.uv = uvs[i];
				v.color = color;
				vertices.push_back(v);
			}

			uint32_t base = face * 4;
			indices.push_back(base + 0);
			indices.push_back(base + 1);
			indices.push_back(base + 2);
			indices.push_back(base + 2);
			indices.push_back(base + 3);
			indices.push_back(base + 0);
		}

		return { vertices, indices };
	}

	VertexesIndexesPair MakeSphere
	(
		int latitudeSegments,
		int longitudeSegments,
		glm::vec3 colorTop,
		glm::vec3 colorMid,
		glm::vec3 colorBottom
	)
	{
		std::vector<Engine::Vertex> vertices;
		std::vector<uint32_t> indices;

		// Clamp to minimum sensible values
		latitudeSegments = std::max(3, latitudeSegments);
		longitudeSegments = std::max(3, longitudeSegments);

		// Generate all vertices
		for (int lat = 0; lat <= latitudeSegments; ++lat)
		{
			float v = static_cast<float>(lat) / latitudeSegments; // [0,1]
			float theta = glm::pi<float>() * v;                        // [0, pi]

			float sinTheta = std::sin(theta);
			float cosTheta = std::cos(theta);

			for (int lon = 0; lon <= longitudeSegments; ++lon)
			{
				float u = static_cast<float>(lon) / longitudeSegments; // [0,1]
				float phi = glm::two_pi<float>() * u;                    // [0, 2pi]

				float sinPhi = std::sin(phi);
				float cosPhi = std::cos(phi);

				glm::vec3 pos;
				pos.x = sinTheta * cosPhi;
				pos.y = cosTheta;
				pos.z = sinTheta * sinPhi;

				glm::vec3 color;

				// Interpolate top -> mid -> bottom along v (Y)
				if (v < 0.5f)
				{
					float t = v * 2.0f;
					color = glm::mix(colorTop, colorMid, t);
				}
				else
				{
					float t = (v - 0.5f) * 2.0f;
					color = glm::mix(colorMid, colorBottom, t);
				}

				// glm::vec2 uv = glm::vec2(u, 1.0f - v);
				glm::vec2 uv = glm::vec2(u, v);

				Engine::Vertex vert;
				vert.position = pos * 0.5f; // Unit sphere scaled to [-0.5, 0.5]
				vert.color = color;
				vert.uv = uv;

				vertices.push_back(vert);
			}
		}

		// Generate indices (CCW winding)
		for (int lat = 0; lat < latitudeSegments; ++lat)
		{
			for (int lon = 0; lon < longitudeSegments; ++lon)
			{
				int current = lat * (longitudeSegments + 1) + lon;
				int next = current + longitudeSegments + 1;

				// Triangle 1 (CCW)
				indices.push_back(static_cast<uint32_t>(current));
				indices.push_back(static_cast<uint32_t>(current + 1));
				indices.push_back(static_cast<uint32_t>(next));

				// Triangle 2 (CCW)
				indices.push_back(static_cast<uint32_t>(current + 1));
				indices.push_back(static_cast<uint32_t>(next + 1));
				indices.push_back(static_cast<uint32_t>(next));
			}
		}

		return { vertices, indices };
	}

	VertexesIndexesPair MakeQuad
	(
		uint32_t tilesX,          
		uint32_t tilesY,          
		uint32_t tileIndexX,      
		uint32_t tileIndexY,       
		glm::vec3 color1,
		glm::vec3 color2,
		glm::vec3 color3,
		glm::vec3 color4
	)
	{
		tilesX = std::max(1u, tilesX);
		tilesY = std::max(1u, tilesY);

		// Compute UV range for this tile
		const float uStep = 1.0f / static_cast<float>(tilesX);
		const float vStep = 1.0f / static_cast<float>(tilesY);

		const float u0 = uStep * static_cast<float>(tileIndexX);
		const float v0 = vStep * static_cast<float>(tileIndexY);
		const float u1 = u0 + uStep;
		const float v1 = v0 + vStep;

		// Define vertices and indices for a colored quad mesh
		std::vector<Engine::Vertex> quadVertices = {
			//   position           color     uv
			{{-0.5f, -0.5f, 0.0f}, color1, {u0, v1}},  // bottom-left
			{{ 0.5f, -0.5f, 0.0f}, color2, {u1, v1}},  // bottom-right
			{{ 0.5f,  0.5f, 0.0f}, color3, {u1, v0}},  // top-right
			{{-0.5f,  0.5f, 0.0f}, color4, {u0, v0}}   // top-left
		};

		std::vector<uint32_t> quadIndices = { 0, 1, 2, 2, 3, 0 };

		return { quadVertices, quadIndices };
	}

	VertexesIndexesPair MakeCircle
	(
		float radius,
		uint32_t segmentCount,
		const glm::vec3& color
	)
	{
		std::vector<Engine::Vertex> vertices;
		std::vector<uint32_t> indices;

		// Add center vertex of triangle fan
		vertices.push_back({
				{0.0f, 0.0f, 0.0f}, // Center position
				color,              // Uniform color
				{0.5f, 0.5f}        // Center UV
			});

		// Add outer circle vertices
		for (uint32_t i = 0; i <= segmentCount; ++i)
		{
			float angle = (float)i / (float)segmentCount * 2.0f * M_PI;

			float x = radius * cosf(angle);
			float y = radius * sinf(angle);

			// UV mapped from [-radius, radius] => [0,1]
			float u = 0.5f + (x / (radius * 2.0f));
			float v = 0.5f - (y / (radius * 2.0f));  // Flip Y for typical UVs

			vertices.push_back({
					{x, y, 0.0f},
					color,
					{u, v}
				});
		}

		// Generate indices for triangle fan
		for (uint32_t i = 1; i <= segmentCount; ++i)
		{
			indices.push_back(0);        // Center
			indices.push_back(i);        // Current vertex
			indices.push_back(i + 1);    // Next vertex (wraps around)
		}

		return { vertices, indices };
	}

	VertexesIndexesPair MakeCylinder
	(
		float radius,
		float height,
		uint32_t segmentCount,
		const glm::vec3& color
	)
	{
		std::vector<Engine::Vertex> vertices;
		std::vector<uint32_t> indices;

		segmentCount = std::max<uint32_t>(3, segmentCount);
		const float halfHeight = height * 0.5f;

		vertices.reserve((segmentCount + 1) * 2 + 2 + 2 * (segmentCount + 1));
		indices.reserve(segmentCount * 6 /*side*/ + segmentCount * 3 /*top*/ + segmentCount * 3 /*bottom*/);

		// Sides
		const uint32_t sideStart = (uint32_t)vertices.size();
		for (uint32_t i = 0; i <= segmentCount; ++i)
		{
			const float u = (float)i / (float)segmentCount;
			const float a = u * Engine::TWO_PI;
			const float x = radius * cosf(a);
			const float z = radius * sinf(a);

			// bottom, top
			Engine::Vertex vb{}, vt{};
			vb.position = { x, -halfHeight, z };
			vb.color = color;
			vb.uv = { u, 0.0f };

			vt.position = { x,  halfHeight, z };
			vt.color = color;
			vt.uv = { u, 1.0f };

			vertices.push_back(vb);
			vertices.push_back(vt);
		}

		// Sides
		for (uint32_t i = 0; i < segmentCount; ++i)
		{
			const uint32_t base = sideStart + i * 2;

			indices.push_back(base + 0);
			indices.push_back(base + 1);
			indices.push_back(base + 2);

			indices.push_back(base + 2);
			indices.push_back(base + 1);
			indices.push_back(base + 3);
		}

		// Top cap
		const uint32_t topCenter = (uint32_t)vertices.size();
		{
			Engine::Vertex c{};
			c.position = { 0.0f,  halfHeight, 0.0f };
			c.color = color;
			c.uv = { 0.5f, 0.5f };
			vertices.push_back(c);
		}

		const uint32_t topRingStart = (uint32_t)vertices.size();
		for (uint32_t i = 0; i <= segmentCount; ++i)
		{
			const float u = (float)i / (float)segmentCount;
			const float a = u * Engine::TWO_PI;
			const float x = radius * cosf(a);
			const float z = radius * sinf(a);

			Engine::Vertex v{};
			v.position = { x,  halfHeight, z };
			v.color = color;
			// planar UVs
			v.uv = { 0.5f + x / (2.0f * radius), 0.5f + z / (2.0f * radius) };
			vertices.push_back(v);
		}

		for (uint32_t i = 0; i < segmentCount; ++i)
		{
			// Fan
			indices.push_back(topCenter);
			indices.push_back(topRingStart + i + 1);
			indices.push_back(topRingStart + i);
		}

		// Bottom cap
		const uint32_t botCenter = (uint32_t)vertices.size();
		{
			Engine::Vertex c{};
			c.position = { 0.0f, -halfHeight, 0.0f };
			c.color = color;
			c.uv = { 0.5f, 0.5f };
			vertices.push_back(c);
		}

		const uint32_t botRingStart = (uint32_t)vertices.size();
		for (uint32_t i = 0; i <= segmentCount; ++i)
		{
			const float u = (float)i / (float)segmentCount;
			const float a = u * Engine::TWO_PI;
			const float x = radius * cosf(a);
			const float z = radius * sinf(a);

			Engine::Vertex v{};
			v.position = { x, -halfHeight, z };
			v.color = color;
			// planar UVs; keep same mapping convention as top
			v.uv = { 0.5f + x / (2.0f * radius), 0.5f + z / (2.0f * radius) };
			vertices.push_back(v);
		}

		for (uint32_t i = 0; i < segmentCount; ++i)
		{
			// Fan
			indices.push_back(botCenter);
			indices.push_back(botRingStart + i);
			indices.push_back(botRingStart + i + 1);
		}

		return { vertices, indices };
	}

	VertexesIndexesPair MakeCone
	(
		float radius,
		float height,
		uint32_t segmentCount,
		const glm::vec3& color
	)
	{
		std::vector<Engine::Vertex> vertices;
		std::vector<uint32_t> indices;

		float halfHeight = height * 0.5f;
		glm::vec3 tip(0.0f, halfHeight, 0.0f);

		// Base ring
		for (uint32_t i = 0; i <= segmentCount; ++i)
		{
			float u = (float)i / (float)segmentCount;
			float angle = u * TWO_PI;
			float x = radius * cosf(angle);
			float z = radius * sinf(angle);
			PushVertex(vertices, { x, -halfHeight, z }, color, { u, 0.0f });
		}

		// Tip vertex
		uint32_t tipIndex = (uint32_t)vertices.size();
		PushVertex(vertices, tip, color, { 0.5f, 1.0f });

		// Sides
		for (uint32_t i = 0; i < segmentCount; ++i)
		{
			indices.push_back(i);
			indices.push_back(i + 1);
			indices.push_back(tipIndex);
		}

		// Base center
		uint32_t baseCenter = (uint32_t)vertices.size();
		PushVertex(vertices, { 0.0f, -halfHeight, 0.0f }, color, { 0.5f, 0.5f });

		// Base ring again for the cap
		uint32_t baseStart = (uint32_t)vertices.size();
		for (uint32_t i = 0; i <= segmentCount; ++i)
		{
			float a = (float)i / (float)segmentCount * TWO_PI;
			float x = radius * cosf(a);
			float z = radius * sinf(a);
			PushVertex(vertices, { x, -halfHeight, z }, color, { 0.5f + x / (2.0f * radius), 0.5f - z / (2.0f * radius) });
		}

		for (uint32_t i = 0; i < segmentCount; ++i)
		{
			indices.push_back(baseCenter);
			indices.push_back(baseStart + i + 1);
			indices.push_back(baseStart + i);
		}

		return { vertices, indices };
	}

	VertexesIndexesPair MakeTorus
	(
		float outerRadius,
		float thickness,
		uint32_t majorSegments,
		uint32_t minorSegments,
		const glm::vec3& color
	)
	{
		std::vector<Engine::Vertex> vertices;
		std::vector<uint32_t> indices;

		float R = outerRadius;
		float r = thickness;

		for (uint32_t i = 0; i <= majorSegments; ++i)
		{
			float u = (float)i / (float)majorSegments * TWO_PI;
			float cu = cosf(u);
			float su = sinf(u);

			for (uint32_t j = 0; j <= minorSegments; ++j)
			{
				float v = (float)j / (float)minorSegments * TWO_PI;
				float cv = cosf(v);
				float sv = sinf(v);

				glm::vec3 pos{
					(R + r * cv) * cu,
					r * sv,
					(R + r * cv) * su
				};

				PushVertex(vertices, pos, color, { (float)i / majorSegments, (float)j / minorSegments });
			}
		}

		for (uint32_t i = 0; i < majorSegments; ++i)
		{
			for (uint32_t j = 0; j < minorSegments; ++j)
			{
				uint32_t i0 = i * (minorSegments + 1) + j;
				uint32_t i1 = i * (minorSegments + 1) + (j + 1);
				uint32_t i2 = (i + 1) * (minorSegments + 1) + (j + 1);
				uint32_t i3 = (i + 1) * (minorSegments + 1) + j;

				indices.push_back(i0);
				indices.push_back(i1);
				indices.push_back(i2);
				indices.push_back(i2);
				indices.push_back(i3);
				indices.push_back(i0);
			}
		}

		return { vertices, indices };
	}

	VertexesIndexesPair MakeArrow
	(
		float shaftRadius,
		float shaftLength,
		float headRadius,
		float headLength,
		uint32_t segmentCount,
		const glm::vec3& color
	)
	{
		// Safety clamps
		segmentCount = std::max<uint32_t>(3, segmentCount);
		shaftRadius = std::max(shaftRadius, 0.0001f);
		headRadius = std::max(headRadius, 0.0001f);
		shaftLength = std::max(shaftLength, 0.0001f);
		headLength = std::max(headLength, 0.0001f);

		// 1) Build centered shaft ([-L/2, +L/2] in Y), then translate it up by +L/2 so it spans [0, L]
		auto shaft = MakeCylinder(shaftRadius, shaftLength, segmentCount, color);
		{
			const float yOff = shaftLength * 0.5f;
			for (auto& v : shaft.vertices) { v.position.y += yOff; }
		}

		// 2) Build centered head (base at -H/2, tip at +H/2), then translate it so base is at y=shaftLength.
		// After translation: base => shaftLength, tip => shaftLength + headLength
		auto head = MakeCone(headRadius, headLength, segmentCount, color);
		{
			const float yOff = shaftLength + headLength * 0.5f;
			for (auto& v : head.vertices) { v.position.y += yOff; }
		}

		// 3) Merge (append head onto shaft)
		std::vector<Engine::Vertex> vertices;
		std::vector<uint32_t> indices;
		vertices.reserve(shaft.vertices.size() + head.vertices.size());
		indices.reserve(shaft.indices.size() + head.vertices.size());

		// Shaft
		vertices.insert(vertices.end(), shaft.vertices.begin(), shaft.vertices.end());
		indices.insert(indices.end(), shaft.indices.begin(), shaft.indices.end());

		// Head (indices offset)
		const uint32_t baseIndex = static_cast<uint32_t>(shaft.vertices.size());
		for (auto idx : head.indices) { indices.push_back(baseIndex + idx); }
		vertices.insert(vertices.end(), head.vertices.begin(), head.vertices.end());

		return { vertices, indices };
	}

}
