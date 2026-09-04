#pragma once

#include <cstring>
#include <cstdint>

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "Engine/Components/Internal/FrustumCullCache.h"
#include "Engine/Systems/Renderer/Core/MathTypes/AABB.h"

namespace Engine
{

	enum class AABBFrustumClassification : uint8_t
	{
		Outside = 0,
		Intersecting = 1,
		Inside = 2
	};

	struct Frustum
	{
		glm::vec4 planes[6]{}; // ax + by + cz + d = 0

		std::uint64_t GetRevision() const { return revision; }
		bool DidCameraMoveThisFrame() const { return cameraMovedThisFrame; }

		// Updates this specific view's frustum state. No process-global camera/frustum
		// cache exists, so multiple views can maintain independent revisions/history.
		void Update(const glm::mat4& view, const glm::mat4& proj)
		{
			const glm::mat4 newVP = proj * view;
			if (!MatricesEqual(newVP, lastVP))
			{
				lastVP = newVP;
				ComputeFromMatrix(newVP);
				revision = HashMatrix(newVP);
				if (revision == 0)
				{
					revision = 1;
				}
				cameraMovedThisFrame = true;
			}
			else
			{
				cameraMovedThisFrame = false;
			}
		}

		// === Accurate method: tests all corners, very slow though ===
		bool IsVisiblePerfectSlow(const glm::vec3& aabbMin, const glm::vec3& aabbMax, const glm::mat4& model) const
		{
			glm::vec3 corners[8] = {
				glm::vec3(model * glm::vec4(aabbMin.x, aabbMin.y, aabbMin.z, 1.0f)),
				glm::vec3(model * glm::vec4(aabbMax.x, aabbMin.y, aabbMin.z, 1.0f)),
				glm::vec3(model * glm::vec4(aabbMin.x, aabbMax.y, aabbMin.z, 1.0f)),
				glm::vec3(model * glm::vec4(aabbMax.x, aabbMax.y, aabbMin.z, 1.0f)),
				glm::vec3(model * glm::vec4(aabbMin.x, aabbMin.y, aabbMax.z, 1.0f)),
				glm::vec3(model * glm::vec4(aabbMax.x, aabbMin.y, aabbMax.z, 1.0f)),
				glm::vec3(model * glm::vec4(aabbMin.x, aabbMax.y, aabbMax.z, 1.0f)),
				glm::vec3(model * glm::vec4(aabbMax.x, aabbMax.y, aabbMax.z, 1.0f))
			};

			for (int i = 0; i < 6; ++i)
			{
				int outside = 0;
				for (int j = 0; j < 8; ++j)
				{
					if (glm::dot(glm::vec3(planes[i]), corners[j]) + planes[i].w < 0.0f)
					{
						++outside;
					}
				}
				if (outside == 8)
				{
					return false;
				}
			}

			return true;
		}

		bool IsAABBVisible(const AABB& aabb) const
		{
			uint8_t planeHint = 0;
			return IsAABBVisible(aabb, planeHint);
		}

		bool ContainsAABB(const AABB& aabb) const
		{
			uint8_t planeHint = 0;
			return ContainsAABB(aabb, planeHint);
		}

		bool ContainsAABB(const AABB& aabb, uint8_t& planeHint) const
		{
			return ClassifyAABB(aabb, planeHint) == AABBFrustumClassification::Inside;
		}

		bool IsAABBVisible(const AABB& aabb, uint8_t& planeHint) const
		{
			return ClassifyAABB(aabb, planeHint) != AABBFrustumClassification::Outside;
		}

		AABBFrustumClassification ClassifyAABB(const AABB& aabb) const
		{
			uint8_t planeHint = 0;
			return ClassifyAABB(aabb, planeHint);
		}

		AABBFrustumClassification ClassifyAABB(const AABB& aabb, uint8_t& planeHint) const
		{
			const uint8_t firstPlane = planeHint < 6 ? planeHint : 0;
			bool fullyInside = true;

			for (uint8_t pass = 0; pass < 6; ++pass)
			{
				const uint8_t planeIndex = (pass == 0) ? firstPlane : static_cast<uint8_t>((pass <= firstPlane) ? (pass - 1) : pass);
				const glm::vec4& plane = planes[planeIndex];

				if (IsAABBOutsidePlane(aabb, plane))
				{
					planeHint = planeIndex;
					return AABBFrustumClassification::Outside;
				}

				fullyInside = fullyInside && IsAABBInsidePlane(aabb, plane);
			}

			planeHint = firstPlane;
			return fullyInside ? AABBFrustumClassification::Inside : AABBFrustumClassification::Intersecting;
		}

		// This is actually the best method to use right now
		bool IsVisibleLazy(const glm::vec4& aabbMin, const glm::vec4& aabbMax, const glm::mat4& model) const
		{
			return IsAABBVisible(BuildWorldAABB(glm::vec3(aabbMin), glm::vec3(aabbMax), model));
		}

		bool IsVisibleCached(FrustumCullCache& cache, const glm::vec3& aabbMin, const glm::vec3& aabbMax, const glm::mat4& model, uint64_t transformVersion) const
		{
			cache.Update(aabbMin, aabbMax, model, transformVersion);
			return IsVisibleCached(cache, cache.GetWorldAABB(), transformVersion);
		}

