#include "Engine/Assets/AssetSystem.h"
#include "Engine/Assets/MeshAsset.h"
#include "Engine/Assets/ModelAsset.h"
#include "Engine/Assets/SassetFormat.h"
#include "Tools/AssetCompiler/StaticModelCompiler.h"

#include <cstdlib>
#include <iostream>

namespace
{
	void Require(bool condition, const char* message)
	{
		if (!condition)
		{
			std::cerr << "static model compiler test failed: " << message << '\n';
			std::exit(1);
		}
	}
}

int main()
{
	using namespace Swim::AssetCompiler;
	using namespace Swim::Assets;

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

	std::vector<SassetSourceDependency> provenance
	{
		{ "Models/Triangle.gltf", ComputeContentHash("triangle-source") }
	};
	StaticModelCompiler compiler;
	const StaticModelCompileResult compiled = compiler.Compile(source, "Models/Triangle.gltf", provenance);
	Require(static_cast<bool>(compiled), compiled.Error.Message.c_str());
	Require(compiled.RootId.IsValid(), "root model has stable identity");
	Require(compiled.RootLogicalPath == "Models/Triangle.model", "root logical path is source path with model identity");
	Require(compiled.Stats.Meshes == 1, "one mesh compiled");

	const CompiledSasset* root = nullptr;
	const CompiledSasset* meshFile = nullptr;
	for (const CompiledSasset& file : compiled.Assets)
	{
		const SassetParseResult parsed = ParseSasset(file.Bytes);
		Require(static_cast<bool>(parsed), parsed.Error.Message.c_str());
		if (file.IsRoot)
		{
			root = &file;
		}
		if (file.Type == SassetAssetType::Mesh)
		{
			meshFile = &file;
		}
	}
	Require(root != nullptr, "root model .sasset emitted");
	Require(meshFile != nullptr, "mesh dependency .sasset emitted");

	AssetSystem runtime;
	Require(runtime.Initialize(), "runtime AssetSystem initialized");
	Require(static_cast<bool>(LoadSasset(runtime, meshFile->Bytes)), "mesh dependency loads");
	Require(static_cast<bool>(LoadSasset(runtime, root->Bytes)), "root model loads");
	const ModelAsset* model = runtime.Resolve(runtime.Declare<ModelAsset>(compiled.RootId));
	Require(model != nullptr && model->Nodes.size() == 1, "runtime model resolves after load");
	Require(model->Nodes[0].Mesh.IsValid(), "runtime model reconstructs typed mesh handle from persisted AssetId");
	const MeshAsset* runtimeMesh = runtime.Resolve(model->Nodes[0].Mesh);
	Require(runtimeMesh != nullptr, "runtime model mesh dependency resolves resident");
	Require(runtimeMesh->Primitives.size() == 1 && runtimeMesh->Primitives[0].IndexCount == 3, "compiled triangle geometry preserved");
	runtime.Shutdown();

	source.Meshes[0].Primitives[0].Topology = SourcePrimitiveTopology::Lines;
	const StaticModelCompileResult unsupported = compiler.Compile(source, "Models/Triangle.gltf", provenance);
	Require(unsupported.Error.Code == StaticModelCompileErrorCode::UnsupportedTopology, "unsupported runtime topology rejected explicitly");
	return 0;
}
