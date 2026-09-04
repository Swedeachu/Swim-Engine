#include "Engine/Assets/AssetSystem.h"
#include "Engine/Assets/MeshAsset.h"
#include "Engine/Assets/ModelAsset.h"
#include "Tests/Fixtures/DracoTriangleFixture.h"
#include "Tests/Framework/Test.h"
#include "Tools/AssetCompiler/DevelopmentAssetPipeline.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>

namespace
{

	std::filesystem::path CookedObjectPath(const std::filesystem::path& root, Swim::Assets::AssetId id)
	{
		std::ostringstream name;
		name << std::hex << std::setfill('0') << std::setw(16) << id.Value << ".sasset";
		return root / "Cooked" / ".objects" / name.str();
	}

	// A single-triangle glTF whose positions live in an external .bin, so the
	// external dependency can be edited independently of the .gltf file.
	void WriteTriangleFixture(const std::filesystem::path& root, float firstX)
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

		const std::array<float, 9> positions
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

	// Owns a scratch asset root so each case cooks in isolation and cleans up
	// even when a requirement aborts the case.
	class ScopedAssetRoot
	{

	public:

		explicit ScopedAssetRoot(std::string_view name)
			: path(std::filesystem::temp_directory_path() / name)
		{
			std::error_code ignored;
			std::filesystem::remove_all(path, ignored);
		}

		~ScopedAssetRoot()
		{
			std::error_code ignored;
			std::filesystem::remove_all(path, ignored);
		}

		ScopedAssetRoot(const ScopedAssetRoot&) = delete;
		ScopedAssetRoot& operator=(const ScopedAssetRoot&) = delete;

		const std::filesystem::path& Path() const
		{
			return path;
		}

	private:

		std::filesystem::path path;

	};

	std::string FirstErrorOr(const Swim::AssetCompiler::DevelopmentAssetBootstrapResult& result, std::string fallback)
	{
		return result.Errors.empty() ? std::move(fallback) : result.Errors.front().Message;
	}

}

SWIM_TEST("AssetCompiler.DevelopmentAssetPipeline", "MissingCookedOutputTriggersAnInitialCook")
{
	using namespace Swim::AssetCompiler;
	using namespace Swim::Assets;

	const ScopedAssetRoot root("swim-dev-asset-bootstrap-initial-test");
	WriteTriangleFixture(root.Path(), 0.0f);

	AssetSystem assets;
	SWIM_REQUIRE(assets.Initialize());

	const DevelopmentAssetBootstrapResult first = RunDevelopmentAssetBootstrap(root.Path(), assets);
	SWIM_REQUIRE_MESSAGE(first.Succeeded(), FirstErrorOr(first, "first bootstrap failed"));
	SWIM_CHECK_EQUAL(first.Stats.SourcesDiscovered, std::size_t{ 1 });
	SWIM_CHECK_EQUAL(first.Stats.SourcesCooked, std::size_t{ 1 });
	SWIM_CHECK_EQUAL(first.Stats.RootModelsLoaded, std::size_t{ 1 });

	const auto modelHandle = assets.Find<ModelAsset>("Models/Triangle.model");
	SWIM_REQUIRE(modelHandle.IsValid());

	const ModelAsset* model = assets.Resolve(modelHandle);
	SWIM_REQUIRE(model != nullptr);
	SWIM_REQUIRE(model->Nodes.size() == 1);
	SWIM_REQUIRE(model->Nodes[0].Mesh.IsValid());
	SWIM_CHECK(assets.Resolve(model->Nodes[0].Mesh) != nullptr);

	assets.Shutdown();
}

SWIM_TEST("AssetCompiler.DevelopmentAssetPipeline", "UnchangedSourcesSkipTheCook")
{
	using namespace Swim::AssetCompiler;
	using namespace Swim::Assets;

	const ScopedAssetRoot root("swim-dev-asset-bootstrap-unchanged-test");
	WriteTriangleFixture(root.Path(), 0.0f);

	AssetSystem assets;
	SWIM_REQUIRE(assets.Initialize());
	SWIM_REQUIRE(RunDevelopmentAssetBootstrap(root.Path(), assets).Succeeded());

	const auto modelHandle = assets.Find<ModelAsset>("Models/Triangle.model");
	SWIM_REQUIRE(modelHandle.IsValid());
	const AssetHandle<MeshAsset> meshHandle = assets.Resolve(modelHandle)->Nodes[0].Mesh;

	const DevelopmentAssetBootstrapResult second = RunDevelopmentAssetBootstrap(root.Path(), assets);
	SWIM_REQUIRE_MESSAGE(second.Succeeded(), FirstErrorOr(second, "second bootstrap failed"));
	SWIM_CHECK_EQUAL(second.Stats.SourcesCurrent, std::size_t{ 1 });
	SWIM_CHECK_EQUAL(second.Stats.SourcesCooked, std::size_t{ 0 });

	const ModelAsset* model = assets.Resolve(modelHandle);
	SWIM_REQUIRE(model != nullptr);
	SWIM_REQUIRE(model->Nodes.size() == 1);
	SWIM_CHECK(model->Nodes[0].Mesh == meshHandle);

	assets.Shutdown();
}

