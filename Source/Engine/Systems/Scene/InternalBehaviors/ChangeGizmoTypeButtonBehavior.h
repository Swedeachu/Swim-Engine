#pragma once

#include "Engine/Systems/Entity/Behavior.h"

namespace Engine
{

	class ChangeGizmoTypeButtonBehavior : public Engine::Behavior
	{

	public:

		using Engine::Behavior::Behavior;

		int Init() override;
		void OnMouseHover() override;
		void OnMouseExit() override;
		void OnLeftClicked() override;

		void SetGizmoType(GizmoType t) { type = t; }
		void DeactiveIfNotThis(ChangeGizmoTypeButtonBehavior*);
		void Activate();

		void SetHoverColor(const glm::vec4& c) { hoverColor = c; }
		void SetActiveColor(const glm::vec4& c) { activeColor = c; }
		void SetRegularColor(const glm::vec4& c) { regularColor = c; }

	private:

		glm::vec4 hoverColor{ glm::vec4(1.0f) };
		glm::vec4 activeColor{ glm::vec4(1.0f) };
		glm::vec4 regularColor{ glm::vec4(1.0f) };

		GizmoType type{ GizmoType::Inactive };

		bool active{ false };

	};

}
