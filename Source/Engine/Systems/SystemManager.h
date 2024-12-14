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

		template <typename T, typename... Args>
		void AddSystem(const std::string& name, Args&&... args)
		{
			systems[name] = std::make_unique<T>(std::forward<Args>(args)...);
		}

	private:

		std::map<std::string, std::unique_ptr<Machine>> systems;

		int SmartIterate(std::function<int(Machine*)> method);

	};

}
