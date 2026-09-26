#include "Engine/Components/Tags.h"

#include <stdexcept>

namespace Engine
{
	TagRegistry::TagRegistry()
	{
		for (const std::string_view name : { "World", "Static", "Dynamic", "Physics", "Light", "Camera", "Player", "Projectile", "Trigger",
				 "Effect", "Environment", "Ui", "Spawned", "Selectable" })
		{
			Register(name);
		}
	}

	TagId TagRegistry::Register(std::string_view name)
	{
		if (name.empty())
		{
			throw std::invalid_argument("Tag names must not be empty");
		}
		const TagId tag = MakeTag(name);
		const auto [it, inserted] = names.emplace(tag.Value, std::string(name));
		if (!inserted && it->second != name)
		{
			throw std::logic_error("Tag '" + std::string(name) + "' collides with '" + it->second + "'");
		}
		return tag;
	}

	std::string_view TagRegistry::GetName(TagId tag) const
	{
		const auto it = names.find(tag.Value);
		return it == names.end() ? std::string_view{} : std::string_view(it->second);
	}

	bool TagRegistry::IsRegistered(TagId tag) const
	{
		return names.contains(tag.Value);
	}
} // namespace Engine
