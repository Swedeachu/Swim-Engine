#include "Engine/Systems/Animation/Skeleton.h"

#include <stdexcept>

namespace Swim::Animation
{
	Skeleton::Skeleton(const Assets::SkeletonAsset& asset)
	{
		if (asset.Joints.empty())
		{
			throw std::invalid_argument("Skeleton needs at least one joint");
		}
		if (asset.Joints.size() > MaxJoints)
		{
			throw std::invalid_argument("Skeleton has more joints than 16-bit joint indices can address");
		}
		const auto count = static_cast<std::uint32_t>(asset.Joints.size());
		parents.reserve(count);
		names.reserve(count);
		restPose.reserve(count);
		inverseBind.reserve(count);
		rootTransform = asset.RootTransform;
		for (std::uint32_t joint = 0; joint < count; ++joint)
		{
			const Assets::SkeletonJoint& source = asset.Joints[joint];
			if (source.Parent != Assets::SkeletonJoint::InvalidJoint && source.Parent >= joint)
			{
				throw std::invalid_argument("Skeleton joints must be listed parents first");
			}
			if (!lookup.emplace(source.Name, joint).second)
			{
				throw std::invalid_argument("Skeleton joint names must be unique: " + source.Name);
			}
			parents.push_back(source.Parent == Assets::SkeletonJoint::InvalidJoint ? InvalidJoint : source.Parent);
			names.push_back(source.Name);
			JointPose rest;
			rest.Translation = source.RestTransform.Translation;
			rest.Rotation = Normalize(source.RestTransform.Rotation);
			rest.Scale = source.RestTransform.Scale;
			restPose.push_back(rest);
			inverseBind.push_back(source.InverseBind);
		}
	}

	std::uint32_t Skeleton::FindJoint(std::string_view name) const
	{
		const auto found = lookup.find(std::string(name));
		return found == lookup.end() ? InvalidJoint : found->second;
	}

	bool Skeleton::IsDescendant(std::uint32_t joint, std::uint32_t ancestor) const
	{
		for (std::uint32_t walk = joint; walk != InvalidJoint; walk = parents[walk])
		{
			if (walk == ancestor)
			{
				return true;
			}
		}
		return false;
	}

	BoneMask BoneMask::FromJoint(const Skeleton& skeleton, std::uint32_t root, float weight, bool includeDescendants)
	{
		BoneMask mask;
		mask.Weights.assign(skeleton.GetJointCount(), 0.0f);
		return mask.Set(skeleton, root, weight, includeDescendants);
	}

	BoneMask& BoneMask::Set(const Skeleton& skeleton, std::uint32_t joint, float weight, bool includeDescendants)
	{
		if (joint >= skeleton.GetJointCount())
		{
			throw std::out_of_range("BoneMask joint is outside the skeleton");
		}
		if (Weights.empty())
		{
			Weights.assign(skeleton.GetJointCount(), 1.0f);
		}
		for (std::uint32_t index = 0; index < skeleton.GetJointCount(); ++index)
		{
			if (index == joint || (includeDescendants && skeleton.IsDescendant(index, joint)))
			{
				Weights[index] = weight;
			}
		}
		return *this;
	}
} // namespace Swim::Animation
