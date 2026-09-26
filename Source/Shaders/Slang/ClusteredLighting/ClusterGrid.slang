// Clustered light grid (critical-path items 64-65, 68). Mirrors
// Engine/Systems/Renderer/ClusteredLighting/ClusterGrid.cpp and ClusterReference.cpp
// line for line: the grid record, log depth slices, cluster bounds, sphere/AABB
// tests and the heatmap ramp.
#ifndef SWIM_CLUSTER_GRID_SLANG
#define SWIM_CLUSTER_GRID_SLANG

struct ClusterGrid
{
	float4 ViewRow0; // World -> view rows.
	float4 ViewRow1;
	float4 ViewRow2;
	float4 Projection;	// P00, P11, P02, P12.
	float4 DepthParams; // P22, P23, Near, Far.
	float4 SliceParams; // SliceScale, SliceBias, TileSize, 0.
	uint4 Dimensions;	// TilesX, TilesY, Slices, ClusterCount.
	uint4 Limits;		// ViewportWidth, ViewportHeight, MaxLightsPerCluster (heatmap scale), MaskWords.
};

struct ClusterAabb
{
	float4 Min; // xyz used.
	float4 Max;
};

struct ClusterViewLight
{
	float3 Center;
	float Radius; // < 0: culled.
};

// A cluster's block in the light-index buffer (at Offset): OccupancyWords, then
// MaskWords light bitmask words (bit i of word w = local light 32 w + i). Count =
// RawCount = the cluster's light count; lists are never truncated.
struct ClusterRecord
{
	uint Offset;
	uint Count;
	uint RawCount;
	uint Reserved;
};

struct ClusterStats
{
	uint VisibleLights;
	uint RequestedIndices;
	uint WrittenIndices;
	uint OverflowClusters;
	uint DroppedIndices;
	uint MaxRawLightsPerCluster;
	uint NonEmptyClusters;
	uint ClusterCount;
};

uint ClusterMaskWords(ClusterGrid grid)
{
	return grid.Limits.w;
}

uint ClusterOccupancyWords(ClusterGrid grid)
{
	return (grid.Limits.w + 31u) / 32u;
}

uint ClusterBlockWords(ClusterGrid grid)
{
	return ClusterOccupancyWords(grid) + ClusterMaskWords(grid);
}

float3 ClusterWorldToView(ClusterGrid grid, float3 p)
{
	const float4 h = float4(p, 1.0);
	return float3(dot(grid.ViewRow0, h), dot(grid.ViewRow1, h), dot(grid.ViewRow2, h));
}

uint ClusterSliceForDepth(ClusterGrid grid, float viewDepth)
{
	const float slice = floor(log(max(viewDepth, 1.0e-6)) * grid.SliceParams.x + grid.SliceParams.y);
	return uint(clamp(slice, 0.0, float(grid.Dimensions.z - 1)));
}

float ClusterSliceNearDepth(ClusterGrid grid, uint slice)
{
	return exp((float(slice) - grid.SliceParams.y) / grid.SliceParams.x);
}

uint ClusterIndexFor(ClusterGrid grid, float pixelX, float pixelY, float viewDepth)
{
	const float tile = grid.SliceParams.z;
	const uint tileX = uint(clamp(floor(pixelX / tile), 0.0, float(grid.Dimensions.x - 1)));
	const uint tileY = uint(clamp(floor(pixelY / tile), 0.0, float(grid.Dimensions.y - 1)));
	const uint slice = ClusterSliceForDepth(grid, viewDepth);
	return (slice * grid.Dimensions.y + tileY) * grid.Dimensions.x + tileX;
}

float ClusterViewDepthFromNdc(ClusterGrid grid, float ndcDepth)
{
	return grid.DepthParams.y / (ndcDepth + grid.DepthParams.x);
}

ClusterAabb ClusterFrustumBounds(ClusterGrid grid, float pixelX0, float pixelY0, float pixelX1, float pixelY1, float nearDepth, float farDepth)
{
	const float width = float(grid.Limits.x);
	const float height = float(grid.Limits.y);
	const float ndcX[2] = { pixelX0 / width * 2.0 - 1.0, pixelX1 / width * 2.0 - 1.0 };
	const float ndcY[2] = { 1.0 - pixelY0 / height * 2.0, 1.0 - pixelY1 / height * 2.0 };
	const float depths[2] = { nearDepth, farDepth };
	ClusterAabb box;
	box.Min = float4(1.0e30, 1.0e30, 1.0e30, 0.0);
	box.Max = float4(-1.0e30, -1.0e30, -1.0e30, 0.0);
	for (int d = 0; d < 2; ++d)
	{
		for (int x = 0; x < 2; ++x)
		{
			for (int y = 0; y < 2; ++y)
			{
				const float depth = depths[d];
				const float3 point = float3(depth * (ndcX[x] + grid.Projection.z) / grid.Projection.x,
					depth * (ndcY[y] + grid.Projection.w) / grid.Projection.y, -depth);
				box.Min.xyz = min(box.Min.xyz, point);
				box.Max.xyz = max(box.Max.xyz, point);
			}
		}
	}
	return box;
}

ClusterAabb ClusterBounds(ClusterGrid grid, uint cluster)
{
	const uint tilesX = grid.Dimensions.x;
	const uint tilesY = grid.Dimensions.y;
	const uint slice = cluster / (tilesX * tilesY);
	const uint tileY = (cluster / tilesX) % tilesY;
	const uint tileX = cluster % tilesX;
	const float tile = grid.SliceParams.z;
	const float x0 = float(tileX) * tile;
	const float y0 = float(tileY) * tile;
	const float x1 = min(x0 + tile, float(grid.Limits.x));
	const float y1 = min(y0 + tile, float(grid.Limits.y));
	const float nearDepth = slice == 0 ? 0.0 : ClusterSliceNearDepth(grid, slice);
	const float farDepth = slice + 1 == grid.Dimensions.z ? grid.DepthParams.w : ClusterSliceNearDepth(grid, slice + 1);
	return ClusterFrustumBounds(grid, x0, y0, x1, y1, nearDepth, farDepth);
}

ClusterAabb ClusterVolumeBounds(ClusterGrid grid)
{
	return ClusterFrustumBounds(grid, 0.0, 0.0, float(grid.Limits.x), float(grid.Limits.y), 0.0, grid.DepthParams.w);
}

float ClusterDistanceSquaredToAabb(float3 p, ClusterAabb box)
{
	float sum = 0.0;
	for (int c = 0; c < 3; ++c)
	{
		const float d = max(max(box.Min[c] - p[c], p[c] - box.Max[c]), 0.0);
		sum += d * d;
	}
	return sum;
}

bool ClusterSphereIntersectsAabb(float3 center, float radius, ClusterAabb box)
{
	return radius >= 0.0 && ClusterDistanceSquaredToAabb(center, box) <= radius * radius;
}

float4 ClusterHeatmapColor(uint count, uint rawCount, uint maxLightsPerCluster)
{
	if (rawCount == 0)
	{
		return float4(0.0, 0.0, 0.0, 0.0);
	}
	if (count < rawCount)
	{
		return float4(1.0, 0.0, 1.0, 1.0);
	}
	const float t = clamp(float(count) / float(maxLightsPerCluster), 0.0, 1.0);
	if (t < 0.5)
	{
		const float u = t * 2.0;
		return float4(0.0, u, 1.0 - u, 1.0);
	}
	const float u = (t - 0.5) * 2.0;
	return float4(u, 1.0 - u, 0.0, 1.0);
}

#endif
