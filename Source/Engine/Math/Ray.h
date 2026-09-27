#pragma once

#include <glm/glm.hpp>

#include <optional>

// Rays and ray/shape intersections (world or any space; right-handed, +Y up). The
// foundation of picking, placement and interaction code: Camera builds rays from screen
// points, ScenePicking tests them against entities, and gameplay intersects them with
// planes (dragging), spheres, boxes and triangles.
namespace Engine
{
	// A ray: Origin + t * Direction for t >= 0. Direction is normalized by the helpers that
	// build rays (Camera::ScreenPointToRay); the intersection functions accept any non-zero
	// direction and report t in its units (Distance = t * |Direction|).
	struct Ray
	{
		glm::vec3 Origin{ 0.0f };
		glm::vec3 Direction{ 0.0f, 0.0f, -1.0f };

		glm::vec3 At(float t) const { return Origin + Direction * t; }
	};

	struct RayHit
	{
		float T = 0.0f;			  // Ray parameter of the hit (>= 0).
		glm::vec3 Point{ 0.0f };  // Origin + T * Direction.
		glm::vec3 Normal{ 0.0f }; // Unit surface normal facing the ray's origin side.
	};

	namespace RayQueries
	{
		// The plane through `point` with normal `normal` (either side); nothing when the ray
		// is parallel to it or the plane is behind the origin.
		std::optional<RayHit> Plane(const Ray& ray, const glm::vec3& point, const glm::vec3& normal);
		// Nearest front hit; a ray starting inside hits the far wall (normal facing inward).
		std::optional<RayHit> Sphere(const Ray& ray, const glm::vec3& centre, float radius);
		// Axis-aligned box (slab test); starting inside hits the exit face.
		std::optional<RayHit> Aabb(const Ray& ray, const glm::vec3& minimum, const glm::vec3& maximum);
		// A box of half extents in the space of `world` (any affine transform with scale): the
		// ray is intersected in that space, so scaled, rotated and sheared boxes are exact.
		std::optional<RayHit> Box(const Ray& ray, const glm::mat4& world, const glm::vec3& halfExtents);
		// Sphere and capsule (axis local Y, half height of the cylinder part) in `world`'s
		// space: non-uniform scale gives the ellipsoid / stretched capsule the mesh shows.
		std::optional<RayHit> TransformedSphere(const Ray& ray, const glm::mat4& world, float radius);
		std::optional<RayHit> TransformedCapsule(const Ray& ray, const glm::mat4& world, float radius, float halfHeight);
		// Möller-Trumbore, both faces.
		std::optional<RayHit> Triangle(const Ray& ray, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c);
		// The ray parameter (clamped to >= 0) of the point on the ray closest to `point`.
		float ClosestT(const Ray& ray, const glm::vec3& point);
	} // namespace RayQueries
} // namespace Engine
