#include "PCH.h"
#include "EntityFactory.h"

namespace Engine
{

	void EntityFactory::ProcessQueues()
	{
		// Pull fresh scene from the engine every time
		auto scene = SwimEngine::GetInstance()->GetSceneSystem()->GetActiveScene();
		if (!scene)
		{
			throw std::runtime_error("EntityFactory: No active scene found.");
		}

		entt::registry& registry = scene->GetRegistry();

		// Create new entities and apply callbacks
		while (!createQueue.empty())
		{
			entt::entity entity = scene->CreateEntity();
			createQueue.front()(registry, entity);
			createQueue.pop();
		}

		// Destroy entities using their destruction callbacks
		while (!destroyQueue.empty())
		{
			destroyQueue.front()();
			destroyQueue.pop();
		}
	}

	void EntityFactory::CreateWithTransform(const Transform& transform)
	{
		QueueCreate([transform](entt::registry& reg, entt::entity e)
		{
			reg.emplace<Transform>(e, transform); // Move if possible, copy fallback
		});
	}

	void EntityFactory::CreateWithTransformAndMaterial(const Transform& transform, const Material& material)
	{
		// Delegate to the templated overload with a no-op callback
		CreateWithTransformAndMaterial(
			transform, material,
			[](entt::entity, Transform&, Material&) {}
		);
	}

	// This destorys an entity and calls exits on all its behaviors and children, freeing them entirely from the registry and memory
	void EntityFactory::Destroy(entt::entity entity)
	{
		QueueDestroy([](entt::entity e)
		{
			auto scene = SwimEngine::GetInstance()->GetSceneSystem()->GetActiveScene();
			if (scene)
			{
				scene->DestroyEntity(e);
			}
		}, entity);
	}

}
