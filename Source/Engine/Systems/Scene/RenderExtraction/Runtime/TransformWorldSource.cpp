#include "Engine/Systems/Scene/RenderExtraction/Runtime/TransformWorldSource.h"
#include "Engine/Components/Transform.h"

#include <glm/gtc/type_ptr.hpp>

namespace Engine
{

	RenderWorldTransformSource MakeTransformWorldSource()
	{
		return [](const entt::registry& registry, entt::entity entity)
		{
			const auto* transform = registry.try_get<Transform>(entity);
			if (!transform)
			{
				return Swim::Render::RenderAffine{};
			}
			// glm is column-major, the render affine is row-major 3x4.
			return Swim::Render::RenderAffine::FromColumnMajor(glm::value_ptr(transform->GetWorldMatrix(registry)));
		};
	}

} // namespace Engine
