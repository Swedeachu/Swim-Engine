#pragma once

#include <functional>

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
			systems[name] = system; 
			return system;          
		}

	private:

		std::map<std::string, std::shared_ptr<Machine>> systems;

		int SmartIterate(std::function<int(Machine*)> method);

	};

}
