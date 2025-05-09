#pragma once

#include "Library/glm/glm.hpp"
#include "Library/glm/gtc/type_ptr.hpp"
// #include <cstring> // std::memcmp
#include "Engine/Components/Internal/FrustumCullCache.h"

namespace Engine
{

	struct Frustum
	{
		glm::vec4 planes[6]; // ax + by + cz + d = 0

		inline static const Frustum& Get()
		{
			return cachedFrustum;
		}

		// === Setup camera frustum from view/proj matrices (once per frame) ===
		static void SetCameraMatrices(const glm::mat4& view, const glm::mat4& proj)
		{
			glm::mat4 newVP = proj * view;

			if (!MatricesEqual(newVP, lastVP))
			{
				lastVP = newVP;
				lastView = view;
				cachedFrustum = ComputeFromMatrix(newVP);
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

		// This is actually the best method to use right now
		bool IsVisibleLazy(const glm::vec3& aabbMin, const glm::vec3& aabbMax, const glm::mat4& model) const
		{
			constexpr float threeHalves = 1.5f;

			glm::vec3 worldMin = glm::vec3(model * glm::vec4(aabbMin, 1.0f));
			glm::vec3 worldMax = glm::vec3(model * glm::vec4(aabbMax, 1.0f));

			for (int i = 0; i < 6; ++i)
			{
				const glm::vec3 normal = glm::vec3(planes[i]);

				glm::vec3 negativeVertex = {
					normal.x >= 0.0f ? worldMin.x : worldMax.x,
					normal.y >= 0.0f ? worldMin.y : worldMax.y,
					normal.z >= 0.0f ? worldMin.z : worldMax.z
				};

				if (glm::dot(normal, negativeVertex) + planes[i].w < -threeHalves)
				{
					return false;
				}
			}

			return true;
		}

		// Uses the internal engine component every entity with a mesh and transform gets assigned silently
		bool IsVisibleCached(const FrustumCullCache& cache) const
		{
			constexpr float threeHalves = 1.5f;

			for (int i = 0; i < 6; ++i)
			{
				if (glm::dot(glm::vec3(planes[i]), cache.GetNegativeVertex(i)) + planes[i].w < -threeHalves)
				{
					return false;
				}
			}

			return true;
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

		static bool MatricesEqual(const glm::mat4& a, const glm::mat4& b)
		{
			return std::memcmp(glm::value_ptr(a), glm::value_ptr(b), sizeof(glm::mat4)) == 0;
		}

		// Static cached state
		static glm::mat4 lastVP;
		static glm::mat4 lastView;
		static Frustum cachedFrustum;
	};

	// Static member initialization
	inline glm::mat4 Frustum::lastVP = glm::mat4(0.0f);
	inline glm::mat4 Frustum::lastView = glm::mat4(1.0f);
	inline Frustum Frustum::cachedFrustum = {};

}
