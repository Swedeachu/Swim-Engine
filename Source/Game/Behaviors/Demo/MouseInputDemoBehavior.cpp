#include "PCH.h"
#include "MouseInputDemoBehavior.h"
#include "Engine/Components/Transform.h"
#include "Engine/Components/DecoratorUI.h"
#include "Engine/Systems/Renderer/Renderer.h"

namespace Game
{

	glm::vec4 toFour(const glm::vec3& vec, float z = 1.0f)
	{
		return glm::vec4(vec.x, vec.y, vec.z, z);
	}

	void MouseInputDemoBehavior::SetColor(Engine::DebugColor color)
	{
		Engine::DecoratorUI& decorator = scene->GetRegistry().get<Engine::DecoratorUI>(entity);
		decorator.fillColor = toFour(Engine::GetDebugColorValue(color));
	}

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
		if (!input->IsKeyDown(VK_LBUTTON))
		{
			SetColor(Engine::DebugColor::Yellow);
		}
	}

	void MouseInputDemoBehavior::OnMouseHover()
	{
		// std::cout << "MouseInputDemoBehavior: Mouse Hover\n"; // commented out just to avoid spam 
	}

	void MouseInputDemoBehavior::OnMouseExit()
	{
		std::cout << "MouseInputDemoBehavior: Mouse Exited\n";
		SetColor(Engine::DebugColor::White);
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
		SetColor(Engine::DebugColor::Green);
	}

	void MouseInputDemoBehavior::OnRightClickDown()
	{
		// std::cout << "MouseInputDemoBehavior: Right Button Down\n"; // commented out just to avoid spam 
	}

	void MouseInputDemoBehavior::OnLeftClickUp()
	{
		std::cout << "MouseInputDemoBehavior: Left Button Up\n";
		SetColor(Engine::DebugColor::White);
	}

	void MouseInputDemoBehavior::OnRightClickUp()
	{
		std::cout << "MouseInputDemoBehavior: Right Button Up\n";
	}

}
