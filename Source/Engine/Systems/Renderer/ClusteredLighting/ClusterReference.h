#pragma once
#include "Engine/Systems/Renderer/ClusteredLighting/ClusterGrid.h"
#include "Engine/Systems/Renderer/Lights/GpuLightRecord.h"
#include "Engine/Systems/Renderer/Materials/StandardPbr.h"

#include <array>
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
		std::vector<std::uint32_t> Indices; // Local light indices (row - FirstLocalRow), WrittenIndices of them.
		ClusterStats Stats;
	};

	// Every local light is tested against every cluster in light-index order; a
	// cluster keeps its first MaxLightsPerCluster hits. Offsets are the exclusive
	// prefix sum of the kept counts in cluster order, clamped to IndexCapacity (later
	// clusters lose their lists first). Fully deterministic, like the GPU passes.
	ClusterAssignment AssignLights(const ClusterGridRecord& grid, std::span<const GpuLightRecord> rows, const GpuLightHeader& header);
	// The same assignment from precomputed view lights and cluster bounds (for
	// example the GPU's own, read back), so a comparison isolates the list building.
	ClusterAssignment AssignLights(const ClusterGridRecord& grid, std::vector<ViewLight> viewLights, std::vector<Aabb> bounds);

	// Clustered forward shading of one point: every directional light plus the
	// cluster's local lights, each StandardPbr::EvaluateBrdf times its radiance.
	// Without truncation it equals Lights::ShadeAllLights.
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
