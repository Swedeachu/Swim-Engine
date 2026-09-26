#pragma once
#include <array>
#include <cstdint>

namespace Swim::Render
{
	// The clustered light grid (critical-path item 64): screen-space tiles of
	// TileSize pixels times logarithmic depth slices between Near and Far. Slice k
	// covers view depths [Near * (Far / Near)^(k / S), Near * (Far / Near)^((k + 1) / S)),
	// so every slice has the same depth ratio; depth beyond Far lands in the last
	// slice and depth before Near in the first.
	struct ClusterGridDesc
	{
		std::uint32_t ViewportWidth = 0;
		std::uint32_t ViewportHeight = 0;
		std::uint32_t TileSize = 64; // Pixels per tile edge.
		std::uint32_t SliceCount = 24;
		float Near = 0.1f;	// View depth where slicing starts (> 0).
		float Far = 500.0f; // View depth where the last slice ends; lights beyond are culled.
		// Full scale of the debug heatmap (a cluster with this many lights is red). Lists are
		// never truncated: every cluster keeps a bitmask over all local lights.
		std::uint32_t MaxLightsPerCluster = 64;
		// Local lights the per-cluster bitmasks address (rounded up to 32). Assignment
		// throws when more local lights are bound; the runtime sets it from the light count.
		std::uint32_t LightCapacity = 1024;
	};

	// The camera the grid is built for: a row-major world-to-view affine (right-handed,
	// looking down -Z) and a row-major perspective projection (clip = P * view, clip.w
	// = -z). Any depth mapping works (forward, reverse-Z, infinite far).
	struct ClusterView
	{
		std::array<float, 16> View{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
		std::array<float, 16> Projection{};
	};

	// ClusterGrid.slang's grid record (std430, 128 bytes), shared by every clustering pass.
	struct ClusterGridRecord
	{
		float ViewRows[3][4] = {};		  // World -> view rows.
		float Projection[4] = {};		  // P00, P11, P02, P12.
		float DepthParams[4] = {};		  // P22, P23, Near, Far.
		float SliceParams[4] = {};		  // SliceScale, SliceBias, TileSize, 0.
		std::uint32_t Dimensions[4] = {}; // TilesX, TilesY, Slices, ClusterCount.
		std::uint32_t Limits[4] = {};	  // ViewportWidth, ViewportHeight, MaxLightsPerCluster (heatmap scale), MaskWords.
	};

	static_assert(sizeof(ClusterGridRecord) == 128);

	// One cluster's light set (ClusterRecords buffer, 16 bytes). The light-index buffer
	// holds, per cluster at Offset, OccupancyWords then MaskWords uints: bit i of mask word
	// w marks local light 32 w + i; bit j of occupancy word o marks a non-zero mask word
	// 32 o + j, so shading visits only words that hold lights. Count = RawCount = the
	// cluster's light count (lists are never truncated).
	struct ClusterRecord
	{
		std::uint32_t Offset = 0;
		std::uint32_t Count = 0;
		std::uint32_t RawCount = 0;
		std::uint32_t Reserved = 0;
	};

	static_assert(sizeof(ClusterRecord) == 16);

	// Assignment statistics (ClusterStats buffer, 32 bytes), written by the scan pass.
	struct ClusterStats
	{
		std::uint32_t VisibleLights = 0;	// Local lights intersecting the clustered volume.
		std::uint32_t RequestedIndices = 0; // Sum of per-cluster light counts.
		std::uint32_t WrittenIndices = 0;	// Equal to RequestedIndices (bitmasks never drop lights).
		std::uint32_t OverflowClusters = 0; // Clusters above MaxLightsPerCluster (heatmap scale only; nothing is dropped).
		std::uint32_t DroppedIndices = 0;	// Always 0 (kept for the buffer layout).
		std::uint32_t MaxRawLightsPerCluster = 0;
		std::uint32_t NonEmptyClusters = 0;
		std::uint32_t ClusterCount = 0;
	};

	static_assert(sizeof(ClusterStats) == 32);

	// Tile/slice layout derived from a desc.
	struct ClusterGridLayout
	{
		std::uint32_t TilesX = 0;
		std::uint32_t TilesY = 0;
		std::uint32_t Slices = 0;
		std::uint32_t ClusterCount = 0;
		float SliceScale = 0.0f; // Slice = floor(log(depth) * SliceScale + SliceBias).
		float SliceBias = 0.0f;
	};

	// Bitmask layout of a grid: MaskWords = ceil(LightCapacity / 32) words per cluster,
	// OccupancyWords = ceil(MaskWords / 32), and each cluster's block is their sum.
	inline std::uint32_t ClusterMaskWords(const ClusterGridRecord& grid)
	{
		return grid.Limits[3];
	}

	inline std::uint32_t ClusterOccupancyWords(const ClusterGridRecord& grid)
	{
		return (grid.Limits[3] + 31u) / 32u;
	}

	inline std::uint32_t ClusterBlockWords(const ClusterGridRecord& grid)
	{
		return ClusterOccupancyWords(grid) + ClusterMaskWords(grid);
	}

	// Throws std::invalid_argument for a zero viewport, tile size or slice count,
	// 0 < Near < Far violations, zero limits or more than MaxClusterCount clusters.
	ClusterGridLayout ComputeClusterGridLayout(const ClusterGridDesc& desc);
	inline constexpr std::uint32_t MaxClusterCount = 1u << 20;
	inline constexpr std::uint32_t MaxClusterLights = 1u << 16; // LightCapacity limit (2048 mask words).

	// Throws std::invalid_argument when the projection is not a perspective one
	// (P32 != -1 or P33 != 0) or the view is not affine.
	ClusterGridRecord MakeClusterGridRecord(const ClusterGridDesc& desc, const ClusterView& view);

	// The slice containing a view depth (clamped to [0, Slices - 1]) and slice k's near depth.
	std::uint32_t ClusterSliceForDepth(const ClusterGridRecord& grid, float viewDepth);
	float ClusterSliceNearDepth(const ClusterGridRecord& grid, std::uint32_t slice);
	// Cluster index of a pixel (x, y from the top-left, in pixels) at a view depth.
	std::uint32_t ClusterIndexFor(const ClusterGridRecord& grid, float pixelX, float pixelY, float viewDepth);
	// View depth (> 0) of a stored depth-buffer value under the grid's projection.
	float ClusterViewDepthFromNdc(const ClusterGridRecord& grid, float ndcDepth);
} // namespace Swim::Render
