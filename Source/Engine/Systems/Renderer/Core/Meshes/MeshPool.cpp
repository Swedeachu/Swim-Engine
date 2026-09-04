#include "PCH.h"
#include "MeshPool.h"

#include <array>
#include <cstring>
#include <span>

namespace Engine
{

	namespace
	{
		Swim::Assets::ContentHash ComputeLegacyMeshContentHash(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices)
		{
			const auto vertexBytes = std::as_bytes(std::span(vertices.data(), vertices.size()));
			const auto indexBytes = std::as_bytes(std::span(indices.data(), indices.size()));
			const Swim::Assets::ContentHash vertexHash = Swim::Assets::ComputeContentHash(vertexBytes);
			const Swim::Assets::ContentHash indexHash = Swim::Assets::ComputeContentHash(indexBytes);

			std::array<std::byte, 80> combined{};
			const std::uint64_t vertexCount = static_cast<std::uint64_t>(vertices.size());
			const std::uint64_t indexCount = static_cast<std::uint64_t>(indices.size());
			std::memcpy(combined.data(), &vertexCount, sizeof(vertexCount));
			std::memcpy(combined.data() + 8, &indexCount, sizeof(indexCount));
			std::memcpy(combined.data() + 16, vertexHash.Bytes.data(), vertexHash.Bytes.size());
			std::memcpy(combined.data() + 48, indexHash.Bytes.data(), indexHash.Bytes.size());
			return Swim::Assets::ComputeContentHash(combined);
		}
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

		// Registration only creates CPU geometry. Renderer residency is requested explicitly by consumers.
		auto mesh = std::make_shared<Mesh>(vertices, indices);
		meshContentIndex[ComputeLegacyMeshContentHash(vertices, indices)] = mesh;
		meshes.emplace(name, mesh);

		return mesh;
	}

	std::shared_ptr<Mesh> MeshPool::GetOrCreateAndRegisterMesh
	(
		const std::string& desiredName, const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices
	)
	{
		std::lock_guard<std::mutex> lock(poolMutex);

		const Swim::Assets::ContentHash contentHash = ComputeLegacyMeshContentHash(vertices, indices);
		auto contentIt = meshContentIndex.find(contentHash);
		if (contentIt != meshContentIndex.end())
		{
			if (std::shared_ptr<Mesh> existingMesh = contentIt->second.lock())
			{
				return existingMesh;
			}
			meshContentIndex.erase(contentIt);
		}

		// No matching content hash found: create CPU geometry only.
		auto mesh = std::make_shared<Mesh>(vertices, indices);
		meshContentIndex[contentHash] = mesh;

		std::string finalName = desiredName;
		int counter = 1;
		while (meshes.find(finalName) != meshes.end())
		{
			finalName = desiredName + "_" + std::to_string(counter);
			counter++;
		}

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

	// Transitional legacy lookup while renderer draw records still use numeric mesh IDs.
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

	std::shared_ptr<MeshBufferData> MeshPool::GetMeshBufferData(const std::shared_ptr<Mesh>& mesh) const
	{
		std::lock_guard<std::mutex> lock(poolMutex);
		auto it = meshResidency.find(mesh);
		if (it != meshResidency.end())
		{
			return it->second;
		}
		return nullptr;
	}

	std::shared_ptr<MeshBufferData> MeshPool::RequestMeshResidency(const std::shared_ptr<Mesh>& mesh)
	{
		if (!mesh)
		{
			throw std::runtime_error("Cannot request renderer residency for a null Mesh.");
		}

		std::lock_guard<std::mutex> lock(poolMutex);
		auto existing = meshResidency.find(mesh);
		if (existing != meshResidency.end())
		{
			return existing->second;
		}

		auto residency = std::make_shared<MeshBufferData>();
		residency->meshID = nextMeshID++;
		residency->GenerateBuffersAndAABB(*renderer, mesh->vertices, mesh->indices);

		meshToID[mesh] = residency->meshID;
		idToMesh[residency->meshID] = mesh;
		meshResidency.emplace(mesh, residency);
		return residency;
	}


	bool MeshPool::RemoveMesh(const std::string& name)
	{
		std::lock_guard<std::mutex> lock(poolMutex);
		auto it = meshes.find(name);
		if (it == meshes.end())
		{
			return false;
		}

		const std::shared_ptr<Mesh> mesh = it->second;
		const Swim::Assets::ContentHash contentHash = ComputeLegacyMeshContentHash(mesh->vertices, mesh->indices);
		meshContentIndex.erase(contentHash);
		meshResidency.erase(mesh);

		auto idIt = meshToID.find(mesh);
		if (idIt != meshToID.end())
		{
			idToMesh.erase(idIt->second);
			meshToID.erase(idIt);
		}

		meshes.erase(it);
		return true;
	}

	void MeshPool::Flush()
	{
		std::lock_guard<std::mutex> lock(poolMutex);

		// Renderer residency is owned separately from CPU mesh geometry.
		meshResidency.clear();
		meshContentIndex.clear();

		// Clear all CPU meshes from the pool too.
		meshes.clear();
		meshToID.clear();
		idToMesh.clear();
		nextMeshID = 0;
	}

}
