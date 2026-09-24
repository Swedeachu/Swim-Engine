#include "Engine/Systems/Animation/AnimationClip.h"
#include "Engine/Systems/Animation/SkeletonInstance.h"
#include "Tests/Fixtures/AnimationFixture.h"
#include "Tests/Framework/Test.h"

#include <cmath>
#include <stdexcept>

using namespace Swim::Animation;
using Swim::Testing::AngleBetween;

namespace
{
	constexpr float Pi = 3.14159265358979f;

	bool Near(const Vec3& a, const Vec3& b, float tolerance = 1e-5f)
	{
		return std::abs(a[0] - b[0]) <= tolerance && std::abs(a[1] - b[1]) <= tolerance && std::abs(a[2] - b[2]) <= tolerance;
	}
} // namespace

SWIM_TEST("Animation.Math", "QuaternionsSlerpComposeAndMatchMatrices")
{
	const Quat a = FromAxisAngle({ 0, 0, 1 }, 0.0f);
	const Quat b = FromAxisAngle({ 0, 0, 1 }, Pi / 2.0f);
	SWIM_CHECK_NEAR(AngleBetween(Slerp(a, b, 0.5f), FromAxisAngle({ 0, 0, 1 }, Pi / 4.0f)), 0.0f, 1e-3f);
	// Shortest path: -b is the same rotation, so the midpoint does not go the long way.
	const Quat negated{ -b[0], -b[1], -b[2], -b[3] };
	SWIM_CHECK_NEAR(AngleBetween(Slerp(a, negated, 0.5f), FromAxisAngle({ 0, 0, 1 }, Pi / 4.0f)), 0.0f, 1e-3f);
	SWIM_CHECK_NEAR(AngleBetween(Nlerp(a, negated, 1.0f), b), 0.0f, 1e-3f);
	SWIM_CHECK(Near(Rotate(b, { 1, 0, 0 }), { 0, 1, 0 }));

	JointPose parent;
	parent.Translation = { 1, 2, 3 };
	parent.Rotation = b;
	parent.Scale = { 2, 2, 2 };
	JointPose child;
	child.Translation = { 1, 0, 0 };
	child.Rotation = FromAxisAngle({ 1, 0, 0 }, 0.3f);
	const Matrix4 composed = ToMatrix(Compose(parent, child));
	const Matrix4 product = Multiply(ToMatrix(parent), ToMatrix(child));
	for (std::size_t index = 0; index < 16; ++index)
	{
		SWIM_CHECK_NEAR(composed[index], product[index], 1e-5f);
	}
	const Vec3 point{ 0.5f, -1.0f, 2.0f };
	SWIM_CHECK(Near(TransformPoint(ToAffineRows(product), point), TransformPoint(product, point)));
	SWIM_CHECK_NEAR(MaxColumnScale(ToAffineRows(product)), 2.0f, 1e-5f);
}

SWIM_TEST("Animation.Skeleton", "ValidatesJointOrderAndBuildsMasks")
{
	const auto skeleton = Swim::Testing::MakeChainSkeleton();
	SWIM_CHECK_EQUAL(skeleton->GetJointCount(), 3u);
	SWIM_CHECK_EQUAL(skeleton->FindJoint("Tip"), 2u);
	SWIM_CHECK_EQUAL(skeleton->FindJoint("Missing"), InvalidJoint);
	SWIM_CHECK(skeleton->IsDescendant(2, 0));
	SWIM_CHECK(!skeleton->IsDescendant(0, 2));

	const BoneMask upper = BoneMask::FromJoint(*skeleton, 1);
	SWIM_CHECK_EQUAL(upper.Get(0), 0.0f);
	SWIM_CHECK_EQUAL(upper.Get(1), 1.0f);
	SWIM_CHECK_EQUAL(upper.Get(2), 1.0f);
	BoneMask partial;
	partial.Set(*skeleton, 2, 0.5f);
	SWIM_CHECK_EQUAL(partial.Get(0), 1.0f);
	SWIM_CHECK_EQUAL(partial.Get(2), 0.5f);

	auto badOrder = Swim::Testing::MakeChainSkeletonAsset();
	badOrder.Joints[0].Parent = 2;
	SWIM_CHECK_THROWS(Skeleton{ badOrder }, std::invalid_argument);
	auto duplicate = Swim::Testing::MakeChainSkeletonAsset();
	duplicate.Joints[2].Name = "Mid";
	SWIM_CHECK_THROWS(Skeleton{ duplicate }, std::invalid_argument);
	SWIM_CHECK_THROWS(Skeleton{ Swim::Assets::SkeletonAsset{} }, std::invalid_argument);
}

