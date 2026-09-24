#pragma once

#include "Engine/Assets/AssetMath.h"

#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace Swim::Assets
{

	// One joint of a compiled skeleton. Joints are stored parents first, so a
	// single forward pass turns local poses into model-space transforms.
	struct SkeletonJoint
	{
		static constexpr std::uint32_t InvalidJoint = std::numeric_limits<std::uint32_t>::max();

		std::string Name;
		std::uint32_t Parent = InvalidJoint;
		AssetTransform RestTransform{}; // Local bind/rest pose relative to Parent.
		// Model-space inverse bind matrix, column-major 4x4 (glTF order).
		std::array<float, 16> InverseBind{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
		// Model-space node the joint came from, for binding clips authored against
		// node names that were renamed. InvalidJoint when unknown.
		std::uint32_t SourceNode = InvalidJoint;
	};

	// A skin's joint hierarchy (one per glTF skin). Skinned mesh vertices index
	// Joints directly. RootTransform is the model-space rest transform of the
	// nearest non-joint ancestor of the root joints (identity when there is none);
	// it is column-major like InverseBind.
	struct SkeletonAsset
	{
		std::vector<SkeletonJoint> Joints;
		std::array<float, 16> RootTransform{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
	};

} // namespace Swim::Assets
