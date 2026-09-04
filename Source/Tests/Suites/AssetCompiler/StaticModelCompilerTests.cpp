#include "Engine/Assets/AssetSystem.h"
#include "Engine/Assets/MeshAsset.h"
#include "Engine/Assets/ModelAsset.h"
#include "Engine/Assets/SassetFormat.h"
#include "Tests/Framework/Test.h"
#include "Tools/AssetCompiler/StaticModelCompiler.h"

#include <cstddef>
#include <string>
#include <vector>

namespace
{

	Swim::AssetCompiler::IntermediateModel MakeTriangleModel()
	{
		using namespace Swim::AssetCompiler;

		IntermediateModel source;

		SourceMesh mesh;
		mesh.Name = "Triangle";
		SourcePrimitive primitive;
		primitive.Topology = SourcePrimitiveTopology::Triangles;
		primitive.Vertices.resize(3);
		primitive.Vertices[0].Position = { 0.0f, 0.0f, 0.0f };
		primitive.Vertices[1].Position = { 1.0f, 0.0f, 0.0f };
		primitive.Vertices[2].Position = { 0.0f, 1.0f, 0.0f };
		primitive.Indices = { 0, 1, 2 };
		primitive.Bounds.Min = { 0.0f, 0.0f, 0.0f };
		primitive.Bounds.Max = { 1.0f, 1.0f, 0.0f };
		mesh.Primitives.push_back(primitive);
		source.Meshes.push_back(mesh);

		SourceNode node;
		node.Name = "TriangleNode";
		node.MeshIndex = 0;
		source.Nodes.push_back(node);
		source.Roots.push_back(0);

		return source;
	}

	std::vector<Swim::Assets::SassetSourceDependency> MakeProvenance()
	{
		return { { "Models/Triangle.gltf", Swim::Assets::ComputeContentHash("triangle-source") } };
	}

}

SWIM_TEST("AssetCompiler.StaticModelCompiler", "EmitsARootModelAndItsMeshDependency")
{
	using namespace Swim::AssetCompiler;
	using namespace Swim::Assets;

	IntermediateModel source = MakeTriangleModel();
	StaticModelCompiler compiler;
	const StaticModelCompileResult compiled = compiler.Compile(source, "Models/Triangle.gltf", MakeProvenance());
	SWIM_REQUIRE_MESSAGE(static_cast<bool>(compiled), compiled.Error.Message);

	SWIM_CHECK(compiled.RootId.IsValid());
	SWIM_CHECK_EQUAL(compiled.RootLogicalPath, std::string("Models/Triangle.model"));
	SWIM_CHECK_EQUAL(compiled.Stats.Meshes, std::size_t{ 1 });

	bool sawRoot = false;
	bool sawMesh = false;
	for (const CompiledSasset& file : compiled.Assets)
	{
		const SassetParseResult parsed = ParseSasset(file.Bytes);
		SWIM_CHECK_MESSAGE(static_cast<bool>(parsed), parsed.Error.Message);
		sawRoot = sawRoot || file.IsRoot;
		sawMesh = sawMesh || file.Type == SassetAssetType::Mesh;
	}

	SWIM_CHECK(sawRoot);
	SWIM_CHECK(sawMesh);
}

SWIM_TEST("AssetCompiler.StaticModelCompiler", "CompiledModelRebuildsTypedHandlesAtLoad")
{
	using namespace Swim::AssetCompiler;
	using namespace Swim::Assets;

	IntermediateModel source = MakeTriangleModel();
	StaticModelCompiler compiler;
	const StaticModelCompileResult compiled = compiler.Compile(source, "Models/Triangle.gltf", MakeProvenance());
	SWIM_REQUIRE_MESSAGE(static_cast<bool>(compiled), compiled.Error.Message);

	const CompiledSasset* root = nullptr;
	const CompiledSasset* meshFile = nullptr;
	for (const CompiledSasset& file : compiled.Assets)
	{
		if (file.IsRoot)
		{
			root = &file;
		}
		if (file.Type == SassetAssetType::Mesh)
		{
			meshFile = &file;
		}
	}
	SWIM_REQUIRE(root != nullptr);
	SWIM_REQUIRE(meshFile != nullptr);

	AssetSystem runtime;
	SWIM_REQUIRE(runtime.Initialize());
	SWIM_REQUIRE(static_cast<bool>(LoadSasset(runtime, meshFile->Bytes)));
	SWIM_REQUIRE(static_cast<bool>(LoadSasset(runtime, root->Bytes)));

	const ModelAsset* model = runtime.Resolve(runtime.Declare<ModelAsset>(compiled.RootId));
	SWIM_REQUIRE(model != nullptr);
	SWIM_REQUIRE(model->Nodes.size() == 1);
	SWIM_REQUIRE(model->Nodes[0].Mesh.IsValid());

	const MeshAsset* runtimeMesh = runtime.Resolve(model->Nodes[0].Mesh);
	SWIM_REQUIRE(runtimeMesh != nullptr);
	SWIM_REQUIRE(runtimeMesh->Primitives.size() == 1);
	SWIM_CHECK_EQUAL(runtimeMesh->Primitives[0].IndexCount, 3u);

	runtime.Shutdown();
}

SWIM_TEST("AssetCompiler.StaticModelCompiler", "UnsupportedRuntimeTopologyIsRejected")
{
	using namespace Swim::AssetCompiler;

	IntermediateModel source = MakeTriangleModel();
	source.Meshes[0].Primitives[0].Topology = SourcePrimitiveTopology::Lines;

	StaticModelCompiler compiler;
	const StaticModelCompileResult result = compiler.Compile(source, "Models/Triangle.gltf", MakeProvenance());
	SWIM_CHECK(!static_cast<bool>(result));
	SWIM_CHECK(result.Error.Code == StaticModelCompileErrorCode::UnsupportedTopology);
}
