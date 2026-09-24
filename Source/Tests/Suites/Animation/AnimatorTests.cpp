#include "Engine/Jobs/JobSystem.h"
#include "Engine/Systems/Animation/AnimationUpdate.h"
#include "Engine/Systems/Animation/Animator.h"
#include "Tests/Fixtures/AnimationFixture.h"
#include "Tests/Framework/Test.h"

#include <cmath>
#include <stdexcept>
#include <vector>

using namespace Swim::Animation;
using Swim::Testing::AngleBetween;

namespace
{
	constexpr float Pi = 3.14159265358979f;

	AnimatorDesc SingleState(std::shared_ptr<const AnimationClip> clip, float speed = 1.0f, bool loop = true, float start = 0.0f)
	{
		AnimatorDesc desc;
		desc.SharedSkeleton = Swim::Testing::MakeChainSkeleton();
		AnimatorLayerDesc layer;
		layer.Name = "Base";
		layer.States.push_back({ "State", std::move(clip), speed, loop, start });
		desc.Layers.push_back(layer);
		return desc;
	}

	std::size_t CountEvents(const Animator& animator, const char* name)
	{
		std::size_t count = 0;
		for (const AnimatorEvent& event : animator.GetEvents())
		{
			count += event.Name == name ? 1u : 0u;
		}
		return count;
	}
} // namespace

SWIM_TEST("Animation.Animator", "StateMachineTransitionsCrossfadesTriggersAndExitTimes")
{
	AnimatorDesc desc;
	desc.SharedSkeleton = Swim::Testing::MakeChainSkeleton();
	desc.Parameters = { { "Speed", AnimatorParameterType::Float, 0.0f }, { "Jump", AnimatorParameterType::Trigger, 0.0f } };
	AnimatorLayerDesc layer;
	layer.States = { { "Idle", Swim::Testing::MakeIdleClip(), 1.0f, true, 0.0f },
		{ "Walk", Swim::Testing::MakeWalkClip(), 1.0f, true, 0.0f }, { "Bend", Swim::Testing::MakeBendClip(), 1.0f, false, 0.0f } };
	layer.Transitions = {
		{ 0, 1, 0.5f, -1.0f, { { "Speed", AnimatorConditionOp::Greater, 0.5f } }, false },
		{ 1, 0, 0.25f, -1.0f, { { "Speed", AnimatorConditionOp::Less, 0.5f } }, false },
		{ AnyState, 2, 0.0f, -1.0f, { { "Jump", AnimatorConditionOp::IsTrue, 0.0f } }, false },
		{ 2, 0, 0.0f, 1.0f, {}, false },
	};
	desc.Layers.push_back(layer);
	Animator animator(desc);

	animator.Update(0.1f);
	SWIM_CHECK_EQUAL(animator.GetCurrentState(0), 0u);
	SWIM_CHECK(animator.SetFloat("Speed", 1.0f));
	SWIM_CHECK(!animator.SetBool("Speed", true)); // Wrong type.
	animator.Update(0.1f);
	SWIM_CHECK_EQUAL(animator.GetCurrentState(0), 1u);
	SWIM_CHECK(animator.IsInTransition(0));
	SWIM_CHECK_NEAR(animator.GetTransitionProgress(0), 0.0f, 1e-6f);

	// Halfway through the 0.5 s fade the walk (at 0.25 s: x = 0.5) is weighted 0.5.
	animator.Update(0.25f);
	SWIM_CHECK_NEAR(animator.GetTransitionProgress(0), 0.5f, 1e-6f);
	SWIM_CHECK_NEAR(animator.GetPose().Joints[0].Translation[0], 0.25f, 1e-5f);
	animator.Update(0.25f);
	SWIM_CHECK(!animator.IsInTransition(0));
	SWIM_CHECK_NEAR(animator.GetPose().Joints[0].Translation[0], 1.0f, 1e-5f);

	// Any-state trigger: fires once and is consumed.
	SWIM_CHECK(animator.SetTrigger("Jump"));
	animator.Update(0.0f);
	SWIM_CHECK_EQUAL(animator.GetCurrentState(0), 2u);
	SWIM_CHECK_EQUAL(animator.GetParameter("Jump").value_or(-1.0f), 0.0f);
	animator.Update(0.5f);
	SWIM_CHECK_NEAR(AngleBetween(animator.GetPose().Joints[1].Rotation, FromAxisAngle({ 0, 0, 1 }, Pi / 4.0f)), 0.0f, 1e-3f);
	SWIM_CHECK_EQUAL(animator.GetCurrentState(0), 2u);
	// Exit time 1.0 of the non-looping bend: back to Idle as the clip ends.
	animator.Update(0.6f);
	SWIM_CHECK_EQUAL(animator.GetCurrentState(0), 0u);
	SWIM_CHECK(animator.GetPose().Joints[1] == animator.GetSkeleton().GetRestPose()[1]);
	// Speed is still 1, so Idle -> Walk again.
	animator.Update(0.0f);
	SWIM_CHECK_EQUAL(animator.GetCurrentState(0), 1u);

	// Manual play with a crossfade.
	SWIM_CHECK(animator.Play(0, "Bend", 0.2f));
	SWIM_CHECK(animator.IsInTransition(0));
	SWIM_CHECK(!animator.Play(0, "Missing"));
}

