#pragma once

#include <cstring>
#include <cstdint>

#include "Library/glm/glm.hpp"
#include "Library/glm/gtc/type_ptr.hpp"
#include "Engine/Components/Internal/FrustumCullCache.h"
#include "Engine/Systems/Renderer/Core/MathTypes/AABB.h"

namespace Engine
{

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

		bool IsAABBVisible(const AABB& aabb, uint8_t& planeHint) const
		{
			const uint8_t firstPlane = planeHint < 6 ? planeHint : 0;

			if (IsAABBOutsidePlane(aabb, planes[firstPlane]))
			{
				planeHint = firstPlane;
				return false;
			}

			for (uint8_t i = 0; i < 6; ++i)
			{
				if (i == firstPlane)
				{
					continue;
				}

				if (IsAABBOutsidePlane(aabb, planes[i]))
				{
					planeHint = i;
					return false;
				}
			}

			planeHint = firstPlane;
			return true;
		}

		// This is actually the best method to use right now
		bool IsVisibleLazy(const glm::vec4& aabbMin, const glm::vec4& aabbMax, const glm::mat4& model) const
		{
			AABB worldAABB;

			glm::vec3 worldMin = glm::vec3(model * aabbMin);
			glm::vec3 worldMax = worldMin;

			const glm::vec3 localMin = glm::vec3(aabbMin);
			const glm::vec3 localMax = glm::vec3(aabbMax);

			const glm::vec3 corners[8] = {
				glm::vec3(model * glm::vec4(localMin.x, localMin.y, localMin.z, 1.0f)),
				glm::vec3(model * glm::vec4(localMax.x, localMin.y, localMin.z, 1.0f)),
				glm::vec3(model * glm::vec4(localMin.x, localMax.y, localMin.z, 1.0f)),
				glm::vec3(model * glm::vec4(localMax.x, localMax.y, localMin.z, 1.0f)),
				glm::vec3(model * glm::vec4(localMin.x, localMin.y, localMax.z, 1.0f)),
				glm::vec3(model * glm::vec4(localMax.x, localMin.y, localMax.z, 1.0f)),
				glm::vec3(model * glm::vec4(localMin.x, localMax.y, localMax.z, 1.0f)),
				glm::vec3(model * glm::vec4(localMax.x, localMax.y, localMax.z, 1.0f))
			};

			for (int i = 1; i < 8; ++i)
			{
				worldMin = glm::min(worldMin, corners[i]);
				worldMax = glm::max(worldMax, corners[i]);
			}

			worldAABB.min = worldMin;
			worldAABB.max = worldMax;

			return IsAABBVisible(worldAABB);
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
			const bool visible = IsAABBVisible(worldAABB, planeHint);

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
