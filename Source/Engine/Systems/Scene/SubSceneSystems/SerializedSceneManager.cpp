#include "PCH.h"
#include "SerializedSceneManager.h"

#include "Engine/SwimEngine.h"
#include "Engine/Components/Transform.h"
#include "Engine/Components/Material.h"
#include "Engine/Components/CompositeMaterial.h"
#include "Engine/Components/ObjectTag.h"

#include <filesystem>
#include <fstream>
#include <algorithm>

using nlohmann::json;

namespace Engine
{

	SerializedSceneManager::SerializedSceneManager(entt::registry& reg, const std::string& sceneName)
		: reg(reg), sceneName(sceneName)
	{}

	std::wstring SerializedSceneManager::Utf8ToWide(const std::string& utf8)
	{
		std::wstring wide;
		wide.reserve(utf8.size());

		for (unsigned char c : utf8)
		{
			wide.push_back(static_cast<wchar_t>(c));
		}

		return wide;
	}

	void SerializedSceneManager::BuildFullJSON()
	{
		jsonRoot = json::object();
		jsonRoot["scene"] = sceneName;
		jsonRoot["entities"] = json::array();

		auto& entitiesArray = jsonRoot["entities"];

		// Walk all entities in the registry
		for (entt::entity e : reg.view<entt::entity>())
		{
			if (!reg.valid(e))
			{
				continue;
			}

			if (!ShouldSerialize(e))
			{
				continue;
			}

			json jEntity = BuildEntityJSON(e);
			entitiesArray.push_back(std::move(jEntity));
		}
	}

	json SerializedSceneManager::BuildEntityJSON(entt::entity e)
	{
		json jEntity = json::object();

		const std::uint32_t id = static_cast<std::uint32_t>(entt::to_integral(e));
		jEntity["id"] = id;

		// Default parent to null; each component serializer is allowed to overwrite this if it
		// has a notion of parenting (Transform does).
		jEntity["parent"] = nullptr;

		// Per-component serialization (easy to extend)
		SerializeTransform(e, jEntity);
		SerializeMaterial(e, jEntity);
		SerializeTag(e, jEntity);
		// TODO: behaviors, mesh decorators, text components

		return jEntity;
	}

	void SerializedSceneManager::SerializeTag(entt::entity e, json& jEntity)
	{
		if (reg.any_of<ObjectTag>(e))
		{
			ObjectTag& tag = reg.get<ObjectTag>(e);
			json jTag = json::object();
			jTag["name"] = tag.name;
			jTag["tag"] = tag.tag;
			jEntity["objectTag"] = std::move(jTag);
		}
	}

	void SerializedSceneManager::SerializeTransform(entt::entity e, json& jEntity)
	{
		// If the entity has a Transform, serialize it and parent info
		if (reg.any_of<Transform>(e))
		{
			Transform& tf = reg.get<Transform>(e);

			json jTransform = json::object();

			const glm::vec3 pos = tf.GetPosition();
			const glm::vec3 scl = tf.GetScale();
			const glm::vec3 rotDeg = tf.GetRotationEuler();

			jTransform["position"] = {
				{ "x", pos.x },
				{ "y", pos.y },
				{ "z", pos.z }
			};

			jTransform["scale"] = {
				{ "x", scl.x },
				{ "y", scl.y },
				{ "z", scl.z }
			};

			jTransform["rotationEuler"] = {
				{ "x", rotDeg.x },
				{ "y", rotDeg.y },
				{ "z", rotDeg.z }
			};

			jTransform["space"] = static_cast<int>(tf.GetTransformSpace());

			jEntity["transform"] = std::move(jTransform);

			// Parent info (via Transform)
			if (tf.HasParent())
			{
				entt::entity parent = tf.GetParent();
				if (parent != entt::null && reg.valid(parent))
				{
					const std::uint32_t parentId = static_cast<std::uint32_t>(entt::to_integral(parent));
					jEntity["parent"] = parentId;
				}
				else
				{
					jEntity["parent"] = nullptr;
				}
			}
			else
			{
				jEntity["parent"] = nullptr;
			}
		}
		else
		{
			// Entity has no Transform — keep it explicit and stable.
			jEntity["transform"] = nullptr;
			// parent is already set to nullptr in BuildEntityJSON anyways
		}
	}

