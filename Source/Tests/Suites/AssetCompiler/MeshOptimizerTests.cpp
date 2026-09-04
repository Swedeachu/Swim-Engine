#include "Tests/Framework/Test.h"
#include "Tools/AssetCompiler/MeshOptimizer.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace
{

	Swim::AssetCompiler::SourceVertex MakeVertex(float x, float y, float z)
	{
		Swim::AssetCompiler::SourceVertex vertex;
		vertex.Position = { x, y, z };
		return vertex;
	}

	// A quad built from two triangles plus one vertex nothing indexes, followed
	// by a line primitive the optimizer is expected to leave alone.
	Swim::AssetCompiler::IntermediateModel MakeMixedTopologyModel()
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
		return model;
	}

}

SWIM_TEST("AssetCompiler.MeshOptimizer", "CompactsTrianglesAndSkipsOtherTopologies")
{
	using namespace Swim::AssetCompiler;

	IntermediateModel model = MakeMixedTopologyModel();
	MeshOptimizer optimizer;
	const MeshOptimizationResult result = optimizer.Optimize(model);
	SWIM_REQUIRE_MESSAGE(static_cast<bool>(result), result.Error.Message);

	SWIM_CHECK_EQUAL(result.Stats.TrianglePrimitives, std::size_t{ 1 });
	SWIM_CHECK_EQUAL(result.Stats.SkippedPrimitives, std::size_t{ 1 });
	SWIM_CHECK_EQUAL(result.Stats.InputVertices, std::size_t{ 5 });
	SWIM_CHECK_EQUAL(result.Stats.OutputVertices, std::size_t{ 4 });
	SWIM_CHECK_EQUAL(result.Stats.Indices, std::size_t{ 6 });

	SWIM_REQUIRE(model.Meshes.size() == 1);
	SWIM_REQUIRE(model.Meshes[0].Primitives.size() == 2);
	SWIM_CHECK_EQUAL(model.Meshes[0].Primitives[0].Vertices.size(), std::size_t{ 4 });
	SWIM_CHECK_EQUAL(model.Meshes[0].Primitives[0].Indices.size(), std::size_t{ 6 });
	SWIM_CHECK_EQUAL(model.Meshes[0].Primitives[0].Bounds.Min[0], 0.0f);
	SWIM_CHECK_EQUAL(model.Meshes[0].Primitives[0].Bounds.Max[0], 1.0f);
	SWIM_CHECK(model.Meshes[0].Primitives[1].Indices == std::vector<std::uint32_t>({ 0, 1 }));
}

SWIM_TEST("AssetCompiler.MeshOptimizer", "OutOfRangeIndicesAreRejectedBeforeMutation")
{
	using namespace Swim::AssetCompiler;

	IntermediateModel invalidModel;
	SourceMesh invalidMesh;
	SourcePrimitive invalidPrimitive;
	invalidPrimitive.Vertices = { MakeVertex(0.0f, 0.0f, 0.0f) };
	invalidPrimitive.Indices = { 0, 1, 0 };
	invalidMesh.Primitives.push_back(invalidPrimitive);
	invalidModel.Meshes.push_back(invalidMesh);

	MeshOptimizer optimizer;
	const MeshOptimizationResult result = optimizer.Optimize(invalidModel);
	SWIM_CHECK(!static_cast<bool>(result));
	SWIM_CHECK(result.Error.Code == MeshOptimizationErrorCode::InvalidIndex);
}

SWIM_TEST("AssetCompiler.MeshOptimizer", "InvalidOverdrawThresholdIsRejected")
{
	using namespace Swim::AssetCompiler;

	IntermediateModel model = MakeMixedTopologyModel();

	MeshOptimizationOptions invalidOptions;
	invalidOptions.OverdrawThreshold = 0.5f;

	MeshOptimizer optimizer;
	const MeshOptimizationResult result = optimizer.Optimize(model, invalidOptions);
	SWIM_CHECK(!static_cast<bool>(result));
	SWIM_CHECK(result.Error.Code == MeshOptimizationErrorCode::InvalidOverdrawThreshold);
}
