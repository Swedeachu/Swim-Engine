#pragma once
#include "Engine/Systems/Animation/Skeleton.h"

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Swim::Animation
{
	// A named attachment point: a joint plus a local offset (weapons, effects).
	struct Socket
	{
		std::string Name;
		std::uint32_t Joint = InvalidJoint;
		JointPose Offset{};
	};

	// Throws std::invalid_argument when the joint is not in the skeleton.
	Socket MakeSocket(const Skeleton& skeleton, std::string name, std::string_view joint, const JointPose& offset = {});

	// One posed skeleton: model-space joint matrices and the skinning palette the
	// GPU consumes (model x inverse bind, as row-major 3x4 rows), plus the
	// previous update's palette and morph weights for motion vectors.
	class SkeletonInstance
	{
	  public:
		explicit SkeletonInstance(std::shared_ptr<const Skeleton> skeleton);

		// Moves the current palette/weights to previous (or copies the new ones there
		// on the first update and after ResetHistory), then poses the skeleton.
		// Throws std::invalid_argument when the pose does not match the skeleton.
		void Update(const AnimationPose& pose);

		// The next Update has no motion (teleports, first frame after a cut).
		void ResetHistory() { historyValid = false; }

		const Skeleton& GetSkeleton() const { return *skeleton; }

		std::span<const Matrix4> GetModelTransforms() const { return model; }

		std::span<const Matrix3x4> GetSkinningMatrices() const { return skinning; }

		std::span<const Matrix3x4> GetPreviousSkinningMatrices() const { return previousSkinning; }

		std::span<const float> GetMorphWeights() const { return morphWeights; }

		std::span<const float> GetPreviousMorphWeights() const { return previousMorphWeights; }

		std::uint64_t GetUpdateCount() const { return updates; }

		// Model-space transform of a socket (joint model transform x offset).
		Matrix4 GetSocketTransform(const Socket& socket) const;

	  private:
		std::shared_ptr<const Skeleton> skeleton;
		std::vector<Matrix4> model;
		std::vector<Matrix3x4> skinning;
		std::vector<Matrix3x4> previousSkinning;
		std::vector<float> morphWeights;
		std::vector<float> previousMorphWeights;
		std::uint64_t updates = 0;
		bool historyValid = false;
	};
} // namespace Swim::Animation
