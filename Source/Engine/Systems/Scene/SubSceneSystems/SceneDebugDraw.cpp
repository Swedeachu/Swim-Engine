#include "PCH.h"
#include "SceneDebugDraw.h"
#include "Engine/Systems/Renderer/Core/Meshes/MeshPool.h"
#include "Engine/Systems/Renderer/Core/Material/MaterialPool.h"
#include "Engine/Systems/Renderer/Core/Material/MaterialData.h"
#include "Engine/Components/Transform.h"
#include "Engine/Components/Material.h"
#include "Engine/Components/MeshDecorator.h"
#include "Engine/Systems/Renderer/Core/Meshes/PrimitiveMeshes.h"

namespace Engine
{

	void SceneDebugDraw::Init()
	{
		auto cubeData = MakeCube();
		cubeMesh = MeshPool::GetInstance().RegisterMesh("DebugDrawCube", cubeData.vertices, cubeData.indices);
		cubeMaterialData = MaterialPool::GetInstance().RegisterMaterialData("DebugDrawCubeMaterial", cubeMesh);

		auto sphereData = MakeSphere(
			24, 48,
			glm::vec3(1, 1, 1),
			glm::vec3(1, 1, 1),
			glm::vec3(1, 1, 1)
		);
		sphereMesh = MeshPool::GetInstance().RegisterMesh("DebugDrawSphere", sphereData.vertices, sphereData.indices);
		sphereMaterialData = MaterialPool::GetInstance().RegisterMaterialData("DebugDrawSphereMaterial", sphereMesh);

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

			constexpr uint32_t boxIndices[36] = {
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

	void SceneDebugDraw::SubmitSphere
	(
		const glm::vec3& pos,
		const glm::vec3& scale,
		const glm::vec4& color
	)
	{
		entt::entity entity = immediateModeRegistry.create();
		immediateModeRegistry.emplace<Transform>(entity, pos, scale);
		immediateModeRegistry.emplace<Material>(entity, Material(sphereMaterialData));
		// the detailed draw data
		immediateModeRegistry.emplace<MeshDecorator>(entity,
			color, // fill color
			color, // stroke color
			glm::vec2(0.0f), // stroke width
			glm::vec2(0.0f), // corner radius
			glm::vec2(0.0f), // pad
			false, // enable rounded corners
			false, // enable stroke
			true, // enable fill
			false // use texture
		);
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

		entt::entity entity = immediateModeRegistry.create();
		immediateModeRegistry.emplace<Transform>(entity, center, size, glm::quat(), static_cast<TransformSpace>(transformSpace));

		immediateModeRegistry.emplace<Material>(entity, Material(GetMeshMaterialDataFromType(boxType)));

		// the detailed draw data
		immediateModeRegistry.emplace<MeshDecorator>(entity,
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

		entt::entity entity = immediateModeRegistry.create();
		immediateModeRegistry.emplace<Transform>(entity, position, scale, rotationQuat, static_cast<TransformSpace>(transformSpace));

		immediateModeRegistry.emplace<Material>(entity, Material(GetMeshMaterialDataFromType(boxType)));

		// the detailed draw data
		immediateModeRegistry.emplace<MeshDecorator>(entity,
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

	void SceneDebugDraw::SubmitRay(const Ray& ray, const glm::vec3& color /*= red*/)
	{
		// Tunables
		constexpr float kThickness = 0.01f;   // X/Y thickness of the ray line
		constexpr float kLength = 100.0f;  // how far to draw the ray visually

		// Normalize direction; if zero, bail.
		glm::vec3 dir = ray.dir;
		const float len = glm::length(dir);
		if (len <= 0.0f) return;
		dir /= len;

		// Our cube mesh is unit-sized centered at origin; scaling Z stretches along local +Z.
		// We want +Z aligned with the ray direction.
		const glm::quat rot = FromToRotation(glm::vec3(0, 0, 1), dir);

		// Center the skinny cube halfway along the ray, so it starts at ray.origin.
		const glm::vec3 position = ray.origin + dir * (kLength * 0.5f);

		// Scale: thin bar along Z
		const glm::vec3 scale(kThickness, kThickness, kLength);

		// Compose color params
		const glm::vec4 strokeColor(color, 1.0f);
		const glm::vec4 fillColor(color, 1.0f);

		// Build the debug entity: Transform + Material + MeshDecorator
		entt::entity e = immediateModeRegistry.create();

		// World-space transform (assuming Transform ctor: position, scale, rotation, space)
		immediateModeRegistry.emplace<Transform>(e, position, scale, rot, TransformSpace::World);

		// Solid cube material (skinny filled bar).
		immediateModeRegistry.emplace<Material>(e, Material(GetMeshMaterialDataFromType(MeshBoxType::Cube)));

		// MeshDecorator: enable fill, disable stroke (set stroke width = 0)
		immediateModeRegistry.emplace<MeshDecorator>(e,
			fillColor,                // fill color
			strokeColor,              // stroke color (not used if width=0)
			glm::vec2(0.0f),          // stroke width
			glm::vec2(0.0f),          // corner radius
			glm::vec2(0.0f),          // padding
			false,                    // enable rounded corners
			false,                    // enable stroke
			true,                     // enable fill
			false                     // use texture
		);
	}

}
