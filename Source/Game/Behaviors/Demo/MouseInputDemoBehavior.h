#pragma once

#include "Engine/Systems/Entity/Behavior.h"
#include "Engine/Utility/ColorConstants.h"
#include <iostream>

namespace Game
{

	class MouseInputDemoBehavior : public Engine::Behavior
	{

	public:

		using Engine::Behavior::Behavior;

		int Awake() override;
		int Init() override;
		void Update(double dt) override;
		void FixedUpdate(unsigned int tickThisSecond) override;
		int Exit() override;

		void OnMouseEnter() override;
		void OnMouseHover() override;
		void OnMouseExit() override;

		void OnLeftClicked() override;
		void OnRightClicked() override;

		void OnLeftClickDown() override;
		void OnRightClickDown() override;

		void OnLeftClickUp() override;
		void OnRightClickUp() override;

	private:

		void SetColor(Engine::DebugColor color);

	};

}
