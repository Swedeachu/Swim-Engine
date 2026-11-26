#pragma once

#include "Engine/Systems/Replay/ReplayTypes.h"

#include <entt/entity/entity.hpp>

#include <string>
#include <utility>
#include <vector>

namespace Engine
{

	// Records this entity as its own replay node (Systems/Replay/Runtime/SceneReplaySource):
	// kind Kind, the entity's world pose and these properties. Mesh parts of descendants
	// carry an "owner" property with this node's id, so tools can find them together;
	// Members (and their descendants) are treated the same way for unparented parts.
	struct ReplayAnnotation
	{
		std::string Kind = "group";
		std::vector<std::pair<std::string, Swim::Replay::Value>> Props;
		std::vector<entt::entity> Members;
	};

	// Keeps this entity and its descendants out of scene recordings (first-person overlays,
	// preview renders, menu backdrops).
	struct ReplayIgnore
	{
	};

	// Records this entity's mesh parts as visible even when its renderer hides them live
	// (a first-person body that the main view must not draw).
	struct ReplayShowHidden
	{
	};

} // namespace Engine
