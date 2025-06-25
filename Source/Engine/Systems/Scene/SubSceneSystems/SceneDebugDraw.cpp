#include "PCH.h"
#include "SceneDebugDraw.h"
#include "Engine/Systems/Renderer/Core/Meshes/MeshPool.h"
#include "Engine/Systems/Renderer/Core/Material/MaterialPool.h"
#include "Engine/Systems/Renderer/Core/Material/MaterialData.h"
#include "Engine/Components/Transform.h"
#include "Engine/Components/Material.h"
#include "Engine/Components/MeshDecorator.h"

namespace Engine
{

	std::pair<std::vector<Engine::Vertex>, std::vector<uint32_t>> MakeCube()
	{
		// 8 unique corners of the cube
		std::array<glm::vec3, 8> corners = {
				glm::vec3{-0.5f, -0.5f, -0.5f}, // 0
				glm::vec3{ 0.5f, -0.5f, -0.5f}, // 1
				glm::vec3{ 0.5f,  0.5f, -0.5f}, // 2
				glm::vec3{-0.5f,  0.5f, -0.5f}, // 3
				glm::vec3{-0.5f, -0.5f,  0.5f}, // 4
				glm::vec3{ 0.5f, -0.5f,  0.5f}, // 5
				glm::vec3{ 0.5f,  0.5f,  0.5f}, // 6
				glm::vec3{-0.5f,  0.5f,  0.5f}, // 7
		};

		// Face definitions with CCW order and associated color
		struct FaceDefinition
		{
			std::array<int, 4> c;
			glm::vec3 color;
		};

		std::array<FaceDefinition, 6> faces = {
			// FRONT (+Z)
			FaceDefinition{{ {4, 5, 6, 7} }, glm::vec3(1.0f, 1.0f, 1.0f)},

			// BACK (-Z)
			FaceDefinition{{ {1, 0, 3, 2} }, glm::vec3(1.0f, 1.0f, 1.0f)},

			// LEFT (-X)
			FaceDefinition{{ {0, 4, 7, 3} }, glm::vec3(1.0f, 1.0f, 1.0f)},

			// RIGHT (+X)
			FaceDefinition{{ {5, 1, 2, 6} }, glm::vec3(1.0f, 1.0f, 1.0f)},

			// TOP (+Y)
			FaceDefinition{{ {3, 7, 6, 2} }, glm::vec3(1.0f, 1.0f, 1.0f)},

			// BOTTOM (-Y)
			FaceDefinition{{ {4, 0, 1, 5} }, glm::vec3(1.0f, 1.0f, 1.0f)},
		};

		// Define UV coordinates for each vertex of a face
		std::array<glm::vec2, 4> faceUVs = {
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
				v.position = corners[face.c[i]];
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

	void SceneDebugDraw::Init()
	{
		auto cubeData = MakeCube();
		cubeMesh = MeshPool::GetInstance().RegisterMesh("DebugDrawCube", cubeData.first, cubeData.second);
		cubeMaterialData = MaterialPool::GetInstance().RegisterMaterialData("DebugDrawCubeMaterial", cubeMesh);

		wireFrameCubeMesh = CreateAndRegisterWireframeBoxMesh(DebugColor::White, "DebugDrawCubeWireFrame");
		wireFrameCubeMaterialData = MaterialPool::GetInstance().RegisterMaterialData("DebugDrawCubeWireFrameMaterial", wireFrameCubeMesh);
	}

	std::shared_ptr<Mesh> SceneDebugDraw::CreateAndRegisterWireframeBoxMesh(DebugColor color, std::string meshName)
	{
		std::vector<Vertex> vertices;
		std::vector<uint32_t> indices;

		glm::vec3 wireColor = GetDebugColorValue(color);

		glm::vec3 corners[8] = {
				{-0.5f, -0.5f, -0.5f}, {0.5f, -0.5f, -0.5f},
				{0.5f, 0.5f, -0.5f},  {-0.5f, 0.5f, -0.5f},
				{-0.5f, -0.5f, 0.5f}, {0.5f, -0.5f, 0.5f},
				{0.5f, 0.5f, 0.5f},   {-0.5f, 0.5f, 0.5f}
		};

		int edges[12][2] = {
				{0,1}, {1,2}, {2,3}, {3,0},
				{4,5}, {5,6}, {6,7}, {7,4},
				{0,4}, {1,5}, {2,6}, {3,7}
		};

		float thickness = 0.02f;
		uint32_t indexOffset = 0;

		for (int i = 0; i < 12; i++)
		{
			glm::vec3 start = corners[edges[i][0]];
			glm::vec3 end = corners[edges[i][1]];
			glm::vec3 center = (start + end) * 0.5f;
			glm::vec3 dir = end - start;
			float length = glm::length(dir);
			glm::vec3 axis = glm::normalize(dir);

			glm::vec3 scale = glm::vec3(thickness);
			if (fabs(axis.x) > 0.9f) { scale.x = length; }
			else if (fabs(axis.y) > 0.9f) { scale.y = length; }
			else if (fabs(axis.z) > 0.9f) { scale.z = length; }

			glm::vec3 min = center - scale * 0.5f;
			glm::vec3 max = center + scale * 0.5f;

			glm::vec3 boxCorners[8] = {
					{min.x, min.y, min.z}, {max.x, min.y, min.z},
					{max.x, max.y, min.z}, {min.x, max.y, min.z},
					{min.x, min.y, max.z}, {max.x, min.y, max.z},
					{max.x, max.y, max.z}, {min.x, max.y, max.z}
			};

			uint32_t boxIndices[36] = {
					0,1,2, 2,3,0,
					4,5,6, 6,7,4,
					0,1,5, 5,4,0,
					2,3,7, 7,6,2,
					1,2,6, 6,5,1,
					3,0,4, 4,7,3
			};

			for (int j = 0; j < 8; j++)
			{
				Vertex v{};
				v.position = boxCorners[j];
				v.color = wireColor;
				v.uv = glm::vec2(0.0f);
				vertices.push_back(v);
			}

			for (int j = 0; j < 36; j++)
			{
				indices.push_back(indexOffset + boxIndices[j]);
			}

			indexOffset += 8;
		}

		return MeshPool::GetInstance().RegisterMesh(meshName, vertices, indices);
	}

	void SceneDebugDraw::Clear()
	{
		debugRegistry.clear();
	}

	std::shared_ptr<MaterialData> SceneDebugDraw::GetMeshMaterialDataFromType(MeshBoxType type)
	{
		if (type == MeshBoxType::BevelledCube)
		{
			return wireFrameCubeMaterialData;
		}
		else if (type == MeshBoxType::Cube)
		{
			return cubeMaterialData;
		}

		return nullptr; // std::unreachable in 23
	}

	void SceneDebugDraw::SubmitWireframeBoxAABB
	(
		const glm::vec3& min,
		const glm::vec3& max,
		const glm::vec4& color,
		bool enableFill,
		const glm::vec4& fillColor,
		const glm::vec2& strokeWidth,
		const glm::vec2& cornerRadius,
		int transformSpace,
		MeshBoxType boxType
	)
	{
		glm::vec3 center = (min + max) * 0.5f;
		glm::vec3 size = (max - min);

		entt::entity entity = debugRegistry.create();
		debugRegistry.emplace<Transform>(entity, center, size, glm::quat(), static_cast<TransformSpace>(transformSpace));
		
		debugRegistry.emplace<Material>(entity, Material(GetMeshMaterialDataFromType(boxType)));

		// the detailed draw data
		debugRegistry.emplace<MeshDecorator>(entity,
			fillColor,
			color, // stroke color
			strokeWidth,
			cornerRadius,
			glm::vec2(0.0f), // pad
			cornerRadius.x > 0.0f || cornerRadius.y > 0.0f, // enable rounded corners
			strokeWidth.x > 0.0f || strokeWidth.y > 0.0f, // enable stroke
			enableFill, // enable fill
			false // use texture
		);
	}

	void SceneDebugDraw::SubmitWireframeBox
	(
		const glm::vec3& position,
		const glm::vec3& scale,
		float pitchDegrees,
		float yawDegrees,
		float rollDegrees,
		const glm::vec4& color,
		bool enableFill,
		const glm::vec4& fillColor,
		const glm::vec2& strokeWidth,
		const glm::vec2& cornerRadius,
		int transformSpace,
		MeshBoxType boxType
	)
	{
		glm::vec3 eulerRadians = glm::radians(glm::vec3(pitchDegrees, yawDegrees, rollDegrees));
		glm::quat rotationQuat = glm::quat(eulerRadians);

		entt::entity entity = debugRegistry.create();
		debugRegistry.emplace<Transform>(entity, position, scale, rotationQuat, static_cast<TransformSpace>(transformSpace));

		debugRegistry.emplace<Material>(entity, Material(GetMeshMaterialDataFromType(boxType)));

		// the detailed draw data
		debugRegistry.emplace<MeshDecorator>(entity,
			fillColor, // fill color
			color,     // stroke color
			strokeWidth,
			cornerRadius,
			glm::vec2(0.0f), // padding 
			cornerRadius.x > 0.0f || cornerRadius.y > 0.0f, // enable rounded corners
			strokeWidth.x > 0.0f || strokeWidth.y > 0.0f,   // enable stroke
			enableFill, // enable fill
			false       // use texture 
		);
	}

}