SWIM_TEST("Animation.Clip", "SamplesStepLinearCubicAndClampsOutsideItsKeys")
{
	using Swim::Assets::AnimationInterpolation;
	using Swim::Assets::AnimationPath;
	float out[4] = {};

	auto step = Swim::Testing::MakeVectorTrack(
		"Root", AnimationPath::Translation, { 0.0f, 1.0f, 2.0f }, { 0, 0, 0, 1, 0, 0, 2, 0, 0 }, AnimationInterpolation::Step);
	SampleTrack(step, 0.99f, out);
	SWIM_CHECK_EQUAL(out[0], 0.0f);
	SampleTrack(step, 1.0f, out);
	SWIM_CHECK_EQUAL(out[0], 1.0f);

	auto linear = Swim::Testing::MakeVectorTrack("Root", AnimationPath::Translation, { 1.0f, 3.0f }, { 0, 0, 0, 4, 2, 0 });
	SampleTrack(linear, 2.0f, out);
	SWIM_CHECK_NEAR(out[0], 2.0f, 1e-6f);
	SWIM_CHECK_NEAR(out[1], 1.0f, 1e-6f);
	SampleTrack(linear, -5.0f, out);
	SWIM_CHECK_EQUAL(out[0], 0.0f);
	SampleTrack(linear, 9.0f, out);
	SWIM_CHECK_EQUAL(out[0], 4.0f);

	// Cubic Hermite (glTF): (in, value, out) per key; tangents scale with the interval.
	Swim::Assets::AnimationTrack cubic;
	cubic.Target = "Root";
	cubic.Path = AnimationPath::Translation;
	cubic.Interpolation = AnimationInterpolation::CubicSpline;
	cubic.Components = 3;
	cubic.Times = { 0.0f, 2.0f };
	cubic.Values = { 0, 0, 0, /*v0*/ 0, 0, 0, /*out0*/ 1, 0, 0, /*in1*/ -1, 0, 0, /*v1*/ 3, 0, 0, /*out1*/ 0, 0, 0 };
	for (const float t : { 0.25f, 0.5f, 0.8f })
	{
		SampleTrack(cubic, t * 2.0f, out);
		const float t2 = t * t, t3 = t2 * t;
		const float expected = (t3 - 2 * t2 + t) * 2.0f * 1.0f + (-2 * t3 + 3 * t2) * 3.0f + (t3 - t2) * 2.0f * -1.0f;
		SWIM_CHECK_NEAR(out[0], expected, 1e-5f);
	}

	auto rotation = Swim::Testing::MakeRotationTrack("Mid", { 0, 1, 0 }, { 0.0f, 1.0f }, { 0.0f, 120.0f });
	SampleTrack(rotation, 0.25f, out);
	const Quat sampled{ out[0], out[1], out[2], out[3] };
	SWIM_CHECK_NEAR(Dot(sampled, sampled), 1.0f, 1e-5f);
	SWIM_CHECK_NEAR(AngleBetween(sampled, FromAxisAngle({ 0, 1, 0 }, 30.0f * Pi / 180.0f)), 0.0f, 1e-3f);
}

