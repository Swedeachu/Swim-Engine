#include "Engine/Assets/AssetSystem.h"
#include "Engine/Assets/MaterialAsset.h"
#include "Engine/Assets/MeshAsset.h"
#include "Engine/Assets/ModelAsset.h"
#include "Tests/Framework/Test.h"

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>

namespace
{

	struct TestMeshAsset
	{
		int VertexCount = 0;
	};

	struct TestTextureAsset
	{
		int Width = 0;
	};

}

SWIM_TEST("Assets.ContentHash", "MatchesPublishedSha256Vectors")
{
	using namespace Swim::Assets;

	SWIM_CHECK_EQUAL(
		ComputeContentHash("").ToHex(),
		std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
	SWIM_CHECK_EQUAL(
		ComputeContentHash("abc").ToHex(),
		std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
	SWIM_CHECK(ContentHash::FromHex(ComputeContentHash("abc").ToHex()) == ComputeContentHash("abc"));
}

SWIM_TEST("Assets.AssetDatabase", "LogicalPathNormalizationAndRootEscape")
{
	using namespace Swim::Assets;

	SWIM_CHECK_EQUAL(NormalizeAssetPath("Models\\Props/../Props//Crate.glb"), std::string("Models/Props/Crate.glb"));
	SWIM_CHECK_THROWS((void)NormalizeAssetPath("../Outside.asset"), std::invalid_argument);
}

SWIM_TEST("Assets.AssetDatabase", "IdentityIsDeterministicAndReboundPathsKeepIt")
{
	using namespace Swim::Assets;

	AssetDatabase database;
	const AssetId databaseId = database.GetOrCreate("Models/Props/Crate.glb");

	SWIM_CHECK(databaseId == database.GetOrCreate("Models\\Props\\Crate.glb"));
	SWIM_CHECK(database.Rebind(databaseId, "Models/Environment/Crate.glb"));
	SWIM_CHECK(database.FindId("Models/Environment/Crate.glb") == databaseId);
	SWIM_CHECK(database.FindPath(databaseId) == std::optional<std::string>("Models/Environment/Crate.glb"));
}

SWIM_TEST("Assets.AssetSystem", "ResidencyLifecycleAndDependencyGraph")
{
	using namespace Swim::Assets;

	AssetSystem assets;
	SWIM_REQUIRE(assets.Initialize());

	auto texture = assets.Declare<TestTextureAsset>("Textures/CrateAlbedo.ktx2");
	auto mesh = assets.Declare<TestMeshAsset>("Meshes/Crate.sasset");

	SWIM_CHECK(mesh.IsValid());
	SWIM_CHECK(assets.Resolve(mesh) == nullptr);
	SWIM_CHECK(assets.GetStatus(mesh).State == AssetLoadState::Unloaded);
	SWIM_CHECK(assets.Queue(mesh));
	SWIM_CHECK(assets.BeginLoading(mesh));

	const AssetId dependencies[] = { texture.GetId(), texture.GetId() };
	const ContentHash meshHash = ComputeContentHash("compiled-mesh-payload");
	SWIM_REQUIRE(assets.Publish(mesh, TestMeshAsset{ 36 }, meshHash, dependencies));
	SWIM_CHECK(assets.GetStatus(mesh).State == AssetLoadState::Resident);
	SWIM_CHECK_EQUAL(assets.GetStatus(mesh).Dependencies.size(), std::size_t{ 1 });
	SWIM_REQUIRE(assets.Resolve(mesh) != nullptr);
	SWIM_CHECK_EQUAL(assets.Resolve(mesh)->VertexCount, 36);
	SWIM_CHECK(assets.FindByContentHash(meshHash) == mesh.GetId());
	SWIM_CHECK_EQUAL(assets.GetDependents(texture.GetId()).size(), std::size_t{ 1 });

	assets.Shutdown();
}

SWIM_TEST("Assets.AssetSystem", "ContentReplacementKeepsStableHandleIdentity")
{
	using namespace Swim::Assets;

	AssetSystem assets;
	SWIM_REQUIRE(assets.Initialize());

	auto texture = assets.Declare<TestTextureAsset>("Textures/CrateAlbedo.ktx2");
	auto mesh = assets.Declare<TestMeshAsset>("Meshes/Crate.sasset");

	const AssetId dependencies[] = { texture.GetId() };
	SWIM_REQUIRE(assets.Publish(mesh, TestMeshAsset{ 36 }, ComputeContentHash("compiled-mesh-payload"), dependencies));

	const ContentHash textureHashA = ComputeContentHash("texture-a");
	const ContentHash textureHashB = ComputeContentHash("texture-b");
	SWIM_REQUIRE(assets.Publish(texture, TestTextureAsset{ 64 }, textureHashA));

	const ContentHash dependencyRevisionA = assets.ComputeDependencyRevisionHash(mesh.GetId());
	const std::uint32_t textureGeneration = texture.GetGeneration();

	SWIM_REQUIRE(assets.Publish(texture, TestTextureAsset{ 128 }, textureHashB));
	SWIM_REQUIRE(assets.Resolve(texture) != nullptr);
	SWIM_CHECK_EQUAL(assets.Resolve(texture)->Width, 128);

	const ContentHash dependencyRevisionB = assets.ComputeDependencyRevisionHash(mesh.GetId());
	SWIM_CHECK_EQUAL(texture.GetGeneration(), textureGeneration);
	SWIM_CHECK(dependencyRevisionA != dependencyRevisionB);
	SWIM_CHECK(!dependencyRevisionA.IsZero());
	SWIM_CHECK(!dependencyRevisionB.IsZero());

	assets.Shutdown();
}

SWIM_TEST("Assets.AssetSystem", "OneAssetIdCannotAliasTwoRuntimeTypes")
{
	using namespace Swim::Assets;

	AssetSystem assets;
	SWIM_REQUIRE(assets.Initialize());

	auto mesh = assets.Declare<TestMeshAsset>("Meshes/Crate.sasset");
	SWIM_CHECK(assets.Declare<TestMeshAsset>("Meshes/Crate.sasset") == mesh);
	SWIM_CHECK_THROWS((void)assets.Declare<TestTextureAsset>(mesh.GetId()), std::logic_error);

	assets.Shutdown();
}

SWIM_TEST("Assets.AssetSystem", "UnloadFailAndForgetTransitions")
{
	using namespace Swim::Assets;

	AssetSystem assets;
	SWIM_REQUIRE(assets.Initialize());

	auto mesh = assets.Declare<TestMeshAsset>("Meshes/Crate.sasset");
	SWIM_REQUIRE(assets.Publish(mesh, TestMeshAsset{ 36 }, ComputeContentHash("compiled-mesh-payload")));

	SWIM_CHECK(assets.Unload(mesh));
	SWIM_CHECK(assets.IsCurrent(mesh));
	SWIM_CHECK(assets.Resolve(mesh) == nullptr);

	SWIM_CHECK(assets.BeginLoading(mesh));
	SWIM_CHECK(assets.Fail(mesh, AssetError{ AssetErrorCode::InvalidData, "bad mesh payload" }));
	SWIM_CHECK(assets.GetStatus(mesh).State == AssetLoadState::Failed);
	SWIM_CHECK(assets.GetStatus(mesh).Error.Code == AssetErrorCode::InvalidData);

	const std::uint32_t oldGeneration = mesh.GetGeneration();
	SWIM_CHECK(assets.Forget(mesh));
	SWIM_CHECK(!assets.IsCurrent(mesh));

	auto replacement = assets.Declare<TestMeshAsset>("Meshes/Crate.sasset");
	SWIM_CHECK(replacement.GetGeneration() != oldGeneration);
	SWIM_CHECK(replacement.GetId() == mesh.GetId());

	assets.Shutdown();
}

SWIM_TEST("Assets.AssetSystem", "SelfAndCyclicDependenciesAreRejected")
{
	using namespace Swim::Assets;

	AssetSystem assets;
	SWIM_REQUIRE(assets.Initialize());

	auto mesh = assets.Declare<TestMeshAsset>("Meshes/Crate.sasset");
	const AssetId selfDependency[] = { mesh.GetId() };
	SWIM_CHECK(!assets.SetDependencies(mesh, selfDependency));

	auto dependentTexture = assets.Declare<TestTextureAsset>("Textures/Dependent.ktx2");
	const AssetId meshDependency[] = { mesh.GetId() };
	SWIM_CHECK(assets.SetDependencies(dependentTexture, meshDependency));

	const AssetId cyclicDependency[] = { dependentTexture.GetId() };
	SWIM_CHECK(!assets.SetDependencies(mesh, cyclicDependency));

	assets.Shutdown();
}

SWIM_TEST("Assets.AssetSystem", "RuntimeMaterialAndModelResidency")
{
	using namespace Swim::Assets;

	AssetSystem assets;
	SWIM_REQUIRE(assets.Initialize());

	auto runtimeTexture = assets.Declare<TextureAsset>("Runtime/Textures/Crate.ktx2");
	auto runtimeSampler = assets.Declare<SamplerAsset>("Runtime/Samplers/LinearRepeat.sasset");
	auto materialTemplate = assets.Declare<MaterialTemplateAsset>("Runtime/Materials/PbrTemplate.sasset");
	auto materialInstance = assets.Declare<MaterialInstanceAsset>("Runtime/Materials/Crate.sasset");
	auto runtimeMesh = assets.Declare<MeshAsset>("Runtime/Meshes/Crate.sasset");
	auto runtimeModel = assets.Declare<ModelAsset>("Runtime/Models/Crate.sasset");

	MaterialInstanceAsset material{};
	material.Template = materialTemplate;
	material.Textures.push_back(MaterialTextureBinding{ "BaseColor", runtimeTexture, runtimeSampler });
	SWIM_REQUIRE(assets.Publish(materialInstance, std::move(material)));

	ModelAsset model{};
	ModelNode node{};
	node.Name = "Crate";
	node.Mesh = runtimeMesh;
	node.Materials.push_back(materialInstance);
	model.Nodes.push_back(std::move(node));
	model.Roots.push_back(0);
	SWIM_REQUIRE(assets.Publish(runtimeModel, std::move(model)));

	SWIM_REQUIRE(assets.Resolve(runtimeModel) != nullptr);
	SWIM_CHECK(assets.Resolve(runtimeModel)->Nodes[0].Mesh == runtimeMesh);
	SWIM_CHECK(assets.Resolve(runtimeModel)->Nodes[0].Materials[0] == materialInstance);

	assets.Shutdown();
	SWIM_CHECK(!assets.IsRunning());
}
