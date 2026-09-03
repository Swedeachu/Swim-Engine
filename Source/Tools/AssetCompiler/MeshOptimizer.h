#pragma once

#include "Tools/AssetCompiler/IntermediateModel.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace Swim::AssetCompiler
{

	enum class MeshOptimizationErrorCode : std::uint8_t
	{
		None,
		InvalidIndex,
		InvalidTriangleIndexCount,
		InvalidOverdrawThreshold
	};

	struct MeshOptimizationError
	{
		MeshOptimizationErrorCode Code = MeshOptimizationErrorCode::None;
		std::size_t MeshIndex = 0;
		std::size_t PrimitiveIndex = 0;
		std::string Message;
	};

	struct MeshOptimizationOptions
	{
		bool OptimizeVertexCache = true;
		bool OptimizeOverdraw = true;
		bool OptimizeVertexFetch = true;
		float OverdrawThreshold = 1.05f;
	};

	struct MeshOptimizationStats
	{
		std::size_t TrianglePrimitives = 0;
		std::size_t SkippedPrimitives = 0;
		std::size_t InputVertices = 0;
		std::size_t OutputVertices = 0;
		std::size_t Indices = 0;
	};

	struct MeshOptimizationResult
	{
		MeshOptimizationStats Stats;
		MeshOptimizationError Error;

		explicit operator bool() const
		{
			return Error.Code == MeshOptimizationErrorCode::None;
		}
	};

	class MeshOptimizer
	{
	public:
		MeshOptimizationResult Optimize(IntermediateModel& model, const MeshOptimizationOptions& options = {}) const;
	};

}
