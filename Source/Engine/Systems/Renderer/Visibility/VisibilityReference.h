#pragma once
#include "Engine/Systems/Renderer/GpuScene/GpuInstanceRecord.h"
#include "Engine/Systems/Renderer/GpuScene/GpuTransformRecord.h"
#include "Engine/Systems/Renderer/Geometry/GpuMeshMetadata.h"
#include "Engine/Systems/Renderer/RHI/RhiContracts.h"
#include "Engine/Systems/Renderer/Visibility/GpuDrawRecord.h"
#include "Engine/Systems/Renderer/Visibility/GpuLodState.h"
#include "Engine/Systems/Renderer/Visibility/GpuViewRecord.h"
#include "Engine/Systems/Renderer/Visibility/VisibilityBinLayout.h"
#include "Engine/Systems/Renderer/Visibility/VisibilityStats.h"

#include <span>
#include <vector>

namespace Swim::Render
{
	struct VisibilityReferenceInputs
	{
		std::span<const GpuInstanceRecord> Instances; // RowCount rows.
		std::span<const GpuTransformRecord> Transforms;
		std::span<const GpuMeshMetadata> Meshes; // Indexed by MeshIndex.
		std::span<const GpuSubmeshRecord> Submeshes;
		GpuViewRecord View{};
		std::span<const std::uint32_t> MaterialBins; // MaterialSet -> material bin; out of range -> bin 0.
		std::span<const std::uint32_t> IndexPages;	 // Page slot -> GeometryHeap index page id.
		const VisibilityBinLayout* Bins = nullptr;
	};

	struct VisibilityReferenceDraw
	{
		Rhi::DrawIndexedIndirectCommand Command; // FirstInstance is the slot within the bin (the GPU adds the bin's First).
		GpuDrawRecord Record;
	};

	struct VisibilityReferenceResult
	{
		std::vector<std::vector<VisibilityReferenceDraw>> Bins; // Row order; the GPU order within a bin is unspecified.
		VisibilityStats Stats;
	};

	// The CPU definition of GPU visibility (frustum culling, LOD with hysteresis,
	// binning, command generation). `lodState` is read and updated like the GPU's
	// persistent LOD buffer (one entry per row, resized as needed).
	VisibilityReferenceResult RunVisibilityReference(const VisibilityReferenceInputs& inputs, std::vector<GpuLodState>& lodState);
} // namespace Swim::Render
