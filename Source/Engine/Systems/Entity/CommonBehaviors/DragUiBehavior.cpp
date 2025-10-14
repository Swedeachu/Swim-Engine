#include "PCH.h"
#include "DragUiBehavior.h"
#include "Engine/Components/Transform.h"

namespace Engine
{

	int DragUiBehavior::Init()
	{
		EnableMouseCallBacks(true);
		return 0;
	}

	void DragUiBehavior::OnLeftClickDown()
	{
		isDragging = true;

		if (transform)
		{
			const glm::vec2 mouse = input->GetMousePosition(true);
			const glm::vec3 pos = transform->GetPosition();

			// Record where we clicked and the offset between object and mouse
			dragStartMouse = mouse;
			dragStartPos = pos;
			grabOffset = glm::vec2(pos.x, pos.y) - mouse;
		}
	}

	void DragUiBehavior::Update(double dt)
	{
		if (!isDragging || !transform)
		{
			return;
		}

		// UI (0, 0) is bottom-left corner
		// Use the virtual canvas extents as our clamp region so the element
		// cannot be dragged past the visible window space.
		const float canvasW = static_cast<float>(Renderer::VirtualCanvasWidth);
		const float canvasH = static_cast<float>(Renderer::VirtualCanvasHeight);

		const glm::vec2 mouse = input->GetMousePosition(true);
		const glm::vec2 target = mouse + grabOffset;

		glm::vec3& tfPos = transform->GetPositionRef();

		// Clamp to [0, canvas] so we cannot drag past the window's dimensions.
		// If the element has its own width/height and you want to keep the
		// entire rect inside, subtract those from canvasW/canvasH here.
		tfPos.x = std::clamp(target.x, 0.0f, canvasW);
		tfPos.y = std::clamp(target.y, 0.0f, canvasH);
	}

	void DragUiBehavior::OnLeftClickUp()
	{
		isDragging = false;
	}

}
