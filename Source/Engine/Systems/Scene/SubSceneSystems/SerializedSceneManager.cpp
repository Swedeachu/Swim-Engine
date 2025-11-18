#include "PCH.h"
#include "SerializedSceneManager.h"

#include "Engine/SwimEngine.h"
#include "Engine/Components/Transform.h"
#include "Engine/Components/Material.h"
#include "Engine/Components/CompositeMaterial.h"

#include <filesystem>
#include <fstream>

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

			bool hasTag = false;

			// First check if we have a tag and if this should be excluded from being serialized due to being an editor mode object
			if (reg.any_of<ObjectTag>(e))
			{
				ObjectTag& tag = reg.get<ObjectTag>(e);
				if (tag.tag == TagConstants::EDITOR_MODE_OBJECT || tag.tag == TagConstants::EDITOR_MODE_UI)
				{
					continue;
				}
				else
				{
					hasTag = true;
				}
			}

			json jEntity = BuildEntityJSON(e, hasTag);
			entitiesArray.push_back(std::move(jEntity));
		}
	}

	json SerializedSceneManager::BuildEntityJSON(entt::entity e, bool hasTag)
	{
		json jEntity = json::object();

		const std::uint32_t id = static_cast<std::uint32_t>(entt::to_integral(e));
		jEntity["id"] = id;

		// Default parent to null; each component serializer is allowed to overwrite this if it
		// has a notion of parenting (Transform does).
		jEntity["parent"] = nullptr;

		// Per-component serialization (easy to extend)
		SerializeTransform(e, jEntity);
		if (hasTag)
		{
			SerializeTag(e, jEntity);
		}
		SerializeMaterial(e, jEntity);
		// TODO: behaviors, mesh decorators, text components

		return jEntity;
	}

	void SerializedSceneManager::SerializeTag(entt::entity e, json& jEntity)
	{
		ObjectTag& tag = reg.get<ObjectTag>(e);
		json jTag = json::object();
		jTag["name"] = tag.name;
		jTag["tag"] = tag.tag;
		jEntity["objectTag"] = std::move(jTag);
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

		// Use default channel 1 for now
		engine->SendEditorMessage(wide, /*channel*/ 1);
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

}
