#include "PCH.h"
#include "ChangeGizmoTypeButtonBehavior.h"
#include "Engine/Components/MeshDecorator.h"

namespace Engine
{

	void SetFillColor(Scene* scene, entt::entity entity, const glm::vec4& color)
	{
		entt::registry& reg = scene->GetRegistry();
		if (reg.any_of<MeshDecorator>(entity))
		{
			MeshDecorator& md = reg.get<MeshDecorator>(entity);
			md.fillColor = color;
		}
	}

	int ChangeGizmoTypeButtonBehavior::Init()
	{
		EnableMouseCallBacks(true);

		return 0;
	}

	void ChangeGizmoTypeButtonBehavior::OnMouseHover()
	{
		if (!active)
		{
			// We only want to be the hover color if we are not active
			SetFillColor(scene, entity, hoverColor);
		}
	}

	void ChangeGizmoTypeButtonBehavior::OnMouseExit()
	{
		if (!active)
		{
			// If mouse not hovering and this button isn't active, go back to normal color
			SetFillColor(scene, entity, regularColor);
		}
	}

	void ChangeGizmoTypeButtonBehavior::OnLeftClicked()
	{
		active = !active;
		if (active)
		{
			SetFillColor(scene, entity, activeColor);
		}

		GizmoSystem* gs = scene->GetGizmoSystem();
		if (gs)
		{
			if (!active)
			{
				gs->SetGizmoType(GizmoType::Inactive);
				return;
			}

			gs->SetGizmoType(type);
			entt::registry& reg = scene->GetRegistry();

			// This is so fucking bad, we need a proper messaging framework
			reg.view<BehaviorComponents>().each(
				[&](entt::entity entity,
				BehaviorComponents& bc)
			{
				for (std::unique_ptr<Behavior>& behavior : bc.behaviors)
				{
					Behavior* beh = behavior.get();
					ChangeGizmoTypeButtonBehavior* b = dynamic_cast<ChangeGizmoTypeButtonBehavior*>(beh);
					if (b)
					{
						b->DeactiveIfNotThis(this);
					}
				}
			});
		}
	}

	void ChangeGizmoTypeButtonBehavior::DeactiveIfNotThis(ChangeGizmoTypeButtonBehavior* s)
	{
		if (s != this)
		{
			active = false;
			// Immediately restore visual state
			SetFillColor(scene, entity, regularColor);
		}
	}

	void ChangeGizmoTypeButtonBehavior::Activate()
	{
		active = false; // method call below flips this value, so this actually gurantees it will be true
		OnLeftClicked(); // reuse logic of getting clicked
	}

}