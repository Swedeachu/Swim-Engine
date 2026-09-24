#include "Engine/Systems/Animation/AnimationUpdate.h"

#include "Engine/Jobs/JobSystem.h"

#include <algorithm>

namespace Swim::Animation
{
	namespace
	{
		void UpdateOne(const AnimatedSkeleton& character, float dt)
		{
			if (character.Controller)
			{
				character.Controller->Update(dt);
				if (character.Instance)
				{
					character.Instance->Update(character.Controller->GetPose());
				}
			}
		}
	} // namespace

	void UpdateAnimations(std::span<const AnimatedSkeleton> characters, float dt, Jobs::JobSystem* jobs, std::size_t minPerTask)
	{
		if (!jobs || !jobs->IsRunning() || characters.size() <= 1)
		{
			for (const AnimatedSkeleton& character : characters)
			{
				UpdateOne(character, dt);
			}
			return;
		}
		jobs->ParallelFor(characters.size(), std::max<std::size_t>(minPerTask, 1),
			[characters, dt](std::size_t begin, std::size_t end, std::uint32_t)
			{
				for (std::size_t index = begin; index < end; ++index)
				{
					UpdateOne(characters[index], dt);
				}
			});
	}
} // namespace Swim::Animation
