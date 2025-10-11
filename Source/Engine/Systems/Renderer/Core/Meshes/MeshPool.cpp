#include "PCH.h"
#include "MeshPool.h"
#include "Engine/SwimEngine.h"

namespace Engine
{

	MeshPool& MeshPool::GetInstance()
	{
		static MeshPool instance;
		return instance;
	}

	std::shared_ptr<Mesh> MeshPool::RegisterMesh(const std::string& name, const VertexesIndexesPair& data)
	{
		return RegisterMesh(name, data.vertices, data.indices);
	}

	std::shared_ptr<Mesh> MeshPool::RegisterMesh(const std::string& name, const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices)
	{
		std::lock_guard<std::mutex> lock(poolMutex);

		// Check if the mesh already exists
		auto it = meshes.find(name);
		if (it != meshes.end())
		{
			return it->second; // Return existing mesh
		}

		// Create the mesh and its buffer data
		auto mesh = std::make_shared<Mesh>(vertices, indices);
		mesh->meshBufferData = std::make_shared<MeshBufferData>();

		// Assign a unique mesh ID
		uint32_t meshID = nextMeshID++;
		mesh->meshBufferData->meshID = meshID;

		// Cache ID <-> Mesh mapping
		meshToID[mesh] = meshID;
		idToMesh[meshID] = mesh;

		// Generate mesh buffers and its AABB and then place in the map
		mesh->meshBufferData->GenerateBuffersAndAABB(vertices, indices);
		meshes.emplace(name, mesh);

		return mesh;
	}

	std::shared_ptr<Mesh> MeshPool::GetOrCreateAndRegisterMesh
	(
		const std::string& desiredName, const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices
	)
	{
		std::lock_guard<std::mutex> lock(poolMutex);

		// First: check if a mesh with identical vertex/index data already exists
		for (const auto& [existingName, existingMesh] : meshes)
		{
			if (!existingMesh)
			{
				continue;
			}

			const auto& existingVertices = existingMesh->vertices;
			const auto& existingIndices = existingMesh->indices;

			// Quick size test
			if (existingVertices.size() != vertices.size() || existingIndices.size() != indices.size())
			{
				continue;
			}

			// Compare raw memory contents, hopefully this isn't too abysmally slow (it kinda is man so really only call this function if you have to)
			if (std::memcmp(vertices.data(), existingVertices.data(), vertices.size() * sizeof(Vertex)) == 0 &&
				std::memcmp(indices.data(), existingIndices.data(), indices.size() * sizeof(uint32_t)) == 0)
			{
				// Match found, reuse
				std::cout << "[MeshPool] Reusing mesh \"" << existingName << "\" for requested name \"" << desiredName << "\"\n";
				return existingMesh;
			}
		}

		// No matching mesh found, create new one
		auto mesh = std::make_shared<Mesh>(vertices, indices);
		mesh->meshBufferData = std::make_shared<MeshBufferData>();

		// Assign unique ID
		uint32_t meshID = nextMeshID++;
		mesh->meshBufferData->meshID = meshID;

		// Register in ID maps
		meshToID[mesh] = meshID;
		idToMesh[meshID] = mesh;

		// Upload to GPU, compute AABB
		mesh->meshBufferData->GenerateBuffersAndAABB(vertices, indices);

		// Name deduplication like TexturePool: append _1, _2, etc.
		std::string finalName = desiredName;
		int counter = 1;
		while (meshes.find(finalName) != meshes.end())
		{
			finalName = desiredName + "_" + std::to_string(counter);
			counter++;
		}

	#ifdef _DEBUG
		constexpr bool run = true;
		if constexpr (run)
		{
			if (finalName != desiredName)
			{
				std::cout << "[MeshPool] Mesh name \"" << desiredName << "\" already exists, registering as \"" << finalName << "\"\n";
			}
		}
	#endif // _DEBUG

		// Store mesh
		meshes[finalName] = mesh;
		return mesh;
	}

	std::shared_ptr<Mesh> MeshPool::GetMesh(const std::string& name) const
	{
		std::lock_guard<std::mutex> lock(poolMutex);

		auto it = meshes.find(name);
		if (it != meshes.end())
		{
			return it->second;
		}

		return nullptr; // Mesh not found
	}

	// kinda pointless because mesh has mesh buffer data which stores the id it was registered to
	uint32_t MeshPool::GetMeshID(const std::shared_ptr<Mesh>& mesh) const
	{
		std::lock_guard<std::mutex> lock(poolMutex);
		auto it = meshToID.find(mesh);
		if (it != meshToID.end())
		{
			return it->second;
		}
		return UINT32_MAX; // Invalid
	}

	std::shared_ptr<Mesh> MeshPool::GetMeshByID(uint32_t id) const
	{
		std::lock_guard<std::mutex> lock(poolMutex);
		auto it = idToMesh.find(id);
		if (it != idToMesh.end())
		{
			return it->second;
		}
		return nullptr;
	}


	bool MeshPool::RemoveMesh(const std::string& name)
	{
		std::lock_guard<std::mutex> lock(poolMutex);
		return meshes.erase(name) > 0;
	}

	void MeshPool::Flush()
	{
		std::lock_guard<std::mutex> lock(poolMutex);

		for (auto& [name, mesh] : meshes)
		{
			if (mesh && mesh->meshBufferData)
			{
				mesh->meshBufferData.reset();
			}
		}

		// Clear all meshes from the pool too
		meshes.clear();
		meshToID.clear();
		idToMesh.clear();
		nextMeshID = 0;
	}

}
