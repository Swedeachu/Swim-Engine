#include "Engine/Assets/AssetSystem.h"
#include "Engine/Assets/MeshAsset.h"
#include "Engine/Assets/ModelAsset.h"
#include "Tools/AssetCompiler/DevelopmentAssetPipeline.h"

#include <array>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string_view>

namespace
{
	void Require(bool condition, const char* message)
	{
		if (!condition)
		{
			std::cerr << "development asset pipeline test failed: " << message << '\n';
			std::exit(1);
		}
	}


	std::filesystem::path CookedObjectPath(const std::filesystem::path& root, Swim::Assets::AssetId id)
	{
		std::ostringstream name;
		name << std::hex << std::setfill('0') << std::setw(16) << id.Value << ".sasset";
		return root / "Cooked" / ".objects" / name.str();
	}

	void WriteFixture(const std::filesystem::path& root, float firstX)
	{
		std::filesystem::create_directories(root / "Models");
		static constexpr std::string_view Gltf = R"json({
			"asset":{"version":"2.0"},
			"scene":0,
			"scenes":[{"nodes":[0]}],
			"nodes":[{"name":"TriangleNode","mesh":0}],
			"meshes":[{"name":"TriangleMesh","primitives":[{"attributes":{"POSITION":0},"indices":1}]}],
			"buffers":[{"byteLength":42,"uri":"triangle.bin"}],
			"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":6}],
			"accessors":[
				{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},
				{"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}
			]
		})json";
		{
			std::ofstream file(root / "Models" / "Triangle.gltf", std::ios::binary | std::ios::trunc);
			file.write(Gltf.data(), static_cast<std::streamsize>(Gltf.size()));
		}

		std::array<float, 9> positions
		{
			firstX, 0.0f, 0.0f,
			1.0f, 0.0f, 0.0f,
			0.0f, 1.0f, 0.0f
		};
		const std::array<std::uint16_t, 3> indices{ 0, 1, 2 };
		std::ofstream file(root / "Models" / "triangle.bin", std::ios::binary | std::ios::trunc);
		file.write(reinterpret_cast<const char*>(positions.data()), sizeof(positions));
		file.write(reinterpret_cast<const char*>(indices.data()), sizeof(indices));
	}

	void WriteUnsupportedDracoFixture(const std::filesystem::path& root)
	{
		std::filesystem::create_directories(root / "Models");
		static constexpr std::string_view Gltf = R"json({
			"asset":{"version":"2.0"},
			"extensionsUsed":["KHR_draco_mesh_compression"],
			"extensionsRequired":["KHR_draco_mesh_compression"],
			"scene":0,
			"scenes":[{"nodes":[0]}],
			"nodes":[{"mesh":0}],
			"meshes":[{"primitives":[{
				"attributes":{"POSITION":0},
				"indices":1,
				"extensions":{"KHR_draco_mesh_compression":{"bufferView":0,"attributes":{"POSITION":0}}}
			}]}],
			"buffers":[{"byteLength":4,"uri":"data:application/octet-stream;base64,AAAAAA=="}],
			"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":4}],
			"accessors":[
				{"componentType":5126,"count":3,"type":"VEC3"},
				{"componentType":5123,"count":3,"type":"SCALAR"}
			]
		})json";
		std::ofstream file(root / "Models" / "Draco.gltf", std::ios::binary | std::ios::trunc);
		file.write(Gltf.data(), static_cast<std::streamsize>(Gltf.size()));
	}
}

