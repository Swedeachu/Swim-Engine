#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Engine
{
	// A hashed tag name (FNV-1a, 32-bit). Tags are compared by value; the names live in
	// a TagRegistry for display. MakeTag is constexpr, so tags can be constants.
	struct TagId
	{
		std::uint32_t Value = 0;

		explicit operator bool() const { return Value != 0; }

		constexpr bool operator==(const TagId&) const = default;
	};

	constexpr TagId MakeTag(std::string_view name)
	{
		std::uint32_t hash = 2166136261u;
		for (const char c : name)
		{
			hash ^= static_cast<std::uint8_t>(c);
			hash *= 16777619u;
		}
		return TagId{ hash == 0 ? 1u : hash };
	}

	// Engine-wide tags (games define their own with MakeTag or TagRegistry::Register).
	namespace Tags
	{
		inline constexpr TagId World = MakeTag("World");
		inline constexpr TagId Static = MakeTag("Static");
		inline constexpr TagId Dynamic = MakeTag("Dynamic");
		inline constexpr TagId Physics = MakeTag("Physics");
		inline constexpr TagId Light = MakeTag("Light");
		inline constexpr TagId Camera = MakeTag("Camera");
		inline constexpr TagId Player = MakeTag("Player");
		inline constexpr TagId Projectile = MakeTag("Projectile");
		inline constexpr TagId Trigger = MakeTag("Trigger");
		inline constexpr TagId Effect = MakeTag("Effect");
		inline constexpr TagId Environment = MakeTag("Environment");
		inline constexpr TagId Ui = MakeTag("Ui");
		inline constexpr TagId Spawned = MakeTag("Spawned"); // Created at runtime (reset removes it).
		inline constexpr TagId Selectable = MakeTag("Selectable");
	} // namespace Tags

	// Display names of tags. Registering the same name twice returns the same id; a
	// different name hashing to an existing id throws std::logic_error (rename one).
	class TagRegistry
	{
	  public:
		TagRegistry();

		TagId Register(std::string_view name);
		// Empty for unregistered ids.
		std::string_view GetName(TagId tag) const;
		bool IsRegistered(TagId tag) const;

		std::size_t GetCount() const { return names.size(); }

	  private:
		std::unordered_map<std::uint32_t, std::string> names;
	};

	// The tags of one entity (a small, unordered set). Change it through Scene (AddTag,
	// RemoveTag) so the scene's tag index stays current.
	struct TagSet
	{
		std::vector<TagId> Values;

		bool Has(TagId tag) const { return std::find(Values.begin(), Values.end(), tag) != Values.end(); }

		bool Add(TagId tag)
		{
			if (!tag || Has(tag))
			{
				return false;
			}
			Values.push_back(tag);
			return true;
		}

		bool Remove(TagId tag)
		{
			const auto it = std::find(Values.begin(), Values.end(), tag);
			if (it == Values.end())
			{
				return false;
			}
			*it = Values.back();
			Values.pop_back();
			return true;
		}
	};

	// A display name for tools, diagnostics and gameplay lookups (Scene::FindByName).
	struct EntityName
	{
		std::string Value;
	};
} // namespace Engine
