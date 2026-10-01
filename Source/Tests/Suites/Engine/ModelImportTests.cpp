#include "Engine/Systems/Scene/ModelImporter/ModelImport.h"
#include "Engine/Assets/AssetSystem.h"
#include "Tests/Framework/Test.h"

#include <string>

SWIM_TEST("Engine.ModelImport", "KeywordPreferenceAndAvoidanceRankCookedModels")
{
	Swim::Assets::AssetSystem assets;
	SWIM_REQUIRE(assets.Initialize());

	for (const char* path : { "Models/Sponza/sponza-ktx.model", "Models/Sponza/sponza-ktx-draco.model", "Models/Sponza/glTF/Sponza.model",
			 "Models/Barrel/barrel.model" })
	{
		const auto handle = assets.Declare<Swim::Assets::ModelAsset>(path);
		SWIM_REQUIRE(assets.Publish(handle, Swim::Assets::ModelAsset{}));
	}

	const auto path = [&](auto handle)
	{
		return assets.GetDatabase().FindPath(handle.GetId()).value_or(std::string());
	};
	const auto sponza = Engine::FindCookedModel(assets, { "sponza" }, { "sponza-ktx-draco", "sponza-ktx", "gltf/sponza" });
	SWIM_REQUIRE(sponza.IsValid());
	SWIM_CHECK_EQUAL(path(sponza), std::string("Models/Sponza/sponza-ktx-draco.model"));
	// Keyword filtering, preference order and avoidance.
	const auto plain = Engine::FindCookedModel(assets, { "sponza" }, { "gltf/sponza" }, { "ktx" });
	SWIM_REQUIRE(plain.IsValid());
	SWIM_CHECK_EQUAL(path(plain), std::string("Models/Sponza/glTF/Sponza.model"));
	const auto ktxOnly = Engine::FindCookedModel(assets, { "sponza", "ktx" }, {}, { "draco" });
	SWIM_REQUIRE(ktxOnly.IsValid());
	SWIM_CHECK_EQUAL(path(ktxOnly), std::string("Models/Sponza/sponza-ktx.model"));
	SWIM_CHECK(!Engine::FindCookedModel(assets, { "helmet" }).IsValid());
	assets.Shutdown();
}
