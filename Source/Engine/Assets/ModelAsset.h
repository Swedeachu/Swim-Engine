#pragma once

#include "Engine/Assets/AssetHandle.h"
#include "Engine/Assets/AssetMath.h"
#include "Engine/Assets/MaterialAsset.h"
#include "Engine/Assets/MeshAsset.h"

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
	};

	struct ModelAsset
	{
		std::vector<ModelNode> Nodes;
		std::vector<std::uint32_t> Roots;
	};

}