SWIM_TEST("Animation.Clip", "BindsTracksByNameAndDrivesMorphChannels")
{
	const auto skeleton = Swim::Testing::MakeChainSkeleton();
	Swim::Assets::AnimationClipAsset asset;
	asset.Tracks.push_back(Swim::Testing::MakeRotationTrack("Mid", { 0, 0, 1 }, { 0.0f, 1.0f }, { 0.0f, 90.0f }));
	asset.Tracks.push_back(Swim::Testing::MakeRotationTrack("NotAJoint", { 0, 0, 1 }, { 0.0f }, { 45.0f }));
	Swim::Assets::AnimationTrack weights;
	weights.Target = "Face";
	weights.Path = Swim::Assets::AnimationPath::MorphWeights;
	weights.Components = 3;
	weights.Times = { 0.0f, 1.0f };
	weights.Values = { 0, 0, 0, 1, 0.5f, 0.25f };
	asset.Tracks.push_back(weights);
	const AnimationClip clip(asset);
	SWIM_CHECK_EQUAL(clip.GetDuration(), 1.0f);

	MorphLayout morphs;
	morphs.Channels.push_back({ "Body", 1, { 0.75f } });
	morphs.Channels.push_back({ "Face", 2, {} }); // Only two of the track's three weights fit.
	const ClipBinding binding = BindClip(clip, *skeleton, morphs);
	SWIM_CHECK_EQUAL(binding.BoundTracks, 2u);
	SWIM_CHECK_EQUAL(binding.Targets[0].Joint, 1u);
	SWIM_CHECK_EQUAL(binding.Targets[1].Joint, InvalidJoint);
	SWIM_CHECK_EQUAL(binding.Targets[2].MorphOffset, 1u);
	SWIM_CHECK_EQUAL(binding.Targets[2].MorphCount, 2u);

	AnimationPose pose = MakeRestPose(*skeleton, morphs);
	SWIM_REQUIRE_EQUAL(pose.MorphWeights.size(), std::size_t{ 3 });
	SampleClip(clip, binding, 0.5f, pose);
	SWIM_CHECK_EQUAL(pose.MorphWeights[0], 0.75f); // Untouched default.
	SWIM_CHECK_NEAR(pose.MorphWeights[1], 0.5f, 1e-6f);
	SWIM_CHECK_NEAR(pose.MorphWeights[2], 0.25f, 1e-6f);
	SWIM_CHECK_NEAR(AngleBetween(pose.Joints[1].Rotation, FromAxisAngle({ 0, 0, 1 }, Pi / 4.0f)), 0.0f, 1e-3f);
	SWIM_CHECK(pose.Joints[0] == skeleton->GetRestPose()[0]);

	Swim::Assets::AnimationClipAsset broken = asset;
	broken.Tracks[0].Times = { 1.0f, 0.5f };
	SWIM_CHECK_THROWS(AnimationClip{ broken }, std::invalid_argument);
}

SWIM_TEST("Animation.Blend", "BlendsAddsAndMasksPoses")
{
	const auto skeleton = Swim::Testing::MakeChainSkeleton();
	AnimationPose rest = MakeRestPose(*skeleton);
	AnimationPose bent = rest;
	bent.Joints[1].Rotation = FromAxisAngle({ 0, 0, 1 }, Pi / 2.0f);
	bent.Joints[2].Translation = { 0, 3, 0 };

	AnimationPose half = rest;
	BlendPose(half, bent, 0.5f);
	SWIM_CHECK_NEAR(AngleBetween(half.Joints[1].Rotation, FromAxisAngle({ 0, 0, 1 }, Pi / 4.0f)), 0.0f, 1e-3f);
	SWIM_CHECK(Near(half.Joints[2].Translation, { 0, 2, 0 }));

	const BoneMask tipOnly = BoneMask::FromJoint(*skeleton, 2);
	AnimationPose masked = rest;
	BlendPose(masked, bent, 1.0f, &tipOnly);
	SWIM_CHECK(masked.Joints[1] == rest.Joints[1]);
	SWIM_CHECK(Near(masked.Joints[2].Translation, { 0, 3, 0 }));

	// Additive round trip: reference + (pose - reference) = pose, and half weight halves it.
	const AnimationPose delta = MakeAdditivePose(bent, rest);
	AnimationPose added = rest;
	AddPose(added, delta, 1.0f);
	SWIM_CHECK_NEAR(AngleBetween(added.Joints[1].Rotation, bent.Joints[1].Rotation), 0.0f, 1e-4f);
	SWIM_CHECK(Near(added.Joints[2].Translation, { 0, 3, 0 }));
	AnimationPose halfAdded = rest;
	AddPose(halfAdded, delta, 0.5f);
	SWIM_CHECK_NEAR(AngleBetween(halfAdded.Joints[1].Rotation, FromAxisAngle({ 0, 0, 1 }, Pi / 4.0f)), 0.0f, 1e-3f);
	// Additive on top of another pose rotates relative to it.
	AnimationPose onTop = bent;
	AddPose(onTop, delta, 1.0f);
	SWIM_CHECK_NEAR(AngleBetween(onTop.Joints[1].Rotation, FromAxisAngle({ 0, 0, 1 }, Pi)), 0.0f, 1e-3f);
}

