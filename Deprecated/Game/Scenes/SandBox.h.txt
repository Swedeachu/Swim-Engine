#pragma once

#include "Engine/Systems/Scene/Scene.h"

namespace Game
{

	class SandBox : public Engine::Scene
	{

	public:

		using Engine::Scene::Scene;

		int Awake() override;
		int Init() override;
		void Update(double dt) override;
		void FixedUpdate(unsigned int tickThisSecond) override;
		int Exit() override;

	};

}
