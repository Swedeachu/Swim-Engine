#pragma once
#include "Engine/Systems/Renderer/ClusteredLighting/ClusterGrid.h"
#include "Engine/Systems/Renderer/Lights/GpuLightRecord.h"
#include "Engine/Systems/Renderer/Materials/StandardPbr.h"

#include <array>
#include <bit>
#include <span>
#include <vector>

namespace Swim::Render::Clustering
{
	// The CPU definition of clustered light assignment (items 64-65, 68). The
	// Shaders/Slang/ClusteredLighting programs mirror every function; the native
	// clustered smoke compares them cluster by cluster.
	using Float3 = std::array<float, 3>;

	struct Aabb
	{
		Float3 Min{ 0, 0, 0 };
		Float3 Max{ 0, 0, 0 };
	};

	// A local light's bounding sphere in view space; Radius < 0 marks a light that
	// misses the clustered volume (ViewLights buffer, 16 bytes per light).
	struct ViewLight
	{
		Float3 Center{ 0, 0, 0 };
		float Radius = -1.0f;
	};

	Float3 WorldToView(const ClusterGridRecord& grid, const Float3& world);
	// View-space bounds of a pixel rectangle between two view depths.
	Aabb FrustumBounds(
		const ClusterGridRecord& grid, float pixelX0, float pixelY0, float pixelX1, float pixelY1, float nearDepth, float farDepth);
	// Cluster c's view-space bounds. Slice 0 starts at the eye (depth 0) so points
	// nearer than Near stay covered; the last slice ends at Far.
	Aabb ClusterBounds(const ClusterGridRecord& grid, std::uint32_t cluster);
	// The whole clustered volume (depth 0 .. Far over the full viewport).
	Aabb VolumeBounds(const ClusterGridRecord& grid);
	bool SphereIntersectsAabb(const Float3& center, float radius, const Aabb& box);
	// Squared distance from a point to a box (0 inside).
	float DistanceSquaredToAabb(const Float3& point, const Aabb& box);

	// A local light's view-space bounding sphere (Lights::LightBoundingSphere),
	// culled (Radius -1) when it misses VolumeBounds.
	ViewLight CullLight(const ClusterGridRecord& grid, const GpuLightRecord& light);

	struct ClusterAssignment
	{
		std::vector<ViewLight> ViewLights; // Per local light.
		std::vector<Aabb> Bounds;		   // Per cluster.
		std::vector<ClusterRecord> Records;
		// ClusterCount * ClusterBlockWords(grid) uints: per cluster its occupancy words,
		// then its light bitmask words (see ClusterRecord).
		std::vector<std::uint32_t> Indices;
		ClusterStats Stats;
	};

	// Every local light is tested against every cluster; a cluster's bitmask holds every
	// light whose view sphere touches its AABB (never truncated). Record c's Offset is
	// c * ClusterBlockWords. Throws std::invalid_argument when more lights are given than
	// the grid's mask words address. Fully deterministic, like the GPU passes.
	ClusterAssignment AssignLights(const ClusterGridRecord& grid, std::span<const GpuLightRecord> rows, const GpuLightHeader& header);
	// The same assignment from precomputed view lights and cluster bounds (for
	// example the GPU's own, read back), so a comparison isolates the list building.
	ClusterAssignment AssignLights(const ClusterGridRecord& grid, std::vector<ViewLight> viewLights, std::vector<Aabb> bounds);

	// Calls visit(localIndex) for every light in a cluster's bitmask, in increasing index
	// order (ClusteredLighting.slang walks the words the same way).
	template <typename Visit>
	void ForEachClusterLight(const ClusterGridRecord& grid, std::span<const ClusterRecord> records, std::span<const std::uint32_t> words,
		std::uint32_t cluster, Visit&& visit)
	{
		const auto& record = records[cluster];
		if (record.Count == 0)
		{
			return;
		}
		const std::uint32_t occupancyWords = ClusterOccupancyWords(grid);
		for (std::uint32_t o = 0; o < occupancyWords; ++o)
		{
			std::uint32_t occupancy = words[record.Offset + o];
			while (occupancy != 0)
			{
				const std::uint32_t w = o * 32u + static_cast<std::uint32_t>(std::countr_zero(occupancy));
				occupancy &= occupancy - 1u;
				std::uint32_t mask = words[record.Offset + occupancyWords + w];
				while (mask != 0)
				{
					visit(w * 32u + static_cast<std::uint32_t>(std::countr_zero(mask)));
					mask &= mask - 1u;
				}
			}
		}
	}

	// A cluster's local light indices in increasing order.
	std::vector<std::uint32_t> ClusterLightList(const ClusterAssignment& assignment, const ClusterGridRecord& grid, std::uint32_t cluster);

	// Clustered forward shading of one point: every directional light plus the
	// cluster's local lights, each StandardPbr::EvaluateBrdf times its radiance.
	// It equals Lights::ShadeAllLights (the assignment is conservative and complete).
	Float3 ShadeClustered(const ClusterAssignment& assignment, const ClusterGridRecord& grid, std::span<const GpuLightRecord> rows,
		const GpuLightHeader& header, const StandardPbr::Surface& surface, const Float3& normal, const Float3& view,
		const Float3& worldPosition, float pixelX, float pixelY, float viewDepth);

	// Debug heatmap color of a cluster (item 68): transparent for no lights, magenta
	// when the list was truncated, otherwise a blue -> green -> red ramp of
	// count / MaxLightsPerCluster.
	std::array<float, 4> HeatmapColor(std::uint32_t count, std::uint32_t rawCount, std::uint32_t maxLightsPerCluster);
	// The heatmap of one depth-buffer pixel: its cluster's color, transparent past Far.
	std::array<float, 4> HeatmapPixel(
		const ClusterGridRecord& grid, std::span<const ClusterRecord> records, float pixelX, float pixelY, float ndcDepth);
} // namespace Swim::Render::Clustering
