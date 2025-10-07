#pragma once

#include "Library/EnTT/entt.hpp"
#include "Library/glm/glm.hpp"
#include "Engine/Utility/ColorConstants.h"
#include "Engine/Systems/Renderer/Core/MathTypes/Ray.h"

namespace Engine
{

	// forward declare
	class Mesh;
	struct MaterialData;

	class SceneDebugDraw
	{

	public:

		enum MeshBoxType
		{
			Cube, // can have fill as it has 6 faces, but can be used with no fill and stroke to appear as a wireframe box. This is used by default since it has fill capabilities.
			BevelledCube, // literally has no faces, just bevelled edges to make a wireframe box
		};

		SceneDebugDraw() = default;

		void Init();
		void Clear();

		void SetEnabled(bool value) { enabled = value; }
		const bool IsEnabled() const { return enabled; }

		void SubmitSphere
		(
			const glm::vec3& pos,
			const glm::vec3& scale = glm::vec3(1.0f),
			const glm::vec4& color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f)
		);

		// Draws an AABB of any color, allows you to optionally fill the box solid and set stroke width and corner radius, including drawing space (0 = world, 1 = screen)
		void SubmitWireframeBoxAABB
		(
			const glm::vec3& min,
			const glm::vec3& max,
			const glm::vec4& color = { 1.0f, 0.0f, 0.0f, 1.0f },
			bool enableFill = false,
			const glm::vec4& fillColor = { 0.0f, 0.0f, 0.0f, 0.0f },
			const glm::vec2& strokeWidth = glm::vec2(10.0f),
			const glm::vec2& cornerRadius = glm::vec2(0.0f),
			int transformSpace = 0,
			MeshBoxType boxType = MeshBoxType::Cube
		);

		// Draws a box of any color, allowing you to change position and rotation values along with fill color and stroke and width and radius, including drawing space (0 = world, 1 = screen)
		void SubmitWireframeBox
		(
			const glm::vec3& position,
			const glm::vec3& scale,
			float pitchDegrees = 0.0f,
			float yawDegrees = 0.0f,
			float rollDegrees = 0.0f,
			const glm::vec4& color = { 1.0f, 0.0f, 0.0f, 1.0f },
			bool enableFill = false,
			const glm::vec4& fillColor = { 0.0f, 0.0f, 0.0f, 0.0f },
			const glm::vec2& strokeWidth = glm::vec2(10.0f),
			const glm::vec2& cornerRadius = glm::vec2(0.0f),
			int transformSpace = 0,
			MeshBoxType boxType = MeshBoxType::Cube
		);

		void SubmitRay
		(
			const Ray& ray,
			const glm::vec3& color = glm::vec3(1.0f, 0.0f, 0.0f)
		);

		entt::registry& GetRegistry()
		{
			return debugRegistry;
		}

	private:

		std::shared_ptr<Mesh> CreateAndRegisterWireframeBoxMesh(DebugColor color, std::string meshName);

		std::shared_ptr<MaterialData> GetMeshMaterialDataFromType(MeshBoxType type);

		bool enabled{ false };
		entt::registry debugRegistry;

		std::shared_ptr<Mesh> sphereMesh;
		std::shared_ptr<Mesh> cubeMesh;
		std::shared_ptr<Mesh> wireFrameCubeMesh;
		std::shared_ptr<MaterialData> cubeMaterialData;
		std::shared_ptr<MaterialData> wireFrameCubeMaterialData;
		std::shared_ptr<MaterialData> sphereMaterialData;

	};

}
