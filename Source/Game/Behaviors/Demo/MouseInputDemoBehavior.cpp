#include "PCH.h"
#include "MouseInputDemoBehavior.h"
#include "Engine/Components/Transform.h"
#include "Engine/Systems/Renderer/Renderer.h"

namespace Game
{

	int MouseInputDemoBehavior::Awake()
	{
		std::cout << "MouseInputDemoBehavior: Awake\n";
		return 0;
	}

	int MouseInputDemoBehavior::Init()
	{
		std::cout << "MouseInputDemoBehavior: Init\n";

		// You must enable mouse callbacks explicitly.
		EnableMouseCallBacks(true);

		return 0;
	}

	void MouseInputDemoBehavior::Update(double dt)
	{

	}

	void MouseInputDemoBehavior::FixedUpdate(unsigned int tickThisSecond)
	{

	}

	int MouseInputDemoBehavior::Exit()
	{
		std::cout << "MouseInputDemoBehavior: Exit\n";
		return 0;
	}

	void MouseInputDemoBehavior::OnMouseEnter()
	{
		std::cout << "MouseInputDemoBehavior: Mouse Entered\n";
	}

	void MouseInputDemoBehavior::OnMouseHover()
	{
		// std::cout << "MouseInputDemoBehavior: Mouse Hover\n"; // commented out just to avoid spam 
	}

	void MouseInputDemoBehavior::OnMouseExit()
	{
		std::cout << "MouseInputDemoBehavior: Mouse Exited\n";
	}

	void MouseInputDemoBehavior::OnLeftClicked()
	{
		std::cout << "MouseInputDemoBehavior: Left Clicked\n";
	}

	void MouseInputDemoBehavior::OnRightClicked()
	{
		std::cout << "MouseInputDemoBehavior: Right Clicked\n";
	}

	void MouseInputDemoBehavior::OnLeftClickDown()
	{
		// std::cout << "MouseInputDemoBehavior: Left Button Down\n"; // commented out just to avoid spam 
	}

	void MouseInputDemoBehavior::OnRightClickDown()
	{
		// std::cout << "MouseInputDemoBehavior: Right Button Down\n"; // commented out just to avoid spam 
	}

	void MouseInputDemoBehavior::OnLeftClickUp()
	{
		std::cout << "MouseInputDemoBehavior: Left Button Up\n";
	}

	void MouseInputDemoBehavior::OnRightClickUp()
	{
		std::cout << "MouseInputDemoBehavior: Right Button Up\n";
	}

}
