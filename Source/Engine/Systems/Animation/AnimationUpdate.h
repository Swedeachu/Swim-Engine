#pragma once
#include "Engine/Systems/Animation/Animator.h"
#include "Engine/Systems/Animation/SkeletonInstance.h"

#include <span>

namespace Swim::Jobs
{
	class JobSystem;
}

namespace Swim::Animation
{
	// One animated character: its state machine and the posed skeleton it drives.
	struct AnimatedSkeleton
	{
		Animator* Controller = nullptr;
		SkeletonInstance* Instance = nullptr;
	};

	// Updates every animator by dt, then poses its skeleton instance. With a
	// running job system the work is split across workers (ParallelFor, at least
	// minPerTask characters per task) and this call waits for it; each character
	// touches only its own objects, so the result equals the serial update.
	// Without one (or with a stopped one) it runs serially on the caller.
	void UpdateAnimations(
		std::span<const AnimatedSkeleton> characters, float dt, Jobs::JobSystem* jobs = nullptr, std::size_t minPerTask = 8);
} // namespace Swim::Animation
