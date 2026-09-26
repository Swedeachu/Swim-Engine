#include "Engine/Systems/Renderer/ClusteredLighting/ClusterReference.h"
#include "Engine/Systems/Renderer/Lights/LightMath.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Swim::Render::Clustering
{
	Float3 WorldToView(const ClusterGridRecord& grid, const Float3& p)
	{
		Float3 result;
		for (int row = 0; row < 3; ++row)
		{
			const auto& r = grid.ViewRows[row];
			result[row] = r[0] * p[0] + r[1] * p[1] + r[2] * p[2] + r[3];
		}
		return result;
	}

	Aabb FrustumBounds(
		const ClusterGridRecord& grid, float pixelX0, float pixelY0, float pixelX1, float pixelY1, float nearDepth, float farDepth)
	{
		const float width = float(grid.Limits[0]);
		const float height = float(grid.Limits[1]);
		const float ndcX[2] = { pixelX0 / width * 2.0f - 1.0f, pixelX1 / width * 2.0f - 1.0f };
		const float ndcY[2] = { 1.0f - pixelY0 / height * 2.0f, 1.0f - pixelY1 / height * 2.0f };
		const float depths[2] = { nearDepth, farDepth };
		Aabb box{ { INFINITY, INFINITY, INFINITY }, { -INFINITY, -INFINITY, -INFINITY } };
		for (float depth : depths)
		{
			for (float nx : ndcX)
			{
				for (float ny : ndcY)
				{
					const Float3 point{ depth * (nx + grid.Projection[2]) / grid.Projection[0],
						depth * (ny + grid.Projection[3]) / grid.Projection[1], -depth };
					for (int c = 0; c < 3; ++c)
					{
						box.Min[c] = std::min(box.Min[c], point[c]);
						box.Max[c] = std::max(box.Max[c], point[c]);
					}
				}
			}
		}
		return box;
	}

	Aabb ClusterBounds(const ClusterGridRecord& grid, std::uint32_t cluster)
	{
		const std::uint32_t tilesX = grid.Dimensions[0];
		const std::uint32_t tilesY = grid.Dimensions[1];
		const std::uint32_t slice = cluster / (tilesX * tilesY);
		const std::uint32_t tileY = (cluster / tilesX) % tilesY;
		const std::uint32_t tileX = cluster % tilesX;
		const float tile = grid.SliceParams[2];
		const float x0 = float(tileX) * tile;
		const float y0 = float(tileY) * tile;
		const float x1 = std::min(x0 + tile, float(grid.Limits[0]));
		const float y1 = std::min(y0 + tile, float(grid.Limits[1]));
		const float nearDepth = slice == 0 ? 0.0f : ClusterSliceNearDepth(grid, slice);
		const float farDepth = slice + 1 == grid.Dimensions[2] ? grid.DepthParams[3] : ClusterSliceNearDepth(grid, slice + 1);
		return FrustumBounds(grid, x0, y0, x1, y1, nearDepth, farDepth);
	}

	Aabb VolumeBounds(const ClusterGridRecord& grid)
	{
		return FrustumBounds(grid, 0.0f, 0.0f, float(grid.Limits[0]), float(grid.Limits[1]), 0.0f, grid.DepthParams[3]);
	}

	float DistanceSquaredToAabb(const Float3& point, const Aabb& box)
	{
		float sum = 0.0f;
		for (int c = 0; c < 3; ++c)
		{
			const float d = std::max(std::max(box.Min[c] - point[c], point[c] - box.Max[c]), 0.0f);
			sum += d * d;
		}
		return sum;
	}

	bool SphereIntersectsAabb(const Float3& center, float radius, const Aabb& box)
	{
		return radius >= 0.0f && DistanceSquaredToAabb(center, box) <= radius * radius;
	}

	ViewLight CullLight(const ClusterGridRecord& grid, const GpuLightRecord& light)
	{
		const auto sphere = Lights::LightBoundingSphere(light);
		ViewLight result;
		result.Center = WorldToView(grid, sphere.Center);
		result.Radius = SphereIntersectsAabb(result.Center, sphere.Radius, VolumeBounds(grid)) ? sphere.Radius : -1.0f;
		return result;
	}

	ClusterAssignment AssignLights(const ClusterGridRecord& grid, std::span<const GpuLightRecord> rows, const GpuLightHeader& header)
	{
		std::vector<ViewLight> viewLights;
		for (std::uint32_t i = 0; i < header.LocalCount; ++i)
		{
			viewLights.push_back(CullLight(grid, rows[header.FirstLocalRow + i]));
		}
		std::vector<Aabb> bounds(grid.Dimensions[3]);
		for (std::uint32_t c = 0; c < grid.Dimensions[3]; ++c)
		{
			bounds[c] = ClusterBounds(grid, c);
		}
		return AssignLights(grid, std::move(viewLights), std::move(bounds));
	}

	ClusterAssignment AssignLights(const ClusterGridRecord& grid, std::vector<ViewLight> viewLights, std::vector<Aabb> bounds)
	{
		ClusterAssignment result;
		const std::uint32_t clusterCount = grid.Dimensions[3];
		const std::uint32_t maskWords = ClusterMaskWords(grid);
		const std::uint32_t occupancyWords = ClusterOccupancyWords(grid);
		const std::uint32_t block = ClusterBlockWords(grid);
		result.ViewLights = std::move(viewLights);
		result.Bounds = std::move(bounds);
		const auto lightCount = static_cast<std::uint32_t>(result.ViewLights.size());
		if (std::uint64_t(lightCount) > std::uint64_t(maskWords) * 32u)
		{
			throw std::invalid_argument("More local lights than the cluster grid's LightCapacity");
		}
		for (const auto& light : result.ViewLights)
		{
			result.Stats.VisibleLights += light.Radius >= 0.0f ? 1u : 0u;
		}
		result.Records.resize(clusterCount);
		result.Indices.assign(std::size_t(clusterCount) * block, 0u);
		for (std::uint32_t c = 0; c < clusterCount; ++c)
		{
			auto& record = result.Records[c];
			record.Offset = c * block;
			std::uint32_t* words = result.Indices.data() + record.Offset;
			for (std::uint32_t i = 0; i < lightCount; ++i)
			{
				const auto& light = result.ViewLights[i];
				if (SphereIntersectsAabb(light.Center, light.Radius, result.Bounds[c]))
				{
					words[occupancyWords + i / 32u] |= 1u << (i % 32u);
					++record.Count;
				}
			}
			for (std::uint32_t w = 0; w < maskWords; ++w)
			{
				if (words[occupancyWords + w] != 0)
				{
					words[w / 32u] |= 1u << (w % 32u);
				}
			}
			record.RawCount = record.Count;
			result.Stats.RequestedIndices += record.Count;
			result.Stats.OverflowClusters += record.Count > grid.Limits[2] ? 1u : 0u;
			result.Stats.MaxRawLightsPerCluster = std::max(result.Stats.MaxRawLightsPerCluster, record.Count);
			result.Stats.NonEmptyClusters += record.Count > 0 ? 1u : 0u;
		}
		result.Stats.WrittenIndices = result.Stats.RequestedIndices;
		result.Stats.DroppedIndices = 0;
		result.Stats.ClusterCount = clusterCount;
		return result;
	}

	std::vector<std::uint32_t> ClusterLightList(const ClusterAssignment& assignment, const ClusterGridRecord& grid, std::uint32_t cluster)
	{
		std::vector<std::uint32_t> list;
		ForEachClusterLight(grid, assignment.Records, assignment.Indices, cluster,
			[&](std::uint32_t index)
			{
				list.push_back(index);
			});
		return list;
	}

	Float3 ShadeClustered(const ClusterAssignment& assignment, const ClusterGridRecord& grid, std::span<const GpuLightRecord> rows,
		const GpuLightHeader& header, const StandardPbr::Surface& surface, const Float3& normal, const Float3& view,
		const Float3& worldPosition, float pixelX, float pixelY, float viewDepth)
	{
		Float3 sum{ 0, 0, 0 };
		const auto add = [&](const GpuLightRecord& light)
		{
			const auto sample = Lights::EvaluateLight(light, worldPosition);
			const auto brdf = StandardPbr::EvaluateBrdf(surface, normal, view, sample.Direction);
			for (int c = 0; c < 3; ++c)
			{
				sum[c] += brdf[c] * sample.Radiance[c];
			}
		};
		for (std::uint32_t i = 0; i < header.DirectionalCount; ++i)
		{
			add(rows[i]);
		}
		ForEachClusterLight(grid, assignment.Records, assignment.Indices, ClusterIndexFor(grid, pixelX, pixelY, viewDepth),
			[&](std::uint32_t index)
			{
				add(rows[header.FirstLocalRow + index]);
			});
		return sum;
	}

	std::array<float, 4> HeatmapColor(std::uint32_t count, std::uint32_t rawCount, std::uint32_t maxLightsPerCluster)
	{
		if (rawCount == 0)
		{
			return { 0, 0, 0, 0 };
		}
		if (count < rawCount)
		{
			return { 1, 0, 1, 1 }; // Truncated: magenta.
		}
		const float t = std::clamp(float(count) / float(maxLightsPerCluster), 0.0f, 1.0f);
		if (t < 0.5f)
		{
			const float u = t * 2.0f;
			return { 0.0f, u, 1.0f - u, 1.0f };
		}
		const float u = (t - 0.5f) * 2.0f;
		return { u, 1.0f - u, 0.0f, 1.0f };
	}

	std::array<float, 4> HeatmapPixel(
		const ClusterGridRecord& grid, std::span<const ClusterRecord> records, float pixelX, float pixelY, float ndcDepth)
	{
		const float depth = ClusterViewDepthFromNdc(grid, ndcDepth);
		if (!(depth <= grid.DepthParams[3]))
		{
			return { 0, 0, 0, 0 };
		}
		const auto& record = records[ClusterIndexFor(grid, pixelX, pixelY, depth)];
		return HeatmapColor(record.Count, record.RawCount, grid.Limits[2]);
	}
} // namespace Swim::Render::Clustering