SWIM_TEST("AssetCompiler.DevelopmentAssetPipeline", "MissingNestedDependencyInvalidatesTheRoot")
{
	using namespace Swim::AssetCompiler;
	using namespace Swim::Assets;

	const ScopedAssetRoot root("swim-dev-asset-bootstrap-repair-test");
	WriteTriangleFixture(root.Path(), 0.0f);

	AssetSystem assets;
	SWIM_REQUIRE(assets.Initialize());
	SWIM_REQUIRE(RunDevelopmentAssetBootstrap(root.Path(), assets).Succeeded());

	const auto modelHandle = assets.Find<ModelAsset>("Models/Triangle.model");
	SWIM_REQUIRE(modelHandle.IsValid());
	const AssetHandle<MeshAsset> meshHandle = assets.Resolve(modelHandle)->Nodes[0].Mesh;

	std::error_code ignored;
	std::filesystem::remove(CookedObjectPath(root.Path(), meshHandle.GetId()), ignored);

	const DevelopmentAssetBootstrapResult repaired = RunDevelopmentAssetBootstrap(root.Path(), assets);
	SWIM_REQUIRE_MESSAGE(repaired.Succeeded(), FirstErrorOr(repaired, "dependency repair bootstrap failed"));
	SWIM_CHECK_EQUAL(repaired.Stats.SourcesCooked, std::size_t{ 1 });

	const ModelAsset* model = assets.Resolve(modelHandle);
	SWIM_REQUIRE(model != nullptr);
	SWIM_REQUIRE(model->Nodes.size() == 1);
	SWIM_CHECK(model->Nodes[0].Mesh == meshHandle);

	assets.Shutdown();
}

SWIM_TEST("AssetCompiler.DevelopmentAssetPipeline", "EditingAnExternalBufferRecooksTheModel")
{
	using namespace Swim::AssetCompiler;
	using namespace Swim::Assets;

	const ScopedAssetRoot root("swim-dev-asset-bootstrap-external-edit-test");
	WriteTriangleFixture(root.Path(), 0.0f);

	AssetSystem assets;
	SWIM_REQUIRE(assets.Initialize());
	SWIM_REQUIRE(RunDevelopmentAssetBootstrap(root.Path(), assets).Succeeded());

	const auto modelHandle = assets.Find<ModelAsset>("Models/Triangle.model");
	SWIM_REQUIRE(modelHandle.IsValid());
	const AssetHandle<MeshAsset> meshHandle = assets.Resolve(modelHandle)->Nodes[0].Mesh;

	// Only the external .bin changes; the .gltf file is byte-identical.
	WriteTriangleFixture(root.Path(), 0.25f);

	const DevelopmentAssetBootstrapResult third = RunDevelopmentAssetBootstrap(root.Path(), assets);
	SWIM_REQUIRE_MESSAGE(third.Succeeded(), FirstErrorOr(third, "external edit bootstrap failed"));
	SWIM_CHECK_EQUAL(third.Stats.SourcesCooked, std::size_t{ 1 });

	const ModelAsset* recookedModel = assets.Resolve(modelHandle);
	SWIM_REQUIRE(recookedModel != nullptr);
	SWIM_REQUIRE(recookedModel->Nodes.size() == 1);
	SWIM_CHECK(recookedModel->Nodes[0].Mesh == meshHandle);

	const MeshAsset* recookedMesh = assets.Resolve(recookedModel->Nodes[0].Mesh);
	SWIM_REQUIRE(recookedMesh != nullptr);
	SWIM_REQUIRE(recookedMesh->VertexBytes.size() >= sizeof(float));

	float firstX = 0.0f;
	std::memcpy(&firstX, recookedMesh->VertexBytes.data(), sizeof(float));
	SWIM_CHECK_EQUAL(firstX, 0.25f);

	assets.Shutdown();
}

SWIM_TEST("AssetCompiler.DevelopmentAssetPipeline", "DracoCompressedSourcesCookAndLoad")
{
	using namespace Swim::AssetCompiler;
	using namespace Swim::Assets;

	const ScopedAssetRoot root("swim-dev-asset-bootstrap-draco-test");
	Swim::Tests::WriteDracoTriangleFixture(root.Path() / "Models", "Draco");

	AssetSystem assets;
	SWIM_REQUIRE(assets.Initialize());

	const DevelopmentAssetBootstrapResult draco = RunDevelopmentAssetBootstrap(root.Path(), assets);
	SWIM_REQUIRE_MESSAGE(draco.Succeeded(), FirstErrorOr(draco, "Draco bootstrap failed"));
	SWIM_CHECK_EQUAL(draco.Stats.SourcesDiscovered, std::size_t{ 1 });
	SWIM_CHECK_EQUAL(draco.Stats.SourcesCooked, std::size_t{ 1 });
	SWIM_CHECK_EQUAL(draco.Stats.SourcesSkippedUnsupported, std::size_t{ 0 });
	SWIM_CHECK_EQUAL(draco.Stats.RootModelsLoaded, std::size_t{ 1 });
	SWIM_CHECK(assets.Find<ModelAsset>("Models/Draco.model").IsValid());

	assets.Shutdown();
}
