#include "Tools/AssetCompiler/MeshOptimizer.h"

#include <meshoptimizer.h>

#include <algorithm>
#include <cstddef>
#include <limits>
#include <type_traits>
#include <utility>
#include <vector>

namespace Swim::AssetCompiler
{
	namespace
	{
		static_assert(std::is_trivially_copyable_v<SourceVertex>);
		static_assert(offsetof(SourceVertex, Position) == 0);

		MeshOptimizationResult MakeError(
			MeshOptimizationErrorCode code,
			std::size_t meshIndex,
			std::size_t primitiveIndex,
			const char* message)
		{
			MeshOptimizationResult result;
			result.Error.Code = code;
			result.Error.MeshIndex = meshIndex;
			result.Error.PrimitiveIndex = primitiveIndex;
			result.Error.Message = message;
			return result;
		}

		bool IsTrianglePrimitive(const SourcePrimitive& primitive)
		{
			return primitive.Topology == SourcePrimitiveTopology::Triangles;
		}

		void RecalculateBounds(SourcePrimitive& primitive)
		{
			primitive.Bounds = {};
			for (const SourceVertex& vertex : primitive.Vertices)
			{
				for (std::size_t axis = 0; axis < vertex.Position.size(); ++axis)
				{
					primitive.Bounds.Min[axis] = std::min(primitive.Bounds.Min[axis], vertex.Position[axis]);
					primitive.Bounds.Max[axis] = std::max(primitive.Bounds.Max[axis], vertex.Position[axis]);
				}
			}
		}
	}

	MeshOptimizationResult MeshOptimizer::Optimize(IntermediateModel& model, const MeshOptimizationOptions& options) const
	{
		if (options.OptimizeOverdraw && options.OverdrawThreshold < 1.0f)
		{
			return MakeError(MeshOptimizationErrorCode::InvalidOverdrawThreshold, 0, 0, "overdraw threshold must be at least 1.0");
		}

		for (std::size_t meshIndex = 0; meshIndex < model.Meshes.size(); ++meshIndex)
		{
			const SourceMesh& mesh = model.Meshes[meshIndex];
			for (std::size_t primitiveIndex = 0; primitiveIndex < mesh.Primitives.size(); ++primitiveIndex)
			{
				const SourcePrimitive& primitive = mesh.Primitives[primitiveIndex];
				if (!IsTrianglePrimitive(primitive) || primitive.Indices.empty())
				{
					continue;
				}

				if ((primitive.Indices.size() % 3) != 0)
				{
					return MakeError(MeshOptimizationErrorCode::InvalidTriangleIndexCount, meshIndex, primitiveIndex, "triangle primitive index count must be divisible by three");
				}

				for (const std::uint32_t index : primitive.Indices)
				{
					if (index >= primitive.Vertices.size())
					{
						return MakeError(MeshOptimizationErrorCode::InvalidIndex, meshIndex, primitiveIndex, "primitive index references a vertex outside the source vertex buffer");
					}
				}
			}
		}

		MeshOptimizationResult result;
		for (SourceMesh& mesh : model.Meshes)
		{
			for (SourcePrimitive& primitive : mesh.Primitives)
			{
				if (!IsTrianglePrimitive(primitive) || primitive.Indices.empty())
				{
					++result.Stats.SkippedPrimitives;
					continue;
				}

				++result.Stats.TrianglePrimitives;
				result.Stats.InputVertices += primitive.Vertices.size();
				result.Stats.Indices += primitive.Indices.size();

				if (options.OptimizeVertexCache)
				{
					std::vector<std::uint32_t> optimizedIndices(primitive.Indices.size());
					meshopt_optimizeVertexCache(
						optimizedIndices.data(),
						primitive.Indices.data(),
						primitive.Indices.size(),
						primitive.Vertices.size());
					primitive.Indices.swap(optimizedIndices);
				}

				if (options.OptimizeOverdraw && !primitive.Vertices.empty())
				{
					std::vector<std::uint32_t> optimizedIndices(primitive.Indices.size());
					meshopt_optimizeOverdraw(
						optimizedIndices.data(),
						primitive.Indices.data(),
						primitive.Indices.size(),
						primitive.Vertices.front().Position.data(),
						primitive.Vertices.size(),
						sizeof(SourceVertex),
						options.OverdrawThreshold);
					primitive.Indices.swap(optimizedIndices);
				}

				if (options.OptimizeVertexFetch && !primitive.Vertices.empty())
				{
					std::vector<SourceVertex> optimizedVertices(primitive.Vertices.size());
					const std::size_t vertexCount = meshopt_optimizeVertexFetch(
						optimizedVertices.data(),
						primitive.Indices.data(),
						primitive.Indices.size(),
						primitive.Vertices.data(),
						primitive.Vertices.size(),
						sizeof(SourceVertex));
					optimizedVertices.resize(vertexCount);
					primitive.Vertices.swap(optimizedVertices);
					RecalculateBounds(primitive);
				}

				result.Stats.OutputVertices += primitive.Vertices.size();
			}
		}

		return result;
	}

}
