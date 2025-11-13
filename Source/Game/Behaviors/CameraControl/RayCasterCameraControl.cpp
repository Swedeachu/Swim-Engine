#include "PCH.h"
#include "RayCasterCameraControl.h"
#include "Engine/Components/Transform.h"

namespace Game
{

	std::vector<Engine::Ray> cachedRays;
	std::vector<glm::vec3> hits;
	bool shouldCache = false;

	void RayCasterCameraControl::Update(double dt)
	{
		bool leftClicked = input->IsKeyTriggered(VK_LBUTTON);
		// bool rightClicked = input->IsKeyTriggered(VK_RBUTTON);
		// bool leftDown = input->IsKeyDown(VK_LBUTTON);
		// bool rightDown = input->IsKeyDown(VK_RBUTTON);

		glm::vec2 mousePos = input->GetMousePosition(false);
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
		if (input->IsKeyTriggered('R'))
		{
			shouldCache = !shouldCache;
		}

		// Q to clear cached rays
		if (input->IsKeyTriggered('Q'))
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
