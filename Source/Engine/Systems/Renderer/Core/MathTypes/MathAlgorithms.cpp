#include "PCH.h"
#include "MathAlgorithms.h"

namespace Engine
{

	bool RayIntersectsAABB
	(
		const Ray& ray,
		const AABB& box,
		float tMin,
		float tMax,
		float& outTNear
	)
	{
		// Slab intersections per axis
		const float t1x = (box.min.x - ray.origin.x) * ray.invDir.x;
		const float t2x = (box.max.x - ray.origin.x) * ray.invDir.x;
		float tmin = std::min(t1x, t2x);
		float tmax = std::max(t1x, t2x);

		const float t1y = (box.min.y - ray.origin.y) * ray.invDir.y;
		const float t2y = (box.max.y - ray.origin.y) * ray.invDir.y;
		tmin = std::max(tmin, std::min(t1y, t2y));
		tmax = std::min(tmax, std::max(t1y, t2y));

		const float t1z = (box.min.z - ray.origin.z) * ray.invDir.z;
		const float t2z = (box.max.z - ray.origin.z) * ray.invDir.z;
		tmin = std::max(tmin, std::min(t1z, t2z));
		tmax = std::min(tmax, std::max(t1z, t2z));

		// Clamp to provided [tMin, tMax]
		if (tmax < tmin) return false;
		if (tmax < tMin) return false;
		if (tmin > tMax) return false;

		outTNear = std::max(tmin, tMin);

		return true;
	}

	// Rotate the vector from onto to with a stable from-to quaternion.
	// Handles parallel and anti-parallel edge cases without NaNs.
	glm::quat FromToRotation(const glm::vec3& from, const glm::vec3& to)
	{
		glm::vec3 f = glm::normalize(from);
		glm::vec3 t = glm::normalize(to);

		const float c = glm::dot(f, t);
		// If vectors are almost identical
		if (c > 0.9999f)
		{
			return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
		}
		// If vectors are opposite, rotate 180 degrees around any orthogonal axis
		if (c < -0.9999f)
		{
			// Pick the largest orthogonal axis for stability
			glm::vec3 axis = glm::abs(f.x) < 0.9f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
			axis = glm::normalize(glm::cross(f, axis));
			return glm::angleAxis(glm::pi<float>(), axis);
		}

		glm::vec3 axis = glm::cross(f, t);
		const float s = glm::sqrt((1.0f + c) * 2.0f);
		const float invS = 1.0f / s;

		// quat = [w, xyz]
		return glm::quat(s * 0.5f, axis.x * invS, axis.y * invS, axis.z * invS);
	}

	float ParamOnAxisFromRay
	(
		const glm::vec3& axisOrigin,
		const glm::vec3& axisDirN,
		const glm::vec3& rayOrigin,
		const glm::vec3& rayDirN
	)
	{
		float t = 0.0f, s = 0.0f;
		ClosestParamsTwoLines(axisOrigin, axisDirN, rayOrigin, rayDirN, t, s);
		return t; // signed distance along axisDirN from axisOrigin
	}

	bool ClosestParamsTwoLines
	(
		const glm::vec3& p0, const glm::vec3& u,
		const glm::vec3& q0, const glm::vec3& v,
		float& outT, float& outS
	)
	{
		// Assumes u and v are normalized
		const float a = 1.0f;               // dot(u,u)
		const float b = glm::dot(u, v);     // dot(u,v)
		const float c = 1.0f;               // dot(v,v)
		const glm::vec3 w0 = p0 - q0;
		const float d = glm::dot(u, w0);    // dot(u, w0)
		const float e = glm::dot(v, w0);    // dot(v, w0)
		const float denom = a * c - b * b;  // = 1 - (u * v)^2

		if (std::abs(denom) < 1e-6f)
		{
			// Nearly parallel: project q0 onto axis as a fallback
			outT = glm::dot((q0 - p0), u);
			outS = 0.0f;
			return false;
		}

		outT = (b * e - c * d) / denom;
		outS = (a * e - b * d) / denom;

		return true;
	}

}
