#pragma once
#include "Library/glm/glm.hpp"

// Internal components are components the gameplay programmers making the game should never have to worry about, 
// the engine makes them for its self only to do optimized things like spacial partions.
// However I can see this holding useful information for gameplay systems to leverage.

namespace Engine
{

	struct FrustumCullCache
	{
		glm::vec3 worldCorners[8];          // Transformed AABB corners in world space
		uint8_t negativeVertexIndices[6];   // Cached per-plane negative corner index

		glm::vec3 lastAABBMin = glm::vec3(0.0f);
		glm::vec3 lastAABBMax = glm::vec3(0.0f);
		glm::mat4 lastModelMatrix = glm::mat4(0.0f);

		bool valid = false;

		// Recomputes transformed corners and negative vertex indices if needed
		void Update(const glm::vec3& aabbMin, const glm::vec3& aabbMax, const glm::mat4& modelMatrix)
		{
			if (valid &&
				lastAABBMin == aabbMin &&
				lastAABBMax == aabbMax &&
				lastModelMatrix == modelMatrix)
			{
				return;
			}

			lastAABBMin = aabbMin;
			lastAABBMax = aabbMax;
			lastModelMatrix = modelMatrix;

			// Recompute world-space corners
			worldCorners[0] = glm::vec3(modelMatrix * glm::vec4(aabbMin.x, aabbMin.y, aabbMin.z, 1.0f));
			worldCorners[1] = glm::vec3(modelMatrix * glm::vec4(aabbMax.x, aabbMin.y, aabbMin.z, 1.0f));
			worldCorners[2] = glm::vec3(modelMatrix * glm::vec4(aabbMin.x, aabbMax.y, aabbMin.z, 1.0f));
			worldCorners[3] = glm::vec3(modelMatrix * glm::vec4(aabbMax.x, aabbMax.y, aabbMin.z, 1.0f));
			worldCorners[4] = glm::vec3(modelMatrix * glm::vec4(aabbMin.x, aabbMin.y, aabbMax.z, 1.0f));
			worldCorners[5] = glm::vec3(modelMatrix * glm::vec4(aabbMax.x, aabbMin.y, aabbMax.z, 1.0f));
			worldCorners[6] = glm::vec3(modelMatrix * glm::vec4(aabbMin.x, aabbMax.y, aabbMax.z, 1.0f));
			worldCorners[7] = glm::vec3(modelMatrix * glm::vec4(aabbMax.x, aabbMax.y, aabbMax.z, 1.0f));

			// Precompute negative vertex indices for common frustum plane orientations
			const glm::vec3 normals[6] = {
				{  1,  0,  0 },  // Left
				{ -1,  0,  0 },  // Right
				{  0,  1,  0 },  // Bottom
				{  0, -1,  0 },  // Top
				{  0,  0,  1 },  // Near
				{  0,  0, -1 }   // Far
			};

			for (int i = 0; i < 6; ++i)
			{
				negativeVertexIndices[i] = ComputeNegativeVertexIndex(normals[i]);
			}

			valid = true;
		}

		// Inline: returns cached negative vertex for a specific frustum plane
		inline glm::vec3 GetNegativeVertex(int planeIndex) const
		{
			return worldCorners[negativeVertexIndices[planeIndex]];
		}

	private:

		// Computes 3-bit corner index (0-7) based on a normal vector
		inline uint8_t ComputeNegativeVertexIndex(const glm::vec3& normal) const
		{
			uint8_t x = normal.x >= 0.0f ? 0 : 1;
			uint8_t y = normal.y >= 0.0f ? 0 : 1;
			uint8_t z = normal.z >= 0.0f ? 0 : 1;
			return static_cast<uint8_t>((x << 2) | (y << 1) | z);
		}
	};

}
