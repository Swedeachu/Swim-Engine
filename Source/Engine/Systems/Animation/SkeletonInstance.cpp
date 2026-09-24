#include "Engine/Systems/Animation/SkeletonInstance.h"

#include <stdexcept>

namespace Swim::Animation
{
	Socket MakeSocket(const Skeleton& skeleton, std::string name, std::string_view joint, const JointPose& offset)
	{
		const std::uint32_t index = skeleton.FindJoint(joint);
		if (index == InvalidJoint)
		{
			throw std::invalid_argument("socket joint is not in the skeleton: " + std::string(joint));
		}
		return { std::move(name), index, offset };
	}

	SkeletonInstance::SkeletonInstance(std::shared_ptr<const Skeleton> source) : skeleton(std::move(source))
	{
		if (!skeleton)
		{
			throw std::invalid_argument("SkeletonInstance needs a skeleton");
		}
		AnimationPose rest;
		rest.Joints = skeleton->GetRestPose();
		Update(rest);
		historyValid = false;
		updates = 0;
	}

	void SkeletonInstance::Update(const AnimationPose& pose)
	{
		const std::uint32_t count = skeleton->GetJointCount();
		if (pose.Joints.size() != count)
		{
			throw std::invalid_argument("pose joint count does not match the skeleton");
		}
		if (historyValid)
		{
			previousSkinning.swap(skinning);
			previousMorphWeights.swap(morphWeights);
		}
		model.resize(count);
		skinning.resize(count);
		for (std::uint32_t joint = 0; joint < count; ++joint)
		{
			const std::uint32_t parent = skeleton->GetParent(joint);
			const Matrix4 local = ToMatrix(pose.Joints[joint]);
			model[joint] = Multiply(parent == InvalidJoint ? skeleton->GetRootTransform() : model[parent], local);
			skinning[joint] = ToAffineRows(Multiply(model[joint], skeleton->GetInverseBind(joint)));
		}
		morphWeights = pose.MorphWeights;
		if (!historyValid)
		{
			previousSkinning = skinning;
			previousMorphWeights = morphWeights;
			historyValid = true;
		}
		++updates;
	}

	Matrix4 SkeletonInstance::GetSocketTransform(const Socket& socket) const
	{
		if (socket.Joint >= model.size())
		{
			throw std::out_of_range("socket joint is outside the skeleton");
		}
		return Multiply(model[socket.Joint], ToMatrix(socket.Offset));
	}
} // namespace Swim::Animation
