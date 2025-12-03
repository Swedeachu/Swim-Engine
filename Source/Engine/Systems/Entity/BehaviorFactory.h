#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

#include "entt/entt.hpp"

namespace Engine
{

	class Behavior;
	class Scene;

	class BehaviorFactory
	{

	public:

		using FactoryFunc = std::function<std::unique_ptr<Behavior>(Scene* scene, entt::entity owner)>;

		static BehaviorFactory& GetInstance()
		{
			static BehaviorFactory instance;
			return instance;
		}

		template<typename T>
		void Register(const std::string& name)
		{
			factories[name] = [](Scene* scene, entt::entity owner) -> std::unique_ptr<Behavior>
			{
				return std::make_unique<T>(scene, owner);
			};
		}

		// Creates a new Behavior instance by name (not attached to any entity yet)
		std::unique_ptr<Behavior> Create(const std::string& name, Scene* scene, entt::entity owner) const
		{
			auto it = factories.find(name);
			if (it == factories.end())
			{
				return nullptr;
			}

			return it->second(scene, owner);
		}

		bool Exists(const std::string& name) const
		{
			return factories.find(name) != factories.end();
		}

		const std::unordered_map<std::string, FactoryFunc>& GetFactories() const
		{
			return factories;
		}

	private:

		std::unordered_map<std::string, FactoryFunc> factories;

	};

} // namespace Engine
