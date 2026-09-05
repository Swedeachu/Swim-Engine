#include "PCH.h"
#include "Engine/Input/InputSystem.h"
#include "RayCasterCameraControl.h"
#include "Engine/Components/Transform.h"

namespace Engine
{

	std::vector<Engine::Ray> cachedRays;
	std::vector<glm::vec3> hits;
	bool shouldCache = false;

	void RayCasterCameraControl::Update(double dt)
	{
		bool leftClicked = input->IsMouseButtonTriggered(Swim::Platform::MouseButton::Left);
		// bool rightClicked = input->IsMouseButtonTriggered(Swim::Platform::MouseButton::Right);
		// bool leftDown = input->IsMouseButtonDown(Swim::Platform::MouseButton::Left);
		// bool rightDown = input->IsMouseButtonDown(Swim::Platform::MouseButton::Right);

		const auto mousePosInput = input->GetMousePosition();
		glm::vec2 mousePos{ mousePosInput.X, mousePosInput.Y };
		Engine::Ray ray = scene->ScreenPointToRay(mousePos);

		// Left click to try and click an object in the scene
		if (leftClicked)
		{
			float out = 0.0f;
			entt::entity hit = scene->GetSceneBVH()->RayCastClosestHit(ray, 0.0f, std::numeric_limits<float>::infinity(), &out);

			if (hit != entt::null)
			{
				ray.debugColor = glm::vec3(0.0f, 1.0f, 0.0f); // Green means hit something
				std::cout << "hit entity ray cast" << std::endl;
				Engine::Transform& tf = scene->GetRegistry().get<Engine::Transform>(hit); // how would this behave if no transform is found?
				glm::vec3 pos = tf.GetPosition();
				glm::vec3 scale = tf.GetScale();
				if (shouldCache) hits.push_back(ray.At(out)); // save hit position we will debug draw as a green sphere
			}
			else
			{
				ray.debugColor = glm::vec3(1.0f, 0.0f, 0.0f); // Red means missed
			}

			if (shouldCache)
			{
				cachedRays.push_back(ray);
			}
		}

		// R to toggle ray caching for debug view
		if (input->IsKeyTriggered(Swim::Platform::KeyCode::R))
		{
			shouldCache = !shouldCache;
		}

		// Q to clear cached rays
		if (input->IsKeyTriggered(Swim::Platform::KeyCode::Q))
		{
			cachedRays.clear();
			hits.clear();
		}

		auto* db = scene->GetSceneDebugDraw();

		// Draw persistent rays
		for (Engine::Ray& r : cachedRays)
		{
			db->SubmitRay(r, r.debugColor);
		}

		// Draw persistent hits
		for (glm::vec3& hit : hits)
		{
			db->SubmitSphere(hit, glm::vec3(0.1f), glm::vec4(0.0f, 1.0f, 0.0f, 1.0f));
		}
	}

}
