#pragma once

#include "Engine/Assets/AnimationClipAsset.h"
#include "Engine/Assets/AssetHandle.h"
#include "Engine/Assets/AssetMath.h"
#include "Engine/Assets/MaterialAsset.h"
#include "Engine/Assets/MeshAsset.h"
#include "Engine/Assets/SkeletonAsset.h"

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace Swim::Assets
{

	struct ModelNode
	{
		static constexpr std::uint32_t InvalidNode = std::numeric_limits<std::uint32_t>::max();

		std::string Name;
		std::uint32_t Parent = InvalidNode;
		AssetTransform LocalTransform{};
		AssetHandle<MeshAsset> Mesh;

		// Material bindings are independent of the mesh identity. A model chooses
		// defaults per mesh material slot without making either asset own the other.
		std::vector<AssetHandle<MaterialInstanceAsset>> Materials;

		// Skinned mesh nodes reference the skeleton their Joints0 indices address.
		// A skinned node's own transform is ignored (glTF): its vertices end up in
		// model space through the skeleton's joints.
		AssetHandle<SkeletonAsset> Skin;
		// Per-node morph weights override the mesh defaults when non-empty.
		std::vector<float> MorphWeights;
	};

	struct ModelAsset
	{
		std::vector<ModelNode> Nodes;
		std::vector<std::uint32_t> Roots;
		std::vector<AssetHandle<SkeletonAsset>> Skeletons;
		std::vector<AssetHandle<AnimationClipAsset>> Animations;
	};

} // namespace Swim::Assets
