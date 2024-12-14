#pragma once

namespace Engine
{

	class Machine
	{

	public:

		// called on construction
		// returns an int for success code
		virtual int Awake() { return 0; };

		// called on adding to the scene
		// returns an int for success code
		virtual int Init() { return 0; };

		// called once every frame, potentially hundreds of times a second
		virtual void Update(double dt) {};

		// called every engine tick, in line with AI and Physics updates
		// this will be a fixed rate, such as 20-60 ticks a second
		// The param says what tick of the second it is 
		virtual void FixedUpdate(unsigned int tickThisSecond) {};

		// called when destroyed
		// returns an int for success code
		virtual int Exit() { return 0;  };

	};

}