int main()
{
	using namespace Swim::AssetCompiler;
	using namespace Swim::Assets;

	const std::filesystem::path root = std::filesystem::temp_directory_path() / "swim-dev-asset-bootstrap-test";
	std::error_code ignored;
	std::filesystem::remove_all(root, ignored);
	WriteFixture(root, 0.0f);

	AssetSystem assets;
	Require(assets.Initialize(), "AssetSystem initialized");
	const DevelopmentAssetBootstrapResult first = RunDevelopmentAssetBootstrap(root, assets);
	Require(first.Succeeded(), first.Errors.empty() ? "first bootstrap failed" : first.Errors.front().Message.c_str());
	Require(first.Stats.SourcesDiscovered == 1, "one loose glTF discovered");
	Require(first.Stats.SourcesCooked == 1, "missing .sasset triggers initial cook");
	Require(first.Stats.RootModelsLoaded == 1, "cooked root model auto loads");
	const auto modelHandle = assets.Find<ModelAsset>("Models/Triangle.model");
	Require(modelHandle.IsValid(), "auto-loaded model registered by logical path");
	const ModelAsset* model = assets.Resolve(modelHandle);
	Require(model != nullptr && model->Nodes.size() == 1, "auto-loaded model resolves");
	Require(assets.Resolve(model->Nodes[0].Mesh) != nullptr, "auto-loaded model dependency graph is resident");

	const DevelopmentAssetBootstrapResult second = RunDevelopmentAssetBootstrap(root, assets);
	Require(second.Succeeded(), "second bootstrap succeeds");
	Require(second.Stats.SourcesCurrent == 1 && second.Stats.SourcesCooked == 0, "unchanged source skips fastgltf/meshoptimizer cook");

	const std::filesystem::path meshObject = CookedObjectPath(root, model->Nodes[0].Mesh.GetId());
	std::filesystem::remove(meshObject, ignored);
	const DevelopmentAssetBootstrapResult repaired = RunDevelopmentAssetBootstrap(root, assets);
	Require(repaired.Succeeded(), repaired.Errors.empty() ? "dependency repair bootstrap failed" : repaired.Errors.front().Message.c_str());
	Require(repaired.Stats.SourcesCooked == 1, "missing nested cooked dependency invalidates the root and recooks");

	WriteFixture(root, 0.25f);
	const DevelopmentAssetBootstrapResult third = RunDevelopmentAssetBootstrap(root, assets);
	Require(third.Succeeded(), third.Errors.empty() ? "third bootstrap failed" : third.Errors.front().Message.c_str());
	Require(third.Stats.SourcesCooked == 1, "external .bin edit invalidates source provenance and recooks");
	const ModelAsset* recookedModel = assets.Resolve(assets.Find<ModelAsset>("Models/Triangle.model"));
	Require(recookedModel != nullptr, "recooked model remains resident under stable identity");
	const MeshAsset* recookedMesh = assets.Resolve(recookedModel->Nodes[0].Mesh);
	Require(recookedMesh != nullptr && !recookedMesh->VertexBytes.empty(), "recooked mesh remains resident");
	float firstX = 0.0f;
	std::memcpy(&firstX, recookedMesh->VertexBytes.data(), sizeof(float));
	Require(firstX == 0.25f, "recooked external buffer content replaces runtime residency");

	assets.Shutdown();
	std::filesystem::remove_all(root, ignored);

	const std::filesystem::path unsupportedRoot = std::filesystem::temp_directory_path() / "swim-dev-asset-bootstrap-unsupported-test";
	std::filesystem::remove_all(unsupportedRoot, ignored);
	WriteUnsupportedDracoFixture(unsupportedRoot);
	AssetSystem unsupportedAssets;
	Require(unsupportedAssets.Initialize(), "unsupported AssetSystem initialized");
	const DevelopmentAssetBootstrapResult unsupported = RunDevelopmentAssetBootstrap(unsupportedRoot, unsupportedAssets);
	Require(unsupported.Succeeded(), "unsupported source is a deliberate skip, not a bootstrap error");
	Require(unsupported.Stats.SourcesDiscovered == 1, "unsupported source is still discovered");
	Require(unsupported.Stats.SourcesSkippedUnsupported == 1, "unsupported Draco source is counted as skipped");
	Require(unsupported.Stats.SourcesCooked == 0, "unsupported Draco source is not cooked");
	unsupportedAssets.Shutdown();
	std::filesystem::remove_all(unsupportedRoot, ignored);
	return 0;
}
