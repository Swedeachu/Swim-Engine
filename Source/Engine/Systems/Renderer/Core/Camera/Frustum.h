#pragma once

#include <cstring>
#include <cstdint>

#include "Library/glm/glm.hpp"
#include "Library/glm/gtc/type_ptr.hpp"
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
		glm::vec4 planes[6]; // ax + by + cz + d = 0

		inline static const Frustum& Get()
		{
			return cachedFrustum;
		}

		inline static uint64_t GetRevision()
		{
			return revision;
		}

		// === Setup camera frustum from view/proj matrices (once per frame) ===
		static void SetCameraMatrices(const glm::mat4& view, const glm::mat4& proj)
		{
			glm::mat4 newVP = proj * view;
			// Psuedo dirty flag to check if we even need to recompute by checking if the matrices memory bounds are equal
			if (!MatricesEqual(newVP, lastVP))
			{
				lastVP = newVP;
				lastView = view;
				cachedFrustum = ComputeFromMatrix(newVP);
				cameraMovedThisFrame = true;
				++revision;
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

		static Frustum ComputeFromMatrix(const glm::mat4& vp)
		{
			Frustum f;

			f.planes[0] = glm::vec4(vp[0][3] + vp[0][0], vp[1][3] + vp[1][0], vp[2][3] + vp[2][0], vp[3][3] + vp[3][0]); // Left
			f.planes[1] = glm::vec4(vp[0][3] - vp[0][0], vp[1][3] - vp[1][0], vp[2][3] - vp[2][0], vp[3][3] - vp[3][0]); // Right
			f.planes[2] = glm::vec4(vp[0][3] + vp[0][1], vp[1][3] + vp[1][1], vp[2][3] + vp[2][1], vp[3][3] + vp[3][1]); // Bottom
			f.planes[3] = glm::vec4(vp[0][3] - vp[0][1], vp[1][3] - vp[1][1], vp[2][3] - vp[2][1], vp[3][3] - vp[3][1]); // Top
			f.planes[4] = glm::vec4(vp[0][3] + vp[0][2], vp[1][3] + vp[1][2], vp[2][3] + vp[2][2], vp[3][3] + vp[3][2]); // Near
			f.planes[5] = glm::vec4(vp[0][3] - vp[0][2], vp[1][3] - vp[1][2], vp[2][3] - vp[2][2], vp[3][3] - vp[3][2]); // Far

			for (int i = 0; i < 6; ++i)
			{
				float len = glm::length(glm::vec3(f.planes[i]));
				if (len > 0.0f)
				{
					f.planes[i] /= len;
				}
			}

			return f;
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

		// Static cached state
		static glm::mat4 lastVP;
		static glm::mat4 lastView;
		static Frustum cachedFrustum;
		static uint64_t revision;
		static bool cameraMovedThisFrame;
	};

	// Static member initialization
	inline glm::mat4 Frustum::lastVP = glm::mat4(0.0f);
	inline glm::mat4 Frustum::lastView = glm::mat4(1.0f);
	inline Frustum Frustum::cachedFrustum = {};
	inline uint64_t Frustum::revision = 1;
	inline bool Frustum::cameraMovedThisFrame = true;

}