	void SerializedSceneManager::SerializeMaterial(entt::entity e, nlohmann::json& jEntity)
	{
		// These need to be a relative paths, for example: "Assets/Textures/image.png", NOT "C:/Users/User/source/repos/Engine/Assets/Textures/image.png"
		// I have noticed that some of these path seperators are sometimes not correct, but it might not matter: "/" vs "\\" vs "\"
		std::string albedoTextureAssetFilePath = "";
		std::string modelFilePath = "";

		if (reg.any_of<Material>(e))
		{
			Material& mat = reg.get<Material>(e);
			if (mat.data->albedoMap)
			{
				albedoTextureAssetFilePath = mat.data->albedoMap->GetFilePath();
			}
		}

		if (reg.any_of<CompositeMaterial>(e))
		{
			CompositeMaterial& mat = reg.get<CompositeMaterial>(e);
			modelFilePath = mat.filePath;
		}

		if (albedoTextureAssetFilePath != "" || modelFilePath != "")
		{
			json jMaterial = json::object();
			jMaterial["albedoTextureFilePath"] = albedoTextureAssetFilePath; // if blank then has no albedo texture
			jMaterial["modelFilePath"] = modelFilePath; // if blank then no model, so not a composite mesh to load from file (behavior or scene code sets its mesh dynamically)
			jEntity["material"] = std::move(jMaterial);
		}
	}

	void SerializedSceneManager::SendFullJSON()
	{
		// Build JSON from current registry
		BuildFullJSON();

		// Dump as compact UTF-8 JSON string
		const std::string utf8 = jsonRoot.dump(); // pass 2 for pretty-print: dump(2)

		// Convert to wide string for WM_COPYDATA
		const std::wstring wide = Utf8ToWide("scene load:" + utf8); // scene load command for editor to parse

		// Send to editor through the engine
		auto engine = SwimEngine::GetInstance();
		if (!engine)
		{
			return;
		}

		engine->SendEditorMessage(wide, /*channel*/ 2);
	}

	void SerializedSceneManager::SaveFullJSON()
	{
		// Reuse the same JSON-building logic as SendFullJSON
		BuildFullJSON();

		// Pretty-print to make it nicer to read on disk
		const std::string utf8 = jsonRoot.dump(2);

		namespace fs = std::filesystem;

		// Base directory: next to the executable, in a "Scenes" folder
		const std::string exeDir = SwimEngine::GetExecutableDirectory();
		fs::path scenesDir = fs::path(exeDir) / "Scenes";

		// Create the directory (and parents) if it doesn't exist
		std::error_code ec;
		fs::create_directories(scenesDir, ec); // ignore errors silently for now

		// Build file path: Scenes/<sceneName>.json
		fs::path filePath = scenesDir / sceneName;
		filePath.replace_extension(".json");

		// Write JSON to file
		std::ofstream out(filePath, std::ios::binary | std::ios::trunc);
		if (!out.is_open())
		{
			// TODO: hook into engine logging if you want
			return;
		}

		out << utf8;
		out.close();
	}

	void SerializedSceneManager::EnqueueCreated(entt::entity e)
	{
		// If it was previously marked destroyed this frame, undo that.
		auto itD = std::find(destroyedEntities.begin(), destroyedEntities.end(), e);
		if (itD != destroyedEntities.end())
		{
			destroyedEntities.erase(itD);
		}

		// Newly created entity's full JSON covers all state; no need to track as updated.
		auto itU = std::find(updatedEntities.begin(), updatedEntities.end(), e);
		if (itU != updatedEntities.end())
		{
			updatedEntities.erase(itU);
		}

		// Avoid duplicate entries.
		auto itC = std::find(createdEntities.begin(), createdEntities.end(), e);
		if (itC == createdEntities.end())
		{
			createdEntities.push_back(e);
		}
	}

	void SerializedSceneManager::EnqueueUpdated(entt::entity e)
	{
		// If the entity was created this frame, the create JSON will already contain latest state.
		auto itC = std::find(createdEntities.begin(), createdEntities.end(), e);
		if (itC != createdEntities.end())
		{
			return;
		}

		// If it's destroyed this frame, do not bother tracking updates.
		auto itD = std::find(destroyedEntities.begin(), destroyedEntities.end(), e);
		if (itD != destroyedEntities.end())
		{
			return;
		}

		// Avoid duplicate entries.
		auto itU = std::find(updatedEntities.begin(), updatedEntities.end(), e);
		if (itU == updatedEntities.end())
		{
			updatedEntities.push_back(e);
		}
	}

