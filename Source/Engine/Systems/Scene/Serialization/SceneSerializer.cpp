#include "PCH.h"
#include "SceneSerializer.h"

#include "Engine/Components/CompositeMaterial.h"
#include "Engine/Components/DoNotSerialize.h"
#include "Engine/Components/Material.h"
#include "Engine/Components/ObjectTag.h"
#include "Engine/Components/Transform.h"

namespace Engine
{

	using nlohmann::json;

	nlohmann::json SceneSerializer::SerializeScene() const
	{
		json root = json::object();
		root["schemaVersion"] = SchemaVersion;
		root["scene"] = sceneName;
		root["entities"] = json::array();

		for (entt::entity entity : registry->storage<entt::entity>())
		{
			if (!registry->valid(entity) || !ShouldSerialize(entity))
			{
				continue;
			}

			json serialized = SerializeEntity(entity);
			if (!serialized.is_null())
			{
				root["entities"].push_back(std::move(serialized));
			}
		}

		return root;
	}

	nlohmann::json SceneSerializer::SerializeEntity(entt::entity entity) const
	{
		if (!registry->valid(entity) || !ShouldSerialize(entity))
		{
			return nullptr;
		}

		const SerializedEntityId id = FindId(entity);
		if (!id)
		{
			return nullptr;
		}

		json jsonEntity = json::object();
		jsonEntity["id"] = id.Value;
		jsonEntity["parent"] = nullptr;

		SerializeTransform(entity, jsonEntity);
		SerializeRenderable(entity, jsonEntity);
		SerializeTag(entity, jsonEntity);

		return jsonEntity;
	}

	void SceneSerializer::SerializeTransform(entt::entity entity, nlohmann::json& jsonEntity) const
	{
		if (!registry->any_of<Transform>(entity))
		{
			jsonEntity["transform"] = nullptr;
			return;
		}

		const Transform& transform = registry->get<Transform>(entity);
		const glm::vec3 position = transform.GetPosition();
		const glm::vec3 scale = transform.GetScale();
		const glm::vec3 rotationEuler = transform.GetRotationEuler();

		json jsonTransform = json::object();
		jsonTransform["position"] = {
			{ "x", position.x },
			{ "y", position.y },
			{ "z", position.z }
		};
		jsonTransform["scale"] = {
			{ "x", scale.x },
			{ "y", scale.y },
			{ "z", scale.z }
		};
		jsonTransform["rotationEuler"] = {
			{ "x", rotationEuler.x },
			{ "y", rotationEuler.y },
			{ "z", rotationEuler.z }
		};
		jsonTransform["space"] = static_cast<int>(transform.GetTransformSpace());
		jsonEntity["transform"] = std::move(jsonTransform);

		if (!transform.HasParent())
		{
			return;
		}

		const entt::entity parent = transform.GetParent();
		if (parent == entt::null || !registry->valid(parent))
		{
			return;
		}

		const SerializedEntityId parentId = FindId(parent);
		if (parentId)
		{
			jsonEntity["parent"] = parentId.Value;
		}
	}

	void SceneSerializer::SerializeRenderable(entt::entity entity, nlohmann::json& jsonEntity) const
	{
		if (registry->any_of<CompositeMaterial>(entity))
		{
			const CompositeMaterial& material = registry->get<CompositeMaterial>(entity);
			json renderable = json::object();
			renderable["kind"] = "model";
			renderable["modelAssetId"] = material.ModelAssetId ? json(material.ModelAssetId.Value) : json(nullptr);
			if (!material.filePath.empty())
			{
				renderable["source"] = material.filePath;
			}
			jsonEntity["renderable"] = std::move(renderable);
			return;
		}

		if (registry->any_of<Material>(entity))
		{
			const Material& material = registry->get<Material>(entity);
			json renderable = json::object();
			renderable["kind"] = "meshMaterial";

			if (material.binding)
			{
				renderable["meshAssetId"] = material.binding->MeshAssetId
					? json(material.binding->MeshAssetId.Value)
					: json(nullptr);
				renderable["materialAssetId"] = material.binding->MaterialAssetId
					? json(material.binding->MaterialAssetId.Value)
					: json(nullptr);
			}
			else
			{
				renderable["meshAssetId"] = nullptr;
				renderable["materialAssetId"] = nullptr;
			}

			jsonEntity["renderable"] = std::move(renderable);
		}
	}

	void SceneSerializer::SerializeTag(entt::entity entity, nlohmann::json& jsonEntity) const
	{
		if (!registry->any_of<ObjectTag>(entity))
		{
			return;
		}

		const ObjectTag& tag = registry->get<ObjectTag>(entity);
		json jsonTag = json::object();
		jsonTag["name"] = tag.name;
		jsonTag["tag"] = tag.tag;
		jsonEntity["objectTag"] = std::move(jsonTag);
	}

	bool SceneSerializer::ShouldSerialize(entt::entity entity) const
	{
		if (!registry->valid(entity))
		{
			return false;
		}

		if (registry->any_of<DoNotSerialize>(entity))
		{
			return false;
		}

		if (registry->any_of<ObjectTag>(entity))
		{
			const ObjectTag& tag = registry->get<ObjectTag>(entity);
			if (tag.tag == TagConstants::EDITOR_MODE_OBJECT || tag.tag == TagConstants::EDITOR_MODE_UI)
			{
				return false;
			}
		}

		return FindId(entity).IsValid();
	}

	entt::entity SceneSerializer::FindEntity(SerializedEntityId id) const
	{
		const auto entity = identities->FindEntity(id);
		if (!entity || !registry->valid(*entity))
		{
			return entt::null;
		}
		return *entity;
	}

	SerializedEntityId SceneSerializer::FindId(entt::entity entity) const
	{
		const auto id = identities->FindId(entity);
		return id ? *id : SerializedEntityId{};
	}

}
