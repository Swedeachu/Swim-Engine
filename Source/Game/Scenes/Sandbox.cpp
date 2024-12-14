#include "PCH.h"
#include "SandBox.h"

namespace Game
{

	int SandBox::Awake()
	{
		std::cout << name << " Awoke" << std::endl;
		return 0;
	}

	int SandBox::Init()
	{
		std::cout << name << " Init" << std::endl;
		return 0;
	}

	void SandBox::Update(double dt)
	{
		std::cout << name << " Update: " << dt << std::endl;
	}

	void SandBox::FixedUpdate(unsigned int tickThisSecond)
	{
		std::cout << name << " Fixed Update: " << tickThisSecond << std::endl;
	}

	int SandBox::Exit()
	{
		std::cout << name << " Exiting" << std::endl;

		return 0;
	}

}
