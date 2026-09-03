#include "Tools/AssetCompiler/MeshOptimizer.h"

#include <cstdlib>
#include <iostream>
#include <vector>

namespace
{
	void Require(bool condition, const char* message)
	{
		if (!condition)
		{
			std::cerr << "mesh optimizer test failed: " << message << '\n';
			std::exit(1);
		}
	}

	Swim::AssetCompiler::SourceVertex MakeVertex(float x, float y, float z)
	{
		Swim::AssetCompiler::SourceVertex vertex;
		vertex.Position = { x, y, z };
		return vertex;
	}
}

int main()
{
	using namespace Swim::AssetCompiler;

	IntermediateModel model;
	SourceMesh mesh;
	SourcePrimitive primitive;
	primitive.Vertices =
	{
		MakeVertex(0.0f, 0.0f, 0.0f),
		MakeVertex(1.0f, 0.0f, 0.0f),
		MakeVertex(1.0f, 1.0f, 0.0f),
		MakeVertex(0.0f, 1.0f, 0.0f),
		MakeVertex(100.0f, 100.0f, 100.0f)
	};
	primitive.Indices = { 0, 1, 2, 0, 2, 3 };
	mesh.Primitives.push_back(primitive);

	SourcePrimitive lines;
	lines.Topology = SourcePrimitiveTopology::Lines;
	lines.Vertices = { MakeVertex(0.0f, 0.0f, 0.0f), MakeVertex(1.0f, 0.0f, 0.0f) };
	lines.Indices = { 0, 1 };
	mesh.Primitives.push_back(lines);
	model.Meshes.push_back(mesh);

	MeshOptimizer optimizer;
	const MeshOptimizationResult result = optimizer.Optimize(model);
	Require(static_cast<bool>(result), result.Error.Message.c_str());
	Require(result.Stats.TrianglePrimitives == 1, "one triangle primitive optimized");
	Require(result.Stats.SkippedPrimitives == 1, "non-triangle primitive skipped");
	Require(result.Stats.InputVertices == 5, "input vertex count recorded");
	Require(result.Stats.OutputVertices == 4, "unused vertex removed by vertex-fetch compaction");
	Require(result.Stats.Indices == 6, "index count preserved");
	Require(model.Meshes[0].Primitives[0].Vertices.size() == 4, "triangle vertices compacted");
	Require(model.Meshes[0].Primitives[0].Indices.size() == 6, "triangle index count preserved");
	Require(model.Meshes[0].Primitives[0].Bounds.Min[0] == 0.0f, "bounds minimum recalculated");
	Require(model.Meshes[0].Primitives[0].Bounds.Max[0] == 1.0f, "unused vertex removed from bounds");
	Require(model.Meshes[0].Primitives[1].Indices == std::vector<std::uint32_t>({ 0, 1 }), "non-triangle primitive preserved");

	IntermediateModel invalidModel;
	SourceMesh invalidMesh;
	SourcePrimitive invalidPrimitive;
	invalidPrimitive.Vertices = { MakeVertex(0.0f, 0.0f, 0.0f) };
	invalidPrimitive.Indices = { 0, 1, 0 };
	invalidMesh.Primitives.push_back(invalidPrimitive);
	invalidModel.Meshes.push_back(invalidMesh);
	const MeshOptimizationResult invalidResult = optimizer.Optimize(invalidModel);
	Require(!static_cast<bool>(invalidResult), "invalid source indices rejected before mutation");
	Require(invalidResult.Error.Code == MeshOptimizationErrorCode::InvalidIndex, "invalid index reports structured error");

	MeshOptimizationOptions invalidOptions;
	invalidOptions.OverdrawThreshold = 0.5f;
	const MeshOptimizationResult thresholdResult = optimizer.Optimize(model, invalidOptions);
	Require(!static_cast<bool>(thresholdResult), "invalid overdraw threshold rejected");
	Require(thresholdResult.Error.Code == MeshOptimizationErrorCode::InvalidOverdrawThreshold, "threshold reports structured error");
	return 0;
}
