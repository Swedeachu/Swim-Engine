#pragma once

#include <entt/entt.hpp>

#include "Behavior.h"

#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Engine
{

	class Scene;

	class BehaviorRegistry
	{
	public:

		using Factory = std::function<std::unique_ptr<Behavior>(Scene* scene, entt::entity owner)>;
		using Matcher = std::function<bool(const Behavior& behavior)>;

		struct Descriptor
		{
			std::string Name;
			Factory Create;
			Matcher Matches;
		};

		template<typename T>
		void Register(std::string name)
		{
			Register(
				std::move(name),
				[](Scene* scene, entt::entity owner) -> std::unique_ptr<Behavior>
			{
				return std::make_unique<T>(scene, owner);
			},
				[](const Behavior& behavior)
			{
				return dynamic_cast<const T*>(&behavior) != nullptr;
			}
			);
		}

		void Register(std::string name, Factory factory, Matcher matcher = {})
		{
			if (name.empty())
			{
				throw std::invalid_argument("Behavior type name cannot be empty.");
			}
			if (!factory)
			{
				throw std::invalid_argument("Behavior type '" + name + "' has no factory.");
			}
			if (Contains(name))
			{
				throw std::runtime_error("Behavior type '" + name + "' is already registered.");
			}

			descriptors.push_back({ std::move(name), std::move(factory), std::move(matcher) });
		}

		bool Contains(std::string_view name) const
		{
			return Find(name) != nullptr;
		}

		std::unique_ptr<Behavior> Create(std::string_view name, Scene* scene, entt::entity owner) const
		{
			const Descriptor* descriptor = Find(name);
			if (!descriptor)
			{
				return nullptr;
			}

			return descriptor->Create(scene, owner);
		}

		bool Matches(std::string_view name, const Behavior& behavior) const
		{
			const Descriptor* descriptor = Find(name);
			return descriptor && descriptor->Matches && descriptor->Matches(behavior);
		}

		const std::vector<Descriptor>& GetDescriptors() const { return descriptors; }

	private:

		const Descriptor* Find(std::string_view name) const
		{
			for (const Descriptor& descriptor : descriptors)
			{
				if (descriptor.Name == name)
				{
					return &descriptor;
				}
			}

			return nullptr;
		}

		std::vector<Descriptor> descriptors;

	};

}