		// Uses the internal engine component every entity with a mesh and transform gets assigned silently
		bool IsVisibleCached(const FrustumCullCache& cache) const
		{
			AABB worldAABB;
			worldAABB.min = cache.lastWorldAABBMin;
			worldAABB.max = cache.lastWorldAABBMax;
			return IsAABBVisible(worldAABB);
		}

		bool IsVisibleCached(FrustumCullCache& cache, const AABB& worldAABB, uint64_t transformVersion) const
		{
			if (cache.HasReusableResult(worldAABB.min, worldAABB.max, transformVersion, revision))
			{
				return cache.lastVisible;
			}

			uint8_t planeHint = cache.lastRejectedPlane;
			const AABBFrustumClassification classification = ClassifyAABB(worldAABB, planeHint);
			const bool visible = classification != AABBFrustumClassification::Outside;

			cache.lastTransformVersion = transformVersion;
			cache.lastFrustumRevision = revision;
			cache.lastRejectedPlane = planeHint;
			cache.lastWorldAABBMin = worldAABB.min;
			cache.lastWorldAABBMax = worldAABB.max;
			cache.lastVisible = visible;
			cache.hasVisibilityHistory = true;

			return visible;
		}

	private:

		void ComputeFromMatrix(const glm::mat4& vp)
		{
			planes[0] = glm::vec4(vp[0][3] + vp[0][0], vp[1][3] + vp[1][0], vp[2][3] + vp[2][0], vp[3][3] + vp[3][0]); // Left
			planes[1] = glm::vec4(vp[0][3] - vp[0][0], vp[1][3] - vp[1][0], vp[2][3] - vp[2][0], vp[3][3] - vp[3][0]); // Right
			planes[2] = glm::vec4(vp[0][3] + vp[0][1], vp[1][3] + vp[1][1], vp[2][3] + vp[2][1], vp[3][3] + vp[3][1]); // Bottom
			planes[3] = glm::vec4(vp[0][3] - vp[0][1], vp[1][3] - vp[1][1], vp[2][3] - vp[2][1], vp[3][3] - vp[3][1]); // Top
			planes[4] = glm::vec4(vp[0][3] + vp[0][2], vp[1][3] + vp[1][2], vp[2][3] + vp[2][2], vp[3][3] + vp[3][2]); // Near
			planes[5] = glm::vec4(vp[0][3] - vp[0][2], vp[1][3] - vp[1][2], vp[2][3] - vp[2][2], vp[3][3] - vp[3][2]); // Far

			for (int i = 0; i < 6; ++i)
			{
				const float len = glm::length(glm::vec3(planes[i]));
				if (len > 0.0f)
				{
					planes[i] /= len;
				}
			}
		}

		static bool IsAABBOutsidePlane(const AABB& aabb, const glm::vec4& plane)
		{
			return (
				plane.x * ((plane.x >= 0.0f) ? aabb.max.x : aabb.min.x)
				+ plane.y * ((plane.y >= 0.0f) ? aabb.max.y : aabb.min.y)
				+ plane.z * ((plane.z >= 0.0f) ? aabb.max.z : aabb.min.z)
				+ plane.w
				) < 0.0f;
		}

		static bool IsAABBInsidePlane(const AABB& aabb, const glm::vec4& plane)
		{
			return (
				plane.x * ((plane.x >= 0.0f) ? aabb.min.x : aabb.max.x)
				+ plane.y * ((plane.y >= 0.0f) ? aabb.min.y : aabb.max.y)
				+ plane.z * ((plane.z >= 0.0f) ? aabb.min.z : aabb.max.z)
				+ plane.w
				) >= 0.0f;
		}

	public:

		static AABB BuildWorldAABB(const glm::vec3& localMin, const glm::vec3& localMax, const glm::mat4& model)
		{
			const glm::vec3 localCenter = 0.5f * (localMin + localMax);
			const glm::vec3 localExtents = 0.5f * glm::max(localMax - localMin, glm::vec3(0.0f));
			const glm::vec3 worldCenter = glm::vec3(model * glm::vec4(localCenter, 1.0f));

			glm::mat3 absBasis(1.0f);
			for (int column = 0; column < 3; ++column)
			{
				absBasis[column] = glm::abs(glm::vec3(model[column]));
			}

			const glm::vec3 worldExtents = absBasis * localExtents;

			AABB worldAABB;
			worldAABB.min = worldCenter - worldExtents;
			worldAABB.max = worldCenter + worldExtents;
			return worldAABB;
		}

	private:

		static bool MatricesEqual(const glm::mat4& a, const glm::mat4& b)
		{
			return std::memcmp(glm::value_ptr(a), glm::value_ptr(b), sizeof(glm::mat4)) == 0;
		}

		static std::uint64_t HashMatrix(const glm::mat4& matrix)
		{
			const auto* bytes = reinterpret_cast<const std::uint8_t*>(glm::value_ptr(matrix));
			std::uint64_t hash = 1469598103934665603ull;
			for (std::size_t i = 0; i < sizeof(glm::mat4); ++i)
			{
				hash ^= static_cast<std::uint64_t>(bytes[i]);
				hash *= 1099511628211ull;
			}
			return hash;
		}

		glm::mat4 lastVP{ 0.0f };
		std::uint64_t revision = 1;
		bool cameraMovedThisFrame = true;
	};

}
