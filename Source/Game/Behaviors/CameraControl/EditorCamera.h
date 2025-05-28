#pragma once

#include "Engine/Systems/Entity/Behavior.h"


namespace Game
{

	class EditorCamera : public Engine::Behavior
	{

	public:

		using Engine::Behavior::Behavior;

		int Awake() override { return 0; }

		int Init() override { return 0; }

		void Update(double dt) override; // defined

		void FixedUpdate(unsigned int) override {}

		int Exit() override { return 0; }

	private:

		// Rotation around world axis
		float yaw = 0.0f;    
		float pitch = 0.0f;  

	};

}