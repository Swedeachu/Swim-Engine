#include "Engine/Assets/AssetDatabase.h"
#include "Engine/Assets/AssetHandle.h"
#include "Engine/Assets/AssetId.h"
#include "Engine/Assets/AssetState.h"
#include "Engine/Assets/AssetSystem.h"
#include "Engine/Assets/ContentHash.h"
#include "Engine/Assets/MaterialAsset.h"
#include "Engine/Assets/MeshAsset.h"
#include "Engine/Assets/ModelAsset.h"
#include "Engine/Assets/TextureAsset.h"

namespace
{
	struct CompileOnlyAsset
	{
		int Value = 0;
	};

	[[maybe_unused]] Swim::Assets::AssetHandle<CompileOnlyAsset> Handle;
}