	void SerializedSceneManager::EnqueueDestroyed(entt::entity e)
	{
		// If it was created this frame, then created+destroyed cancels out.
		// Net effect: the editor never needs to know about this entity at all.
		auto itC = std::find(createdEntities.begin(), createdEntities.end(), e);
		if (itC != createdEntities.end())
		{
			createdEntities.erase(itC);
			return; // do NOT add to destroyedEntities
		}

		// Any pending updates are irrelevant if it is destroyed.
		auto itU = std::find(updatedEntities.begin(), updatedEntities.end(), e);
		if (itU != updatedEntities.end())
		{
			updatedEntities.erase(itU);
		}

		// Avoid duplicate entries.
		auto itD = std::find(destroyedEntities.begin(), destroyedEntities.end(), e);
		if (itD == destroyedEntities.end())
		{
			destroyedEntities.push_back(e);
		}
	}

	void SerializedSceneManager::SendEntityCreated(entt::entity e)
	{
		if (!reg.valid(e))
		{
			return;
		}

		if (!ShouldSerialize(e))
		{
			return;
		}

		EnqueueCreated(e);
	}

	void SerializedSceneManager::SendEntityDestroyed(entt::entity e)
	{
		if (!reg.valid(e))
		{
			return;
		}

		if (!ShouldSerialize(e))
		{
			return;
		}

		EnqueueDestroyed(e);
	}

	void SerializedSceneManager::SendEntityUpdated(entt::entity e)
	{
		if (!reg.valid(e))
		{
			return;
		}

		if (!ShouldSerialize(e))
		{
			return;
		}

		EnqueueUpdated(e);
	}

	void SerializedSceneManager::SendSync()
	{
		// Nothing changed this frame.
		if (createdEntities.empty() && updatedEntities.empty() && destroyedEntities.empty())
		{
			return;
		}

		json syncRoot = json::object();
		syncRoot["scene"] = sceneName;
		syncRoot["created"] = json::array();
		syncRoot["updated"] = json::array();
		syncRoot["destroyed"] = json::array();

		auto& createdArray = syncRoot["created"];
		auto& updatedArray = syncRoot["updated"];
		auto& destroyedArray = syncRoot["destroyed"];

		// Serialize created entities
		for (entt::entity e : createdEntities)
		{
			if (!reg.valid(e))
			{
				continue;
			}

			if (!ShouldSerialize(e))
			{
				continue;
			}

			json jEntity = BuildEntityJSON(e);
			createdArray.push_back(std::move(jEntity));
		}

		// Serialize updated entities
		for (entt::entity e : updatedEntities)
		{
			if (!reg.valid(e))
			{
				continue;
			}

			if (!ShouldSerialize(e))
			{
				continue;
			}

			json jEntity = BuildEntityJSON(e);
			updatedArray.push_back(std::move(jEntity));
		}

		// Serialize destroyed entities (IDs only; entity may no longer be valid in registry).
		for (entt::entity e : destroyedEntities)
		{
			const std::uint32_t id = static_cast<std::uint32_t>(entt::to_integral(e));
			json j = json::object();
			j["id"] = id;
			destroyedArray.push_back(std::move(j));
		}

		// Dump as compact UTF-8 JSON string
		const std::string utf8 = syncRoot.dump();

		// Convert to wide string for WM_COPYDATA
		const std::wstring wide = Utf8ToWide("scene sync:" + utf8);

		auto engine = SwimEngine::GetInstance();
		if (!engine)
		{
			return;
		}

		engine->SendEditorMessage(wide, /*channel*/ 2);

		// Clear per-frame queues
		createdEntities.clear();
		updatedEntities.clear();
		destroyedEntities.clear();
	}

	// Skip editor tagged objects that should not appear in the scene hierarchy view
	const bool SerializedSceneManager::ShouldSerialize(entt::entity e) const
	{
		if (reg.any_of<ObjectTag>(e))
		{
			ObjectTag& tag = reg.get<ObjectTag>(e);
			if (tag.tag == TagConstants::EDITOR_MODE_OBJECT || tag.tag == TagConstants::EDITOR_MODE_UI)
			{
				return false;
			}
		}

		return true;
	}

}
