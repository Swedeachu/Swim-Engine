#pragma once

#include "Engine/Machine.h"
#include "Engine/Systems/Renderer/Core/MathTypes/Axis.h"
#include "Library/EnTT/entt.hpp"

namespace Engine
{

	// Forward declared
	class Scene;

	enum class GizmoType : int
	{
		Translate, Scale, Rotate, Inactive
	};

	class GizmoSystem : public Machine
	{

	public:

		int Awake() override;

		int Init() override;

		void Update(double dt) override;

		void FixedUpdate(unsigned int tickThisSecond) override {}

		int Exit() override { return 0; }

		void SetScene(std::shared_ptr<Scene>& scene) { activeScene = scene; }

		void SetGizmoType(GizmoType type);

	private:

		void NothingSelectedYetBehavior();

		void SelectedEntityToControlWithGizmo(entt::entity);

		void GizmoRootControl();

		void LoseFocus(bool setFocusedEntityNull = true);

		entt::entity LeftClickCheck();

		void CreateTranslationGizmo(bool useBallArrow);

		void ScaleGizmoBasedOnCameraDistance(entt::registry& reg);

		entt::entity RayCastUnderMouse() const;

		Axis AxisFromTagEntity(entt::entity e) const;

		void SetAxisHighlight(Axis hovered, Axis active);

		void BeginDrag(Axis axis, const glm::vec3& rayOrigin, const glm::vec3& rayDirN);

		void UpdateDrag(const glm::vec3& rayOrigin, const glm::vec3& rayDirN);

		void EndDrag();

		// Our gizmo is an invisible root entity with a transform that has the control entities parented to it.
		entt::entity rootGizmoControl = entt::null;
		entt::entity focusedEntity = entt::null;

		GizmoType activeGizmoType = GizmoType::Inactive;

		std::shared_ptr<Mesh> sphereMesh;
		std::shared_ptr<Mesh> arrowMesh;
		std::shared_ptr<Mesh> ringMesh;
		std::shared_ptr<Mesh> cubeMesh;
		std::shared_ptr<Mesh> quadMesh;
		std::shared_ptr<Mesh> circleMesh;
		std::shared_ptr<Mesh> ballArrowMesh;

		std::shared_ptr<MaterialData> sphereMatData;
		std::shared_ptr<MaterialData> arrowMatData;
		std::shared_ptr<MaterialData> ringMatData;
		std::shared_ptr<MaterialData> cubeMatData;
		std::shared_ptr<MaterialData> quadMatData;
		std::shared_ptr<MaterialData> circleMatData;
		std::shared_ptr<MaterialData> ballArrowMatData;

		std::shared_ptr<Scene> activeScene;

		entt::entity gizmoUI = entt::null;

		// Axis state
		entt::entity axisX = entt::null;
		entt::entity axisY = entt::null;
		entt::entity axisZ = entt::null;

		Axis hoveredAxis = Axis::None;
		Axis activeAxisDrag = Axis::None;
		bool isDragging = false;

		// Drag math
		float dragStartT = 0.0f;
		glm::vec3 dragAxisDir = glm::vec3(0);
		glm::vec3 dragStartObjPos = glm::vec3(0);
		glm::vec3 dragStartObjScale = glm::vec3(0);

	};

}
