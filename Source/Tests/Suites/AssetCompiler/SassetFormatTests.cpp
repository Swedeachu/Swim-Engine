#include "Engine/Assets/AssetDatabase.h"
#include "Engine/Assets/AssetSystem.h"
#include "Engine/Assets/MeshAsset.h"
#include "Engine/Assets/SassetFormat.h"
#include "Tests/Framework/Test.h"
#include "Tools/AssetCompiler/SassetWriter.h"

#include <cstddef>
#include <string>
#include <vector>

namespace
{

	Swim::Assets::MeshAsset MakeTriangleMesh()
	{
		using namespace Swim::Assets;

		MeshAsset mesh;
		mesh.IndexFormat = IndexElementFormat::UInt32;
		mesh.VertexStreams.push_back({ 12, 0, 36 });
		mesh.VertexAttributes.push_back({ VertexSemantic::Position, VertexElementFormat::Float32x3, 0, 0 });
		mesh.Primitives.push_back({ 0, 3, 0, 0, {} });
		mesh.Lods.push_back({ 0, 1, 1.0f });
		mesh.VertexBytes.resize(36, std::byte{ 0x2A });
		mesh.IndexBytes.resize(12, std::byte{ 0x01 });
		return mesh;
	}

	struct BuiltMeshSasset
	{
		Swim::AssetCompiler::SassetBuildInput Input;
		std::vector<std::byte> Bytes;
		Swim::Assets::MeshAsset Mesh;
	};

	BuiltMeshSasset BuildMeshSasset()
	{
		using namespace Swim::Assets;
		using namespace Swim::AssetCompiler;

		AssetDatabase ids;

		BuiltMeshSasset built;
		built.Mesh = MakeTriangleMesh();
		built.Input.Type = SassetAssetType::Mesh;
		built.Input.Id = ids.GetOrCreate("Models/Test.model#mesh/0");
		built.Input.LogicalPath = "Models/Test.model#mesh/0";
		built.Input.CompilerProfileHash = ComputeContentHash("compiler-profile");
		built.Input.SourceDependencies.push_back({ "Models/Test.glb", ComputeContentHash("source") });
		built.Input.SourceHash = ComputeSourceGraphHash(built.Input.SourceDependencies);
		built.Input.Payload = SerializeAssetPayload(built.Mesh);

		const SassetBuildResult result = BuildSasset(built.Input);
		SWIM_REQUIRE_MESSAGE(static_cast<bool>(result), result.Error.Message);
		built.Bytes = result.Bytes;
		return built;
	}

}

SWIM_TEST("AssetCompiler.SassetFormat", "MetadataRoundTripsThroughTheContainer")
{
	using namespace Swim::Assets;

	const BuiltMeshSasset built = BuildMeshSasset();
	const SassetParseResult parsed = ParseSasset(built.Bytes);
	SWIM_REQUIRE_MESSAGE(static_cast<bool>(parsed), parsed.Error.Message);

	SWIM_CHECK_EQUAL(parsed.Metadata.SchemaVersion, SassetSchemaVersion);
	SWIM_CHECK(parsed.Metadata.Type == SassetAssetType::Mesh);
	SWIM_CHECK(parsed.Metadata.Id == built.Input.Id);
	SWIM_CHECK_EQUAL(parsed.Metadata.LogicalPath, built.Input.LogicalPath);
	SWIM_CHECK_EQUAL(parsed.Metadata.SourceDependencies.size(), std::size_t{ 1 });
	SWIM_CHECK(parsed.Metadata.SourceHash == built.Input.SourceHash);
}

SWIM_TEST("AssetCompiler.SassetFormat", "PayloadLoadsIntoRuntimeResidency")
{
	using namespace Swim::Assets;
	using namespace Swim::AssetCompiler;

	const BuiltMeshSasset built = BuildMeshSasset();

	AssetSystem runtime;
	SWIM_REQUIRE(runtime.Initialize());

	const SassetLoadResult loaded = LoadSasset(runtime, built.Bytes);
	SWIM_REQUIRE_MESSAGE(static_cast<bool>(loaded), loaded.Error.Message);

	const auto handle = runtime.Find<MeshAsset>(built.Input.LogicalPath);
	SWIM_REQUIRE(handle.IsValid());

	const MeshAsset* resolved = runtime.Resolve(handle);
	SWIM_REQUIRE(resolved != nullptr);
	SWIM_CHECK(resolved->VertexBytes == built.Mesh.VertexBytes);
	SWIM_CHECK(resolved->IndexBytes == built.Mesh.IndexBytes);

	runtime.Shutdown();
}

SWIM_TEST("AssetCompiler.SassetFormat", "ChunkHashesCatchPayloadCorruption")
{
	using namespace Swim::Assets;

	const BuiltMeshSasset built = BuildMeshSasset();

	std::vector<std::byte> corrupted = built.Bytes;
	SWIM_REQUIRE(!corrupted.empty());
	corrupted.back() ^= std::byte{ 0x01 };

	SWIM_CHECK(ParseSasset(corrupted).Error.Code == SassetErrorCode::HashMismatch);
}
