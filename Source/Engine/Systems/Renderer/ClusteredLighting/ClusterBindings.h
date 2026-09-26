#pragma once
#include <cstdint>

namespace Swim::Render
{
	// Descriptor contracts of the clustered lighting programs
	// (Shaders/Slang/ClusteredLighting). Every program uses one space; Grid is a
	// one-element StructuredBuffer<ClusterGridRecord>.

	struct ClusterLightCullBindings // ClusterLightCull.slang: one thread per local light.
	{
		static constexpr std::uint32_t Grid = 0;
		static constexpr std::uint32_t Lights = 1;		// StructuredBuffer<GpuLightRecord>.
		static constexpr std::uint32_t LightHeader = 2; // StructuredBuffer<GpuLightHeader>.
		static constexpr std::uint32_t ViewLights = 3;	// RWStructuredBuffer<ViewLight> (16 bytes).
		static constexpr std::uint32_t ThreadGroupSize = 64;
	};

	struct ClusterBoundsBindings // ClusterBounds.slang: one thread per cluster.
	{
		static constexpr std::uint32_t Grid = 0;
		static constexpr std::uint32_t Bounds = 1; // RWStructuredBuffer<ClusterAabb> (32 bytes).
		static constexpr std::uint32_t ThreadGroupSize = 64;
	};

	struct ClusterAssignBindings // ClusterAssign.slang: one thread per (cluster, mask word).
	{
		static constexpr std::uint32_t Grid = 0;
		static constexpr std::uint32_t LightHeader = 1;
		static constexpr std::uint32_t ViewLights = 2;
		static constexpr std::uint32_t Bounds = 3;
		static constexpr std::uint32_t Indices = 4; // RWStructuredBuffer<uint>: cluster blocks (occupancy + mask words).
		static constexpr std::uint32_t Stats = 5;	// RWStructuredBuffer<ClusterStats>: cleared here.
		static constexpr std::uint32_t ThreadGroupSize = 64;
		static constexpr std::uint32_t PushConstantBytes = 16; // uint RowThreads (2D dispatch pitch), 3 reserved.
		static constexpr std::uint32_t MaxGroupsPerRow = 65535;
	};

	struct ClusterScanBindings // ClusterScan.slang: one thread per cluster (and per local light for the visible count).
	{
		static constexpr std::uint32_t Grid = 0;
		static constexpr std::uint32_t LightHeader = 1;
		static constexpr std::uint32_t ViewLights = 2;
		static constexpr std::uint32_t Records = 3; // RWStructuredBuffer<ClusterRecord>.
		static constexpr std::uint32_t Stats = 4;	// RWStructuredBuffer<ClusterStats> (atomics).
		static constexpr std::uint32_t Indices = 5; // RWStructuredBuffer<uint>: occupancy words written here.
		static constexpr std::uint32_t ThreadGroupSize = 64;
	};

	struct ClusterHeatmapBindings // ClusterHeatmap.slang: one thread per pixel.
	{
		static constexpr std::uint32_t Grid = 0;
		static constexpr std::uint32_t Records = 1;
		static constexpr std::uint32_t Depth = 2;  // Texture2D<float>: the depth buffer (D32 depth aspect or R32Float).
		static constexpr std::uint32_t Output = 3; // RWTexture2D<float4> (rgba8).
		static constexpr std::uint32_t ThreadGroupSize = 8;
	};
} // namespace Swim::Render
