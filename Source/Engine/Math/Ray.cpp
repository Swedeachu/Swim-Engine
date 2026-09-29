#include "Engine/Math/Ray.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Engine::RayQueries
{

	namespace
	{

		RayHit Make(const Ray& ray, float t, const glm::vec3& normal)
		{
			RayHit hit;
			hit.T = t;
			hit.Point = ray.At(t);
			const float length = glm::length(normal);
			glm::vec3 n = length > 0.0f ? normal / length : glm::vec3(0.0f, 1.0f, 0.0f);

			if (glm::dot(n, ray.Direction) > 0.0f)
			{
				n = -n; // Face the ray's origin side.
			}

			hit.Normal = n;
			return hit;
		}

		// A hit found in a local space mapped back: same t, world point and normal.
		std::optional<RayHit> Back(const Ray& ray, const glm::mat4& world, const std::optional<RayHit>& local)
		{
			if (!local)
			{
				return std::nullopt;
			}

			const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(world)));
			return Make(ray, local->T, normalMatrix * local->Normal);
		}

		std::optional<Ray> ToLocal(const Ray& ray, const glm::mat4& world)
		{
			const float determinant = glm::determinant(world);

			if (!(std::abs(determinant) > 1.0e-20f) || !std::isfinite(determinant))
			{
				return std::nullopt;
			}

			const glm::mat4 inverse = glm::inverse(world);
			Ray local;
			local.Origin = glm::vec3(inverse * glm::vec4(ray.Origin, 1.0f));
			local.Direction = glm::vec3(inverse * glm::vec4(ray.Direction, 0.0f)); // Unnormalized: t is preserved.
			return local;
		}

	} // namespace

	std::optional<RayHit> Plane(const Ray& ray, const glm::vec3& point, const glm::vec3& normal)
	{
		const float denominator = glm::dot(normal, ray.Direction);

		if (std::abs(denominator) < 1.0e-8f)
		{
			return std::nullopt;
		}

		const float t = glm::dot(normal, point - ray.Origin) / denominator;

		if (!(t >= 0.0f))
		{
			return std::nullopt;
		}

		return Make(ray, t, normal);
	}

	std::optional<RayHit> Sphere(const Ray& ray, const glm::vec3& centre, float radius)
	{
		const glm::vec3 offset = ray.Origin - centre;
		const float a = glm::dot(ray.Direction, ray.Direction);
		const float b = glm::dot(offset, ray.Direction);
		const float c = glm::dot(offset, offset) - radius * radius;
		const float discriminant = b * b - a * c;

		if (!(a > 0.0f) || discriminant < 0.0f)
		{
			return std::nullopt;
		}

		const float root = std::sqrt(discriminant);
		float t = (-b - root) / a;

		if (t < 0.0f)
		{
			t = (-b + root) / a; // Inside: the far wall.
		}

		if (t < 0.0f)
		{
			return std::nullopt;
		}

		return Make(ray, t, ray.At(t) - centre);
	}

	std::optional<RayHit> Aabb(const Ray& ray, const glm::vec3& minimum, const glm::vec3& maximum)
	{
		float enter = -std::numeric_limits<float>::infinity();
		float exit = std::numeric_limits<float>::infinity();
		int enterAxis = -1;
		int exitAxis = -1;

		for (int axis = 0; axis < 3; ++axis)
		{
			if (std::abs(ray.Direction[axis]) < 1.0e-12f)
			{
				if (ray.Origin[axis] < minimum[axis] || ray.Origin[axis] > maximum[axis])
				{
					return std::nullopt;
				}

				continue;
			}

			float t0 = (minimum[axis] - ray.Origin[axis]) / ray.Direction[axis];
			float t1 = (maximum[axis] - ray.Origin[axis]) / ray.Direction[axis];

			if (t0 > t1)
			{
				std::swap(t0, t1);
			}

			if (t0 > enter)
			{
				enter = t0;
				enterAxis = axis;
			}

			if (t1 < exit)
			{
				exit = t1;
				exitAxis = axis;
			}
		}

		if (enter > exit || exit < 0.0f)
		{
			return std::nullopt;
		}

		const bool inside = enter < 0.0f;
		const float t = inside ? exit : enter;
		const int axis = inside ? exitAxis : enterAxis;
		glm::vec3 normal(0.0f);

		if (axis >= 0)
		{
			normal[axis] = 1.0f;
		}

		return Make(ray, t, normal);
	}

	std::optional<RayHit> Box(const Ray& ray, const glm::mat4& world, const glm::vec3& halfExtents)
	{
		const auto local = ToLocal(ray, world);
		return local ? Back(ray, world, Aabb(*local, -halfExtents, halfExtents)) : std::nullopt;
	}

	std::optional<RayHit> TransformedSphere(const Ray& ray, const glm::mat4& world, float radius)
	{
		const auto local = ToLocal(ray, world);
		return local ? Back(ray, world, Sphere(*local, glm::vec3(0.0f), radius)) : std::nullopt;
	}

	std::optional<RayHit> TransformedCapsule(const Ray& ray, const glm::mat4& world, float radius, float halfHeight)
	{
		const auto local = ToLocal(ray, world);

		if (!local)
		{
			return std::nullopt;
		}

		// The two end spheres and the cylinder between them; the nearest non-negative hit.
		std::optional<RayHit> best;
		const auto keep = [&](const std::optional<RayHit>& hit)
		{
			if (hit && (!best || hit->T < best->T))
			{
				best = hit;
			}
		};
		keep(Sphere(*local, glm::vec3(0.0f, halfHeight, 0.0f), radius));
		keep(Sphere(*local, glm::vec3(0.0f, -halfHeight, 0.0f), radius));
		const glm::vec2 o(local->Origin.x, local->Origin.z);
		const glm::vec2 d(local->Direction.x, local->Direction.z);
		const float a = glm::dot(d, d);

		if (a > 1.0e-12f)
		{
			const float b = glm::dot(o, d);
			const float c = glm::dot(o, o) - radius * radius;
			const float discriminant = b * b - a * c;

			if (discriminant >= 0.0f)
			{
				const float root = std::sqrt(discriminant);

				for (const float t : { (-b - root) / a, (-b + root) / a })
				{
					const glm::vec3 p = local->At(t);

					if (t >= 0.0f && std::abs(p.y) <= halfHeight)
					{
						keep(Make(*local, t, glm::vec3(p.x, 0.0f, p.z)));
						break;
					}
				}
			}
		}

		return Back(ray, world, best);
	}

	std::optional<RayHit> Triangle(const Ray& ray, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c)
	{
		const glm::vec3 e1 = b - a;
		const glm::vec3 e2 = c - a;
		const glm::vec3 p = glm::cross(ray.Direction, e2);
		const float determinant = glm::dot(e1, p);

		if (std::abs(determinant) < 1.0e-12f)
		{
			return std::nullopt;
		}

		const float inverse = 1.0f / determinant;
		const glm::vec3 s = ray.Origin - a;
		const float u = glm::dot(s, p) * inverse;

		if (u < 0.0f || u > 1.0f)
		{
			return std::nullopt;
		}

		const glm::vec3 q = glm::cross(s, e1);
		const float v = glm::dot(ray.Direction, q) * inverse;

		if (v < 0.0f || u + v > 1.0f)
		{
			return std::nullopt;
		}

		const float t = glm::dot(e2, q) * inverse;

		if (t < 0.0f)
		{
			return std::nullopt;
		}

		return Make(ray, t, glm::cross(e1, e2));
	}

	float ClosestT(const Ray& ray, const glm::vec3& point)
	{
		const float a = glm::dot(ray.Direction, ray.Direction);
		return a > 0.0f ? std::max(glm::dot(point - ray.Origin, ray.Direction) / a, 0.0f) : 0.0f;
	}

} // namespace Engine::RayQueries
