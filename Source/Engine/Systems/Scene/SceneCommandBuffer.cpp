#include "SceneCommandBuffer.h"

#include <stdexcept>

namespace Engine
{

	std::size_t SceneCommandBuffer::Flush()
	{
		if (!scene)
		{
			throw std::runtime_error("SceneCommandBuffer: owning scene is unavailable.");
		}

		return commands.Flush(*scene);
	}

	void SceneCommandBuffer::Clear()
	{
		commands.Clear();
	}

	void SceneCommandBuffer::Create()
	{
		Create([](Scene&, entt::entity) {});
	}

	void SceneCommandBuffer::Destroy(entt::entity entity, bool callExit, bool destroyChildren)
	{
		Defer(
			[entity, callExit, destroyChildren](Scene& owningScene)
		{
			owningScene.DestroyEntity(entity, callExit, destroyChildren);
		}
		);
	}

	void SceneCommandBuffer::CreateWithTransform(const Transform& transform)
	{
		Create(
			[transform](Scene& owningScene, entt::entity entity)
		{
			owningScene.AddComponent<Transform>(entity, transform);
		}
		);
	}

	void SceneCommandBuffer::AddTag(entt::entity entity, TagId tag)
	{
		Defer([entity, tag](Scene& owningScene)
		{
			if (owningScene.IsValid(entity))
			{
				owningScene.AddTag(entity, tag);
			}
		});
	}

	void SceneCommandBuffer::RemoveTag(entt::entity entity, TagId tag)
	{
		Defer([entity, tag](Scene& owningScene)
		{
			if (owningScene.IsValid(entity))
			{
				owningScene.RemoveTag(entity, tag);
			}
		});
	}

	void SceneCommandBuffer::SetName(entt::entity entity, std::string name)
	{
		Defer([entity, name = std::move(name)](Scene& owningScene)
		{
			if (owningScene.IsValid(entity))
			{
				owningScene.SetEntityName(entity, name);
			}
		});
	}

}