SWIM_TEST("Animation.SkeletonInstance", "BuildsSkinningPalettesHistoryAndSockets")
{
	const auto skeleton = Swim::Testing::MakeChainSkeleton(0.5f);
	SkeletonInstance instance(skeleton);
	// Rest pose with inverse binds of the rest pose: an identity palette.
	for (const Matrix3x4& matrix : instance.GetSkinningMatrices())
	{
		for (std::size_t index = 0; index < 12; ++index)
		{
			SWIM_CHECK_NEAR(matrix[index], IdentityAffine[index], 1e-6f);
		}
	}
	SWIM_CHECK(Near(TransformPoint(instance.GetModelTransforms()[2], { 0, 0, 0 }), { 0, 2, 0.5f }));

	AnimationPose pose = MakeRestPose(*skeleton);
	pose.Joints[1].Rotation = FromAxisAngle({ 0, 0, 1 }, Pi / 2.0f);
	instance.Update(pose);
	// The tip swings to x = -1 at y = 1; a vertex bound to it at its rest spot follows.
	SWIM_CHECK(Near(TransformPoint(instance.GetModelTransforms()[2], { 0, 0, 0 }), { -1, 1, 0.5f }));
	SWIM_CHECK(Near(TransformPoint(instance.GetSkinningMatrices()[2], { 0, 2, 0.5f }), { -1, 1, 0.5f }));
	// The first update has no history (the object just appeared): previous = current.
	SWIM_CHECK(Near(TransformPoint(instance.GetPreviousSkinningMatrices()[2], { 0, 2, 0.5f }), { -1, 1, 0.5f }));

	instance.Update(MakeRestPose(*skeleton));
	SWIM_CHECK(Near(TransformPoint(instance.GetPreviousSkinningMatrices()[2], { 0, 2, 0.5f }), { -1, 1, 0.5f }));
	SWIM_CHECK(Near(TransformPoint(instance.GetSkinningMatrices()[2], { 0, 2, 0.5f }), { 0, 2, 0.5f }));
	instance.Update(pose);
	instance.ResetHistory();
	instance.Update(MakeRestPose(*skeleton));
	SWIM_CHECK(Near(TransformPoint(instance.GetPreviousSkinningMatrices()[2], { 0, 2, 0.5f }), { 0, 2, 0.5f }));

	JointPose offset;
	offset.Translation = { 0.25f, 0, 0 };
	const Socket hand = MakeSocket(*skeleton, "Hand", "Tip", offset);
	SWIM_CHECK(Near(TransformPoint(instance.GetSocketTransform(hand), { 0, 0, 0 }), { 0.25f, 2, 0.5f }));
	SWIM_CHECK_THROWS(MakeSocket(*skeleton, "Bad", "Nope"), std::invalid_argument);
	AnimationPose wrong;
	SWIM_CHECK_THROWS(instance.Update(wrong), std::invalid_argument);
}
