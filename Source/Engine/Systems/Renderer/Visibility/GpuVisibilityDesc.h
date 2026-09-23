#pragma once
#include "Engine/Systems/Renderer/RHI/RhiContracts.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Swim::Render
{
	// Descriptor contract of the culling program (Shaders/Slang/GpuScene/
	// GpuVisibility.slang), all in one space. The program and its layout come
	// from the caller's compiled shader; GpuVisibility never loads shaders.
	struct GpuVisibilityBindings
	{
		static constexpr std::uint32_t Instances = 0;
		static constexpr std::uint32_t Transforms = 1;
		static constexpr std::uint32_t Meshes = 2;
		static constexpr std::uint32_t Submeshes = 3;
		static constexpr std::uint32_t View = 4;
		static constexpr std::uint32_t MaterialBins = 5;
		static constexpr std::uint32_t BinRanges = 6;
		static constexpr std::uint32_t IndexPages = 7;
		static constexpr std::uint32_t LodState = 8;
		static constexpr std::uint32_t Commands = 9;
		static constexpr std::uint32_t DrawRecords = 10;
		static constexpr std::uint32_t Counts = 11;
		static constexpr std::uint32_t Stats = 12;
		static constexpr std::uint32_t ThreadGroupSize = 64;
	};

	struct GpuVisibilityDesc
	{
		Rhi::ComputePipeline* CullPipeline = nullptr;
		Rhi::PipelineLayout* Layout = nullptr;
		std::uint32_t Space = 0;
		std::uint32_t MaxObjects = 16384;	  // LOD history rows; at least the GPU Scene capacity.
		std::uint32_t MaxMaterialSets = 1024; // MaterialSet -> material bin table size.
		// Draw capacity per material bin (bin 0 also takes unmapped material sets),
		// replicated for every index-page slot.
		std::vector<std::uint32_t> MaterialBinCapacities{ 65536 };
		std::uint32_t IndexPageSlots = 1; // Index pages one frame can draw from.
		std::string DebugName = "GPU visibility";
	};
} // namespace Swim::Render
