#pragma once

namespace Engine
{

	class Scene : public Machine
	{

	public:

		Scene() : name("UnnamedScene") {} // Default constructor

		// takes param
		explicit Scene(const std::string& name = "scene")
			: name(name)
		{}

		int Awake() override { return 0; };

		int Init() override { return 0; };

		void Update(double dt) override {};

		void FixedUpdate(unsigned int tickThisSecond) override {};

		int Exit() override { return 0; };

		const std::string& GetName() const { return name; }

	private:

		// TODO: make this work with EnTT as each scene has an entity list to update 
		// TODO: Entity registry to a scene (scene will have add method)

	protected:

		std::string name;

	};

}