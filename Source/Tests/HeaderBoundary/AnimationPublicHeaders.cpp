#include "Engine/Systems/Animation/AnimationClip.h"
#include "Engine/Systems/Animation/AnimationMath.h"
#include "Engine/Systems/Animation/AnimationUpdate.h"
#include "Engine/Systems/Animation/Animator.h"
#include "Engine/Systems/Animation/Skeleton.h"
#include "Engine/Systems/Animation/SkeletonInstance.h"

#include <type_traits>

// The animation runtime's public headers compile on their own: no renderer, scene,
// platform or job-system definitions are needed (JobSystem is only forward-declared).
static_assert(std::is_move_constructible_v<Swim::Animation::Animator>);
static_assert(std::is_move_constructible_v<Swim::Animation::SkeletonInstance>);
static_assert(sizeof(Swim::Animation::Matrix3x4) == 48);
