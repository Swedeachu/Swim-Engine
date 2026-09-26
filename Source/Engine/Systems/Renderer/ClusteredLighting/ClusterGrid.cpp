#include "Engine/Systems/Renderer/ClusteredLighting/ClusterGrid.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Swim::Render
{
	ClusterGridLayout ComputeClusterGridLayout(const ClusterGridDesc& desc)
	{
		if (desc.ViewportWidth == 0 || desc.ViewportHeight == 0 || desc.TileSize == 0 || desc.SliceCount == 0)
		{
			throw std::invalid_argument("Cluster grid needs a viewport, a tile size and at least one slice");
		}
		if (!(desc.Near > 0.0f) || !(desc.Far > desc.Near) || !std::isfinite(desc.Far))
		{
			throw std::invalid_argument("Cluster grid needs 0 < Near < Far (finite)");
		}
		if (desc.MaxLightsPerCluster == 0 || desc.LightCapacity == 0 || desc.LightCapacity > MaxClusterLights)
		{
			throw std::invalid_argument("Cluster grid needs non-zero light limits");
		}
		ClusterGridLayout layout;
		layout.TilesX = (desc.ViewportWidth + desc.TileSize - 1) / desc.TileSize;
		layout.TilesY = (desc.ViewportHeight + desc.TileSize - 1) / desc.TileSize;
		layout.Slices = desc.SliceCount;
		const std::uint64_t clusters = std::uint64_t(layout.TilesX) * layout.TilesY * layout.Slices;
		if (clusters > MaxClusterCount)
		{
			throw std::invalid_argument("Cluster grid has more than MaxClusterCount clusters");
		}
		layout.ClusterCount = static_cast<std::uint32_t>(clusters);
		const float logRatio = std::log(desc.Far / desc.Near);
		layout.SliceScale = float(layout.Slices) / logRatio;
		layout.SliceBias = -float(layout.Slices) * std::log(desc.Near) / logRatio;
		return layout;
	}

	ClusterGridRecord MakeClusterGridRecord(const ClusterGridDesc& desc, const ClusterView& view)
	{
		const auto layout = ComputeClusterGridLayout(desc);
		const auto& p = view.Projection;
		if (p[14] != -1.0f || p[15] != 0.0f || p[0] == 0.0f || p[5] == 0.0f)
		{
			throw std::invalid_argument("Cluster grids need a perspective projection (clip.w = -z)");
		}
		const auto& v = view.View;
		if (v[12] != 0.0f || v[13] != 0.0f || v[14] != 0.0f || v[15] != 1.0f)
		{
			throw std::invalid_argument("Cluster view matrices must be affine");
		}
		ClusterGridRecord record;
		for (int row = 0; row < 3; ++row)
		{
			for (int column = 0; column < 4; ++column)
			{
				record.ViewRows[row][column] = v[row * 4 + column];
			}
		}
		record.Projection[0] = p[0];
		record.Projection[1] = p[5];
		record.Projection[2] = p[2];
		record.Projection[3] = p[6];
		record.DepthParams[0] = p[10];
		record.DepthParams[1] = p[11];
		record.DepthParams[2] = desc.Near;
		record.DepthParams[3] = desc.Far;
		record.SliceParams[0] = layout.SliceScale;
		record.SliceParams[1] = layout.SliceBias;
		record.SliceParams[2] = float(desc.TileSize);
		record.Dimensions[0] = layout.TilesX;
		record.Dimensions[1] = layout.TilesY;
		record.Dimensions[2] = layout.Slices;
		record.Dimensions[3] = layout.ClusterCount;
		record.Limits[0] = desc.ViewportWidth;
		record.Limits[1] = desc.ViewportHeight;
		record.Limits[2] = desc.MaxLightsPerCluster;
		record.Limits[3] = (desc.LightCapacity + 31u) / 32u;
		return record;
	}

	std::uint32_t ClusterSliceForDepth(const ClusterGridRecord& grid, float viewDepth)
	{
		const float slice = std::floor(std::log(std::max(viewDepth, 1.0e-6f)) * grid.SliceParams[0] + grid.SliceParams[1]);
		return static_cast<std::uint32_t>(std::clamp(slice, 0.0f, float(grid.Dimensions[2] - 1)));
	}

	float ClusterSliceNearDepth(const ClusterGridRecord& grid, std::uint32_t slice)
	{
		return std::exp((float(slice) - grid.SliceParams[1]) / grid.SliceParams[0]);
	}

	std::uint32_t ClusterIndexFor(const ClusterGridRecord& grid, float pixelX, float pixelY, float viewDepth)
	{
		const float tile = grid.SliceParams[2];
		const auto tileX = static_cast<std::uint32_t>(std::clamp(std::floor(pixelX / tile), 0.0f, float(grid.Dimensions[0] - 1)));
		const auto tileY = static_cast<std::uint32_t>(std::clamp(std::floor(pixelY / tile), 0.0f, float(grid.Dimensions[1] - 1)));
		const auto slice = ClusterSliceForDepth(grid, viewDepth);
		return (slice * grid.Dimensions[1] + tileY) * grid.Dimensions[0] + tileX;
	}

	float ClusterViewDepthFromNdc(const ClusterGridRecord& grid, float ndcDepth)
	{
		return grid.DepthParams[1] / (ndcDepth + grid.DepthParams[0]);
	}
} // namespace Swim::Render
