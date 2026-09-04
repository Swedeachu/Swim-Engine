#include "PCH.h"
#include "SceneCommandBuffer.h"

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

	void SceneCommandBuffer::CreateWithTransformAndMaterial(const Transform& transform, const Material& material)
	{
		CreateWithTransformAndMaterial(
			transform,
			material,
			[](entt::entity, Transform&, Material&) {}
		);
	}

}
