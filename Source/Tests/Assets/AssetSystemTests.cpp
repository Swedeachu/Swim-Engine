#include "Engine/Assets/AssetSystem.h"
#include "Engine/Assets/MaterialAsset.h"
#include "Engine/Assets/ModelAsset.h"

#include <cstdlib>
#include <iostream>
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

	void Require(bool condition, const char* message)
	{
		if (!condition)
		{
			std::cerr << "Asset system test failed: " << message << '\n';
			std::exit(1);
		}
	}

}

int main()
{
	using namespace Swim::Assets;

	Require(
		ComputeContentHash("").ToHex() == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
		"SHA-256 empty vector"
	);
	Require(
		ComputeContentHash("abc").ToHex() == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
		"SHA-256 abc vector"
	);
	Require(
		ContentHash::FromHex(ComputeContentHash("abc").ToHex()) == ComputeContentHash("abc"),
		"content hash hex roundtrip"
	);

	Require(NormalizeAssetPath("Models\\Props/../Props//Crate.glb") == "Models/Props/Crate.glb", "logical path normalization");
	bool rejectedEscapingPath = false;
	try
	{
		(void)NormalizeAssetPath("../Outside.asset");
	}
	catch (const std::invalid_argument&)
	{
		rejectedEscapingPath = true;
	}
	Require(rejectedEscapingPath, "asset path cannot escape root");

	AssetDatabase database;
	const AssetId databaseId = database.GetOrCreate("Models/Props/Crate.glb");
	Require(databaseId == database.GetOrCreate("Models\\Props\\Crate.glb"), "path identity is deterministic");
	Require(database.Rebind(databaseId, "Models/Environment/Crate.glb"), "path rebind keeps identity");
	Require(database.FindId("Models/Environment/Crate.glb") == databaseId, "rebound path lookup");
	Require(database.FindPath(databaseId) == std::optional<std::string>("Models/Environment/Crate.glb"), "id lookup after rebind");

	AssetSystem assets;
	Require(assets.Initialize(), "asset system initialization");

	auto texture = assets.Declare<TestTextureAsset>("Textures/CrateAlbedo.ktx2");
	auto mesh = assets.Declare<TestMeshAsset>("Meshes/Crate.sasset");
	Require(mesh.IsValid(), "typed handle exists before residency");
	Require(assets.Resolve(mesh) == nullptr, "unloaded handle does not resolve");
	Require(assets.GetStatus(mesh).State == AssetLoadState::Unloaded, "initial load state");
	Require(assets.Queue(mesh), "queue transition");
	Require(assets.BeginLoading(mesh), "loading transition");

	const AssetId dependencies[] = { texture.GetId(), texture.GetId() };
	const ContentHash meshHash = ComputeContentHash("compiled-mesh-payload");
	Require(assets.Publish(mesh, TestMeshAsset{ 36 }, meshHash, dependencies), "publish resident asset");
	Require(assets.GetStatus(mesh).State == AssetLoadState::Resident, "resident load state");
	Require(assets.GetStatus(mesh).Dependencies.size() == 1, "dependency deduplication");
	Require(assets.Resolve(mesh) && assets.Resolve(mesh)->VertexCount == 36, "typed resident resolution");
	Require(assets.FindByContentHash(meshHash) == mesh.GetId(), "content hash index");
	Require(assets.GetDependents(texture.GetId()).size() == 1, "reverse dependency graph");

	const ContentHash textureHashA = ComputeContentHash("texture-a");
	const ContentHash textureHashB = ComputeContentHash("texture-b");
	Require(assets.Publish(texture, TestTextureAsset{ 64 }, textureHashA), "publish dependency content");
	const ContentHash dependencyRevisionA = assets.ComputeDependencyRevisionHash(mesh.GetId());
	const std::uint32_t textureGeneration = texture.GetGeneration();
	Require(assets.Publish(texture, TestTextureAsset{ 128 }, textureHashB), "replace dependency content under stable identity");
	const ContentHash dependencyRevisionB = assets.ComputeDependencyRevisionHash(mesh.GetId());
	Require(texture.GetGeneration() == textureGeneration, "content replacement preserves stable handle generation");
	Require(dependencyRevisionA != dependencyRevisionB, "dependency revision tracks stable-handle content replacement");
	Require(!dependencyRevisionA.IsZero() && !dependencyRevisionB.IsZero(), "declared dependency graph has a revision hash");

	auto sameMesh = assets.Declare<TestMeshAsset>("Meshes/Crate.sasset");
	Require(sameMesh == mesh, "redeclare returns current typed generation");

	bool rejectedTypeAlias = false;
	try
	{
		(void)assets.Declare<TestTextureAsset>(mesh.GetId());
	}
	catch (const std::logic_error&)
	{
		rejectedTypeAlias = true;
	}
	Require(rejectedTypeAlias, "one AssetId cannot alias two runtime asset types");

	Require(assets.Unload(mesh), "unload resident asset");
	Require(assets.IsCurrent(mesh), "unload does not stale identity handle");
	Require(assets.Resolve(mesh) == nullptr, "unloaded asset no longer resolves");

	Require(assets.BeginLoading(mesh), "reload transition");
	Require(assets.Fail(mesh, AssetError{ AssetErrorCode::InvalidData, "bad mesh payload" }), "failure transition");
	Require(assets.GetStatus(mesh).State == AssetLoadState::Failed, "failed state");
	Require(assets.GetStatus(mesh).Error.Code == AssetErrorCode::InvalidData, "explicit failure code");

	const std::uint32_t oldGeneration = mesh.GetGeneration();
	Require(assets.Forget(mesh), "forget invalidates handle generation");
	Require(!assets.IsCurrent(mesh), "forgotten handle is stale");
	auto replacement = assets.Declare<TestMeshAsset>("Meshes/Crate.sasset");
	Require(replacement.GetGeneration() != oldGeneration, "redeclare increments stale generation");
	Require(replacement.GetId() == mesh.GetId(), "path identity survives runtime generation changes");

	const AssetId invalidDependencies[] = { replacement.GetId() };
	Require(!assets.SetDependencies(replacement, invalidDependencies), "self dependency is rejected");

	auto dependentTexture = assets.Declare<TestTextureAsset>("Textures/Dependent.ktx2");
	const AssetId meshDependency[] = { replacement.GetId() };
	Require(assets.SetDependencies(dependentTexture, meshDependency), "forward dependency accepted");
	const AssetId cyclicDependency[] = { dependentTexture.GetId() };
	Require(!assets.SetDependencies(replacement, cyclicDependency), "dependency cycle is rejected");

	auto runtimeTexture = assets.Declare<Swim::Assets::TextureAsset>("Runtime/Textures/Crate.ktx2");
	auto runtimeSampler = assets.Declare<Swim::Assets::SamplerAsset>("Runtime/Samplers/LinearRepeat.sasset");
	auto materialTemplate = assets.Declare<Swim::Assets::MaterialTemplateAsset>("Runtime/Materials/PbrTemplate.sasset");
	auto materialInstance = assets.Declare<Swim::Assets::MaterialInstanceAsset>("Runtime/Materials/Crate.sasset");
	auto runtimeMesh = assets.Declare<Swim::Assets::MeshAsset>("Runtime/Meshes/Crate.sasset");
	auto runtimeModel = assets.Declare<Swim::Assets::ModelAsset>("Runtime/Models/Crate.sasset");

	Swim::Assets::MaterialInstanceAsset material{};
	material.Template = materialTemplate;
	material.Textures.push_back(Swim::Assets::MaterialTextureBinding{ "BaseColor", runtimeTexture, runtimeSampler });
	Require(assets.Publish(materialInstance, std::move(material)), "material instance residency");

	Swim::Assets::ModelAsset model{};
	Swim::Assets::ModelNode node{};
	node.Name = "Crate";
	node.Mesh = runtimeMesh;
	node.Materials.push_back(materialInstance);
	model.Nodes.push_back(std::move(node));
	model.Roots.push_back(0);
	Require(assets.Publish(runtimeModel, std::move(model)), "model residency");
	Require(assets.Resolve(runtimeModel)->Nodes[0].Mesh == runtimeMesh, "model stores independent mesh identity");
	Require(assets.Resolve(runtimeModel)->Nodes[0].Materials[0] == materialInstance, "model stores independent material identity");

	assets.Shutdown();
	Require(!assets.IsRunning(), "asset system shutdown");
	return 0;
}