SWIM_TEST("Animation.Animator", "EventsFireAcrossLoopsInReverseAndOnceWithoutLooping")
{
	Animator looping(SingleState(Swim::Testing::MakeWalkClip()));
	looping.Update(0.0f);
	SWIM_CHECK_EQUAL(looping.GetEvents().size(), std::size_t{ 0 });
	looping.Update(2.0f);
	SWIM_CHECK_EQUAL(CountEvents(looping, "Step"), std::size_t{ 4 });
	SWIM_CHECK_EQUAL(looping.GetEvents()[0].Time, 0.25f);
	SWIM_CHECK_EQUAL(looping.GetEvents()[3].Time, 0.75f);
	looping.Update(0.125f); // 2.0 -> 2.125: nothing.
	SWIM_CHECK_EQUAL(looping.GetEvents().size(), std::size_t{ 0 });
	looping.Update(0.125f); // Reaches 2.25 exactly: fires.
	SWIM_CHECK_EQUAL(CountEvents(looping, "Step"), std::size_t{ 1 });

	Animator reverse(SingleState(Swim::Testing::MakeWalkClip(), -1.0f, true, 1.0f));
	reverse.Update(0.5f); // 1.0 -> 0.5 backwards: the 0.75 step.
	SWIM_REQUIRE_EQUAL(reverse.GetEvents().size(), std::size_t{ 1 });
	SWIM_CHECK_EQUAL(reverse.GetEvents()[0].Time, 0.75f);
	reverse.Update(1.0f); // 0.5 -> -0.5: 0.25, then -0.25 (the previous loop's 0.75).
	SWIM_CHECK_EQUAL(reverse.GetEvents().size(), std::size_t{ 2 });
	SWIM_CHECK_NEAR(reverse.GetStateTime(0), 0.5f, 1e-5f);

	Animator once(SingleState(Swim::Testing::MakeWalkClip(), 1.0f, false));
	once.Update(5.0f);
	SWIM_CHECK_EQUAL(CountEvents(once, "Step"), std::size_t{ 2 });
	once.Update(5.0f);
	SWIM_CHECK_EQUAL(once.GetEvents().size(), std::size_t{ 0 });
	SWIM_CHECK_NEAR(once.GetStateTime(0), 1.0f, 1e-6f);
	SWIM_CHECK_NEAR(once.GetPose().Joints[0].Translation[0], 2.0f, 1e-6f);
}

SWIM_TEST("Animation.Animator", "RootMotionAccumulatesAcrossLoopsAndPinsTheRoot")
{
	AnimatorDesc desc = SingleState(Swim::Testing::MakeWalkClip());
	desc.RootMotion.Enabled = true;
	Animator animator(desc);
	animator.Update(0.5f);
	SWIM_CHECK_NEAR(animator.GetRootMotion().Translation[0], 1.0f, 1e-5f);
	SWIM_CHECK_NEAR(animator.GetPose().Joints[0].Translation[0], 0.0f, 1e-6f);
	animator.Update(2.0f); // 0.5 -> 2.5: two full strides.
	SWIM_CHECK_NEAR(animator.GetRootMotion().Translation[0], 4.0f, 1e-4f);

	AnimatorDesc vertical = SingleState(Swim::Testing::MakeWalkClip());
	vertical.RootMotion.Enabled = true;
	vertical.RootMotion.Joint = "Root";
	vertical.RootMotion.TranslationAxes = { 0.0f, 1.0f, 1.0f }; // Keep x animated.
	Animator inPlace(vertical);
	inPlace.Update(0.5f);
	SWIM_CHECK_NEAR(inPlace.GetRootMotion().Translation[0], 0.0f, 1e-6f);
	SWIM_CHECK_NEAR(inPlace.GetPose().Joints[0].Translation[0], 1.0f, 1e-5f);

	AnimatorDesc bad = SingleState(Swim::Testing::MakeWalkClip());
	bad.RootMotion.Enabled = true;
	bad.RootMotion.Joint = "Nope";
	SWIM_CHECK_THROWS(Animator{ bad }, std::invalid_argument);
}

