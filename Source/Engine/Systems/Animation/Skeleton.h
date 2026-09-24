#pragma once
#include "Engine/Assets/SkeletonAsset.h"
#include "Engine/Systems/Animation/AnimationMath.h"

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Swim::Animation
{
	inline constexpr std::uint32_t InvalidJoint = std::numeric_limits<std::uint32_t>::max();

	// Runtime skeleton built once from a SkeletonAsset and shared (read-only) by
	// every animator and skeleton instance that uses it. Joints are parents first.
	class Skeleton
	{
	  public:
		// Throws std::invalid_argument when the asset is empty, not parents first,
		// has duplicate joint names or more than MaxJoints joints.
		explicit Skeleton(const Assets::SkeletonAsset& asset);

		static constexpr std::uint32_t MaxJoints = 65536; // Joints0 is 16-bit.

		std::uint32_t GetJointCount() const { return static_cast<std::uint32_t>(parents.size()); }

		std::uint32_t GetParent(std::uint32_t joint) const { return parents[joint]; }

		const std::string& GetName(std::uint32_t joint) const { return names[joint]; }

		std::uint32_t FindJoint(std::string_view name) const;
		bool IsDescendant(std::uint32_t joint, std::uint32_t ancestor) const; // A joint is its own descendant.

		const std::vector<JointPose>& GetRestPose() const { return restPose; }

		const Matrix4& GetInverseBind(std::uint32_t joint) const { return inverseBind[joint]; }

		const Matrix4& GetRootTransform() const { return rootTransform; }

	  private:
		std::vector<std::uint32_t> parents;
		std::vector<std::string> names;
		std::vector<JointPose> restPose;
		std::vector<Matrix4> inverseBind;
		Matrix4 rootTransform = IdentityMatrix;
		std::unordered_map<std::string, std::uint32_t> lookup;
	};

	// A local pose: one JointPose per skeleton joint plus the animator's morph weights.
	struct AnimationPose
	{
		std::vector<JointPose> Joints;
		std::vector<float> MorphWeights;
	};

	// Per-joint blend weights in [0, 1]; empty means every joint weighs 1.
	struct BoneMask
	{
		std::vector<float> Weights;

		float Get(std::uint32_t joint) const { return Weights.empty() ? 1.0f : Weights[joint]; }

		// weight for `root` and (optionally) all its descendants, 0 elsewhere.
		static BoneMask FromJoint(const Skeleton& skeleton, std::uint32_t root, float weight = 1.0f, bool includeDescendants = true);
		// This mask with `joint` (and optionally its descendants) set to weight.
		BoneMask& Set(const Skeleton& skeleton, std::uint32_t joint, float weight, bool includeDescendants = true);
	};
} // namespace Swim::Animation
