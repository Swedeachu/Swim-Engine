#pragma once
#include "Engine/Assets/AnimationClipAsset.h"
#include "Engine/Assets/SkeletonAsset.h"
#include "Engine/Systems/Animation/AnimationClip.h"
#include "Engine/Systems/Animation/Skeleton.h"

#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace Swim::Testing
{
	// A three-joint chain along +y: Root (origin) -> Mid (y = 1) -> Tip (y = 2 in
	// model space). Inverse bind matrices invert the rest pose, so the rest
	// skinning palette is the identity.
	inline Assets::SkeletonAsset MakeChainSkeletonAsset(float rootZ = 0.0f)
	{
		Assets::SkeletonAsset asset;
		asset.RootTransform[14] = rootZ;
		const char* names[] = { "Root", "Mid", "Tip" };
		for (std::uint32_t joint = 0; joint < 3; ++joint)
		{
			Assets::SkeletonJoint out;
			out.Name = names[joint];
			out.Parent = joint == 0 ? Assets::SkeletonJoint::InvalidJoint : joint - 1;
			out.RestTransform.Translation = { 0.0f, joint == 0 ? 0.0f : 1.0f, 0.0f };
			// Inverse of the rest model transform: translate by -(rootZ, y).
			out.InverseBind[13] = -static_cast<float>(joint);
			out.InverseBind[14] = -rootZ;
			asset.Joints.push_back(out);
		}
		return asset;
	}

	inline std::shared_ptr<const Animation::Skeleton> MakeChainSkeleton(float rootZ = 0.0f)
	{
		return std::make_shared<const Animation::Skeleton>(MakeChainSkeletonAsset(rootZ));
	}

	inline Assets::AnimationTrack MakeRotationTrack(
		std::string target, Animation::Vec3 axis, std::vector<float> times, std::vector<float> degrees)
	{
		Assets::AnimationTrack track;
		track.Target = std::move(target);
		track.Path = Assets::AnimationPath::Rotation;
		track.Components = 4;
		track.Times = std::move(times);
		for (const float angle : degrees)
		{
			const Animation::Quat q = Animation::FromAxisAngle(axis, angle * 3.14159265358979f / 180.0f);
			track.Values.insert(track.Values.end(), q.begin(), q.end());
		}
		return track;
	}

	inline Assets::AnimationTrack MakeVectorTrack(std::string target, Assets::AnimationPath path, std::vector<float> times,
		std::vector<float> values, Assets::AnimationInterpolation interpolation = Assets::AnimationInterpolation::Linear)
	{
		Assets::AnimationTrack track;
		track.Target = std::move(target);
		track.Path = path;
		track.Interpolation = interpolation;
		track.Components = 3;
		track.Times = std::move(times);
		track.Values = std::move(values);
		return track;
	}

	// Mid bends 0 -> 90 degrees about z over one second.
	inline std::shared_ptr<const Animation::AnimationClip> MakeBendClip()
	{
		Assets::AnimationClipAsset clip;
		clip.Name = "Bend";
		clip.Tracks.push_back(MakeRotationTrack("Mid", { 0, 0, 1 }, { 0.0f, 1.0f }, { 0.0f, 90.0f }));
		return std::make_shared<const Animation::AnimationClip>(clip);
	}

	// Root walks 0 -> 2 along x per one-second loop; "Step" at 0.25 s and 0.75 s.
	inline std::shared_ptr<const Animation::AnimationClip> MakeWalkClip()
	{
		Assets::AnimationClipAsset clip;
		clip.Name = "Walk";
		clip.Tracks.push_back(MakeVectorTrack("Root", Assets::AnimationPath::Translation, { 0.0f, 1.0f }, { 0, 0, 0, 2, 0, 0 }));
		clip.Events = { { 0.25f, "Step" }, { 0.75f, "Step" } };
		return std::make_shared<const Animation::AnimationClip>(clip);
	}

	// Tip nods 0 -> 30 degrees about x over one second (an additive overlay).
	inline std::shared_ptr<const Animation::AnimationClip> MakeNodClip()
	{
		Assets::AnimationClipAsset clip;
		clip.Name = "Nod";
		clip.Tracks.push_back(MakeRotationTrack("Tip", { 1, 0, 0 }, { 0.0f, 1.0f }, { 0.0f, 30.0f }));
		return std::make_shared<const Animation::AnimationClip>(clip);
	}

	// A clip with no tracks, one second long (the idle state).
	inline std::shared_ptr<const Animation::AnimationClip> MakeIdleClip(float duration = 1.0f)
	{
		Assets::AnimationClipAsset clip;
		clip.Name = "Idle";
		clip.Duration = duration;
		return std::make_shared<const Animation::AnimationClip>(clip);
	}

	inline float AngleBetween(const Animation::Quat& a, const Animation::Quat& b)
	{
		const float d = std::abs(Animation::Dot(Animation::Normalize(a), Animation::Normalize(b)));
		return 2.0f * std::acos(d > 1.0f ? 1.0f : d);
	}
} // namespace Swim::Testing
