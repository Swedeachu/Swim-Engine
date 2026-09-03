#include "Engine/Assets/AssetDatabase.h"
#include "Engine/Assets/AssetSystem.h"
#include "Engine/Assets/MeshAsset.h"
#include "Engine/Assets/SassetFormat.h"
#include "Tools/AssetCompiler/SassetWriter.h"

#include <cstdlib>
#include <iostream>
#include <vector>

namespace
{
	void Require(bool condition, const char* message)
	{
		if (!condition)
		{
			std::cerr << ".sasset format test failed: " << message << '\n';
			std::exit(1);
		}
	}
}

int main()
{
	using namespace Swim::Assets;
	using namespace Swim::AssetCompiler;

	AssetDatabase ids;
	const AssetId meshId = ids.GetOrCreate("Models/Test.model#mesh/0");
	MeshAsset mesh;
	mesh.IndexFormat = IndexElementFormat::UInt32;
	mesh.VertexStreams.push_back({ 12, 0, 36 });
	mesh.VertexAttributes.push_back({ VertexSemantic::Position, VertexElementFormat::Float32x3, 0, 0 });
	mesh.Primitives.push_back({ 0, 3, 0, 0, {} });
	mesh.Lods.push_back({ 0, 1, 1.0f });
	mesh.VertexBytes.resize(36, std::byte{ 0x2A });
	mesh.IndexBytes.resize(12, std::byte{ 0x01 });

	SassetBuildInput input;
	input.Type = SassetAssetType::Mesh;
	input.Id = meshId;
	input.LogicalPath = "Models/Test.model#mesh/0";
	input.CompilerProfileHash = ComputeContentHash("compiler-profile");
	input.SourceDependencies.push_back({ "Models/Test.glb", ComputeContentHash("source") });
	input.SourceHash = ComputeSourceGraphHash(input.SourceDependencies);
	input.Payload = SerializeAssetPayload(mesh);

	const SassetBuildResult built = BuildSasset(input);
	Require(static_cast<bool>(built), built.Error.Message.c_str());
	const SassetParseResult parsed = ParseSasset(built.Bytes);
	Require(static_cast<bool>(parsed), parsed.Error.Message.c_str());
	Require(parsed.Metadata.SchemaVersion == SassetSchemaVersion, "schema version parsed");
	Require(parsed.Metadata.Type == SassetAssetType::Mesh, "asset type parsed");
	Require(parsed.Metadata.Id == meshId, "AssetId parsed");
	Require(parsed.Metadata.LogicalPath == input.LogicalPath, "logical path parsed");
	Require(parsed.Metadata.SourceDependencies.size() == 1, "source provenance parsed");
	Require(parsed.Metadata.SourceHash == input.SourceHash, "source graph hash parsed");

	AssetSystem runtime;
	Require(runtime.Initialize(), "runtime asset system initialized");
	const SassetLoadResult loaded = LoadSasset(runtime, built.Bytes);
	Require(static_cast<bool>(loaded), loaded.Error.Message.c_str());
	const auto handle = runtime.Find<MeshAsset>(input.LogicalPath);
	Require(handle.IsValid(), "loaded mesh has a typed runtime handle");
	const MeshAsset* resolved = runtime.Resolve(handle);
	Require(resolved != nullptr, "loaded mesh resolves resident");
	Require(resolved->VertexBytes == mesh.VertexBytes, "mesh vertex payload round-trips");
	Require(resolved->IndexBytes == mesh.IndexBytes, "mesh index payload round-trips");

	std::vector<std::byte> corrupted = built.Bytes;
	corrupted.back() ^= std::byte{ 0x01 };
	Require(ParseSasset(corrupted).Error.Code == SassetErrorCode::HashMismatch, "chunk hash catches payload corruption");

	runtime.Shutdown();
	return 0;
}
