#pragma once

#include "Engine/Systems/Entity/Behavior.h"

namespace Game
{

	class CubeMapControlTest : public Engine::Behavior
	{

	public:

		using Engine::Behavior::Behavior;

		int Awake() override { return 0; }

		int Init() override; // implemented

		void Update(double dt) override; // implemented

		void FixedUpdate(unsigned int) override {}

		int Exit() override { return 0; }

	private:

		bool flip = false;
		bool styleToggle = false;

	};

}