SWIM_TEST("Animation.Animator", "AdditiveMaskedLayersAndMorphOverrides")
{
	AnimatorDesc desc = SingleState(Swim::Testing::MakeBendClip(), 1.0f, false);
	AnimatorLayerDesc overlay;
	overlay.Name = "Nod";
	overlay.Blend = LayerBlendMode::Additive;
	overlay.States.push_back({ "Nod", Swim::Testing::MakeNodClip(), 1.0f, false, 0.0f });
	overlay.Mask = BoneMask::FromJoint(*desc.SharedSkeleton, 2);
	desc.Layers.push_back(overlay);
	desc.Morphs.Channels.push_back({ "Face", 2, { 0.1f, 0.2f } });
	Animator animator(desc);

	animator.Update(1.0f);
	const AnimationPose& pose = animator.GetPose();
	SWIM_CHECK_NEAR(AngleBetween(pose.Joints[1].Rotation, FromAxisAngle({ 0, 0, 1 }, Pi / 2.0f)), 0.0f, 1e-3f);
	SWIM_CHECK_NEAR(AngleBetween(pose.Joints[2].Rotation, FromAxisAngle({ 1, 0, 0 }, Pi / 6.0f)), 0.0f, 1e-3f);

	animator.SetLayerWeight(1, 0.5f);
	animator.Update(0.0f);
	SWIM_CHECK_NEAR(AngleBetween(animator.GetPose().Joints[2].Rotation, FromAxisAngle({ 1, 0, 0 }, Pi / 12.0f)), 0.0f, 1e-3f);

	SWIM_CHECK_NEAR(animator.GetPose().MorphWeights[1], 0.2f, 1e-6f);
	animator.SetMorphWeightOverride(1, 0.8f);
	animator.Update(0.0f);
	SWIM_CHECK_NEAR(animator.GetPose().MorphWeights[1], 0.8f, 1e-6f);
	animator.SetMorphWeightOverride(1, std::nullopt);
	animator.Update(0.0f);
	SWIM_CHECK_NEAR(animator.GetPose().MorphWeights[1], 0.2f, 1e-6f);

	AnimatorDesc wrongMask = desc;
	wrongMask.Layers[1].Mask.Weights = { 1.0f };
	SWIM_CHECK_THROWS(Animator{ wrongMask }, std::invalid_argument);
	AnimatorDesc unknown = desc;
	unknown.Layers[0].Transitions.push_back({ 0, 0, 0.1f, -1.0f, { { "Missing", AnimatorConditionOp::IsTrue, 0.0f } }, false });
	SWIM_CHECK_THROWS(Animator{ unknown }, std::invalid_argument);
}

SWIM_TEST("Animation.Update", "JobifiedUpdatesEqualTheSerialOnes")
{
	constexpr std::size_t Count = 24;
	const auto build = [&](std::vector<Animator>& animators, std::vector<SkeletonInstance>& instances)
	{
		for (std::size_t index = 0; index < Count; ++index)
		{
			AnimatorDesc desc =
				SingleState(index % 2 ? Swim::Testing::MakeWalkClip() : Swim::Testing::MakeBendClip(), 0.5f + 0.1f * float(index));
			animators.emplace_back(desc);
			instances.emplace_back(desc.SharedSkeleton);
		}
	};
	std::vector<Animator> serialAnimators, parallelAnimators;
	std::vector<SkeletonInstance> serialInstances, parallelInstances;
	serialAnimators.reserve(Count);
	parallelAnimators.reserve(Count);
	serialInstances.reserve(Count);
	parallelInstances.reserve(Count);
	build(serialAnimators, serialInstances);
	build(parallelAnimators, parallelInstances);
	std::vector<AnimatedSkeleton> serial, parallel;
	for (std::size_t index = 0; index < Count; ++index)
	{
		serial.push_back({ &serialAnimators[index], &serialInstances[index] });
		parallel.push_back({ &parallelAnimators[index], &parallelInstances[index] });
	}

	Swim::Jobs::JobSystem jobs;
	SWIM_REQUIRE(jobs.Initialize({ 3, 0, 0 }));
	for (int frame = 0; frame < 10; ++frame)
	{
		UpdateAnimations(serial, 1.0f / 30.0f);
		UpdateAnimations(parallel, 1.0f / 30.0f, &jobs, 2);
	}
	jobs.Shutdown();
	for (std::size_t index = 0; index < Count; ++index)
	{
		const auto a = serialInstances[index].GetSkinningMatrices();
		const auto b = parallelInstances[index].GetSkinningMatrices();
		for (std::size_t joint = 0; joint < a.size(); ++joint)
		{
			SWIM_CHECK(a[joint] == b[joint]);
		}
		SWIM_CHECK_EQUAL(serialInstances[index].GetUpdateCount(), std::uint64_t{ 10 });
	}
}
