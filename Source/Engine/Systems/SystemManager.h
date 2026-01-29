#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Engine
{

	class SystemManager : public Machine
	{

	public:

		int Awake() override;

		int Init() override;

		void Update(double dt) override;

		void FixedUpdate(unsigned int tickThisSecond) override;

		int Exit() override;

		// Creates the system and stores it in the map and then returns it
		template <typename T, typename... Args>
		std::shared_ptr<T> AddSystem(const std::string& name, Args&&... args)
		{
			auto system = std::make_shared<T>(std::forward<Args>(args)...);

			auto it = systemIndex.find(name);
			if (it == systemIndex.end())
			{
				systemIndex[name] = orderedSystems.size();
				orderedSystems.push_back({ name, system });
			}
			else
			{
				// Replace existing system instance but preserve original insertion order
				orderedSystems[it->second].second = system;
			}

			systems[name] = system;
			return system;
		}

	private:

		// Lookup by name if needed later
		std::unordered_map<std::string, std::shared_ptr<Machine>> systems;

		// Iteration order is insertion order 
		std::vector<std::pair<std::string, std::shared_ptr<Machine>>> orderedSystems;

		// Name -> index into orderedSystems
		std::unordered_map<std::string, std::size_t> systemIndex;

		int SmartIterate(std::function<int(Machine*)> method);

	};

} // Namespace Engine
