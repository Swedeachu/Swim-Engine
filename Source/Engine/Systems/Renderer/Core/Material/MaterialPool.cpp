#include "PCH.h"
#include "MaterialPool.h"
#include "Engine/Assets/AssetSystem.h"
#include "Engine/Assets/MaterialAsset.h"
#include "Engine/Assets/MeshAsset.h"
#include "Engine/Assets/ModelAsset.h"
#include "Engine/Systems/Renderer/Core/Material/LegacyRenderBinding.h"
#include "Engine/Systems/Renderer/Core/Meshes/MeshPool.h"
#include "Engine/Systems/Renderer/Core/Textures/TexturePool.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <unordered_map>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Engine
{

	namespace
	{
		const char* AssetLoadStateName(Swim::Assets::AssetLoadState state)
		{
			switch (state)
			{
				case Swim::Assets::AssetLoadState::Unloaded: return "Unloaded";
				case Swim::Assets::AssetLoadState::Queued: return "Queued";
				case Swim::Assets::AssetLoadState::Loading: return "Loading";
				case Swim::Assets::AssetLoadState::Resident: return "Resident";
				case Swim::Assets::AssetLoadState::Failed: return "Failed";
			}
			return "Unknown";
		}

		std::string CookedModelLogicalPath(const std::string& sourcePath)
		{
			std::string normalized = sourcePath;
			std::replace(normalized.begin(), normalized.end(), '\\', '/');

			std::string lower = normalized;
			std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char value)
			{
				return static_cast<char>(std::tolower(value));
			});

			constexpr std::string_view AssetPrefix = "assets/";
			constexpr std::string_view AssetSegment = "/assets/";
			if (lower.starts_with(AssetPrefix))
			{
				normalized.erase(0, AssetPrefix.size());
			}
			else if (const std::size_t assetPosition = lower.find(AssetSegment); assetPosition != std::string::npos)
			{
				normalized.erase(0, assetPosition + AssetSegment.size());
			}

			std::filesystem::path logicalPath(normalized);
			logicalPath.replace_extension(".model");
			return Swim::Assets::NormalizeAssetPath(logicalPath.generic_string());
		}

		glm::mat4 ToMatrix(const Swim::Assets::AssetTransform& transform)
		{
			const glm::vec3 translation(transform.Translation[0], transform.Translation[1], transform.Translation[2]);
			const glm::quat rotation(
				transform.Rotation[3],
				transform.Rotation[0],
				transform.Rotation[1],
				transform.Rotation[2]
			);
			const glm::vec3 scale(transform.Scale[0], transform.Scale[1], transform.Scale[2]);
			return glm::translate(glm::mat4(1.0f), translation) * glm::mat4_cast(rotation) * glm::scale(glm::mat4(1.0f), scale);
		}

		glm::mat4 ResolveWorldTransform(
			const Swim::Assets::ModelAsset& model,
			std::uint32_t nodeIndex,
			std::vector<glm::mat4>& worldTransforms,
			std::vector<std::uint8_t>& visitState)
		{
			if (nodeIndex >= model.Nodes.size())
			{
				throw std::runtime_error("Cooked model contains a node index outside the node table.");
			}
			if (visitState[nodeIndex] == 2)
			{
				return worldTransforms[nodeIndex];
			}
			if (visitState[nodeIndex] == 1)
			{
				throw std::runtime_error("Cooked model contains a transform-parent cycle.");
			}

			visitState[nodeIndex] = 1;
			const Swim::Assets::ModelNode& node = model.Nodes[nodeIndex];
			const glm::mat4 local = ToMatrix(node.LocalTransform);
			if (node.Parent == Swim::Assets::ModelNode::InvalidNode)
			{
				worldTransforms[nodeIndex] = local;
			}
			else
			{
				worldTransforms[nodeIndex] = ResolveWorldTransform(model, node.Parent, worldTransforms, visitState) * local;
			}
			visitState[nodeIndex] = 2;
			return worldTransforms[nodeIndex];
		}

		const Swim::Assets::VertexAttributeDesc* FindAttribute(
			const Swim::Assets::MeshAsset& mesh,
			Swim::Assets::VertexSemantic semantic)
		{
			const auto attribute = std::find_if(mesh.VertexAttributes.begin(), mesh.VertexAttributes.end(), [semantic](const auto& value)
			{
				return value.Semantic == semantic;
			});
			return attribute == mesh.VertexAttributes.end() ? nullptr : &*attribute;
		}

		const std::byte* VertexAttributeAddress(
			const Swim::Assets::MeshAsset& mesh,
			const Swim::Assets::VertexAttributeDesc& attribute,
			std::uint64_t vertexIndex,
			std::size_t elementSize)
		{
			if (attribute.StreamIndex >= mesh.VertexStreams.size())
			{
				throw std::runtime_error("Cooked mesh vertex attribute references a missing stream.");
			}
			const Swim::Assets::VertexStreamDesc& stream = mesh.VertexStreams[attribute.StreamIndex];
			if (stream.StrideBytes == 0)
			{
				throw std::runtime_error("Cooked mesh vertex stream has a zero stride.");
			}

			if (vertexIndex > (std::numeric_limits<std::uint64_t>::max() - attribute.OffsetBytes) / stream.StrideBytes)
			{
				throw std::runtime_error("Cooked mesh vertex offset overflows its address range.");
			}
			const std::uint64_t relativeOffset = vertexIndex * stream.StrideBytes + attribute.OffsetBytes;
			if (relativeOffset > stream.DataSizeBytes || elementSize > stream.DataSizeBytes - relativeOffset)
			{
				throw std::runtime_error("Cooked mesh vertex attribute points outside its stream payload.");
			}
			if (relativeOffset > std::numeric_limits<std::uint64_t>::max() - stream.DataOffsetBytes)
			{
				throw std::runtime_error("Cooked mesh stream offset overflows its address range.");
			}
			const std::uint64_t absoluteOffset = stream.DataOffsetBytes + relativeOffset;
			if (absoluteOffset > mesh.VertexBytes.size() || elementSize > mesh.VertexBytes.size() - absoluteOffset)
			{
				throw std::runtime_error("Cooked mesh vertex stream points outside the mesh payload.");
			}
			return mesh.VertexBytes.data() + absoluteOffset;
		}

		glm::vec3 ReadFloat3(
			const Swim::Assets::MeshAsset& mesh,
			const Swim::Assets::VertexAttributeDesc& attribute,
			std::uint64_t vertexIndex)
		{
			if (attribute.Format != Swim::Assets::VertexElementFormat::Float32x3)
			{
				throw std::runtime_error("Legacy renderer residency currently requires Float32x3 position/color streams.");
			}
			std::array<float, 3> value{};
			std::memcpy(value.data(), VertexAttributeAddress(mesh, attribute, vertexIndex, sizeof(value)), sizeof(value));
			return glm::vec3(value[0], value[1], value[2]);
		}

		glm::vec2 ReadFloat2(
			const Swim::Assets::MeshAsset& mesh,
			const Swim::Assets::VertexAttributeDesc& attribute,
			std::uint64_t vertexIndex)
		{
			if (attribute.Format != Swim::Assets::VertexElementFormat::Float32x2)
			{
				throw std::runtime_error("Legacy renderer residency currently requires Float32x2 UV streams.");
			}
			std::array<float, 2> value{};
			std::memcpy(value.data(), VertexAttributeAddress(mesh, attribute, vertexIndex, sizeof(value)), sizeof(value));
			return glm::vec2(value[0], value[1]);
		}

		std::uint32_t ReadIndex(const Swim::Assets::MeshAsset& mesh, std::uint64_t index)
		{
			const std::size_t elementSize = mesh.IndexFormat == Swim::Assets::IndexElementFormat::UInt16
				? sizeof(std::uint16_t)
				: sizeof(std::uint32_t);
			if (index > std::numeric_limits<std::size_t>::max() / elementSize)
			{
				throw std::runtime_error("Cooked mesh index offset exceeds the host address space.");
			}
			const std::size_t byteOffset = static_cast<std::size_t>(index) * elementSize;
			if (byteOffset > mesh.IndexBytes.size() || elementSize > mesh.IndexBytes.size() - byteOffset)
			{
				throw std::runtime_error("Cooked mesh index points outside the index payload.");
			}

			if (mesh.IndexFormat == Swim::Assets::IndexElementFormat::UInt16)
			{
				std::uint16_t value = 0;
				std::memcpy(&value, mesh.IndexBytes.data() + byteOffset, sizeof(value));
				return value;
			}
			std::uint32_t value = 0;
			std::memcpy(&value, mesh.IndexBytes.data() + byteOffset, sizeof(value));
			return value;
		}

		glm::vec3 ResolveBaseColorFactor(const Swim::Assets::MaterialInstanceAsset* material)
		{
			if (!material)
			{
				return glm::vec3(1.0f);
			}
			for (const Swim::Assets::MaterialParameterValue& parameter : material->Parameters)
			{
				if (parameter.Name == "BaseColorFactor")
				{
					return glm::vec3(parameter.Value[0], parameter.Value[1], parameter.Value[2]);
				}
			}
			return glm::vec3(1.0f);
		}
	}

	std::shared_ptr<LegacyRenderBinding> MaterialPool::GetMaterialBindingByID(uint32_t id)
	{
		std::lock_guard<std::mutex> lock(poolMutex);

		for (auto& kv : materials)
		{
			const std::shared_ptr<LegacyRenderBinding>& data = kv.second;
			if (!data)
			{
				continue;
			}

			if (data->meshBufferData->meshID == id)
			{
				return data;
			}
		}

		return nullptr;
	}

	std::string MaterialPool::GetMaterialNameByID(uint32_t id)
	{
		std::lock_guard<std::mutex> lock(poolMutex);

		for (auto& kv : materials)
		{
			const std::string& name = kv.first;
			const std::shared_ptr<LegacyRenderBinding>& data = kv.second;

			if (!data)
			{
				continue;
			}

			if (data->meshBufferData->meshID == id)
			{
				return name;
			}
		}

		return std::string();
	}

	std::shared_ptr<LegacyRenderBinding> MaterialPool::GetMaterialBinding(const std::string& name)
	{
		std::lock_guard<std::mutex> lock(poolMutex);

		auto it = materials.find(name);
		if (it != materials.end())
		{
			return it->second;
		}

		return nullptr;
	}

	bool MaterialPool::MaterialExists(const std::string& name)
	{
		std::lock_guard<std::mutex> lock(poolMutex);
		auto it = materials.find(name);
		if (it != materials.end())
		{
			return true;
		}

		return false;
	}

	std::shared_ptr<LegacyRenderBinding> MaterialPool::RegisterMaterialBinding(
		const std::string& name,
		std::shared_ptr<Mesh> mesh,
		std::shared_ptr<Texture2D> albedoMap,
		Swim::Assets::AssetId materialAssetId,
		Swim::Assets::AssetId meshAssetId)
	{
		std::lock_guard<std::mutex> lock(poolMutex);

		auto it = materials.find(name);
		if (it != materials.end())
		{
			return it->second;
		}

		if (assets && !materialAssetId)
		{
			materialAssetId = assets->GetDatabase().GetOrCreate("Legacy/Materials/" + name);
		}
		if (assets && !meshAssetId)
		{
			meshAssetId = assets->GetDatabase().GetOrCreate("Legacy/Meshes/" + name);
		}

		auto material = std::make_shared<MaterialData>(std::move(albedoMap));
		auto residency = meshes->RequestMeshResidency(mesh);

		auto data = std::make_shared<LegacyRenderBinding>(
			std::move(mesh),
			std::move(residency),
			std::move(material),
			meshAssetId,
			materialAssetId
		);
		materials.emplace(name, data);


		return data;
	}

	std::vector<std::shared_ptr<LegacyRenderBinding>> MaterialPool::GetCompositeMaterialData(const std::string& name)
	{
		std::lock_guard<std::mutex> lock(poolMutex);

		auto it = compositeMaterials.find(name);
		if (it != compositeMaterials.end())
		{
			return it->second;
		}

		throw std::runtime_error("Failed to find composite material data: " + name);
	}

	std::vector<std::shared_ptr<LegacyRenderBinding>> MaterialPool::LazyLoadAndGetCompositeMaterial(const std::string& sourcePath)
	{
		return LoadAndRegisterCompositeMaterial(sourcePath);
	}

	bool MaterialPool::CompositeMaterialExists(const std::string& name)
	{
		std::lock_guard<std::mutex> lock(poolMutex);
		auto it = compositeMaterials.find(name);
		if (it != compositeMaterials.end())
		{
			return true;
		}

		return false;
	}

	Swim::Assets::AssetId MaterialPool::GetCompositeMaterialAssetId(const std::string& sourcePath) const
	{
		std::lock_guard<std::mutex> lock(poolMutex);
		const auto existing = compositeMaterialAssetIds.find(sourcePath);
		return existing != compositeMaterialAssetIds.end() ? existing->second : Swim::Assets::AssetId{};
	}

	std::vector<std::shared_ptr<LegacyRenderBinding>> MaterialPool::LoadAndRegisterCompositeMaterial(const std::string& sourcePath)
	{
		auto getCachedBinding = [&]()
		{
			std::lock_guard<std::mutex> lock(poolMutex);
			const auto existing = compositeMaterials.find(sourcePath);
			return existing != compositeMaterials.end()
				? existing->second
				: std::vector<std::shared_ptr<LegacyRenderBinding>>{};
		};

		try
		{
			if (!assets)
			{
				throw std::runtime_error("MaterialPool has no AssetSystem for cooked model residency.");
			}

			const std::string modelLogicalPath = CookedModelLogicalPath(sourcePath);
			const auto modelHandle = assets->Find<Swim::Assets::ModelAsset>(modelLogicalPath);
			if (!modelHandle)
			{
				throw std::runtime_error(
					"Cooked model was never registered as '" + modelLogicalPath +
					"'. The development asset bootstrap either did not discover the source or reported a cook/load error earlier in the log."
				);
			}

			{
				std::lock_guard<std::mutex> lock(poolMutex);
				compositeMaterialAssetIds[sourcePath] = modelHandle.GetId();
			}

			const Swim::Assets::ModelAsset* model = assets->Resolve(modelHandle);
			if (!model)
			{
				const Swim::Assets::AssetStatus status = assets->GetStatus(modelHandle);
				std::string message =
					"Cooked model '" + modelLogicalPath + "' is not resident (state " + AssetLoadStateName(status.State) + ")";
				if (status.Error.HasError() && !status.Error.Message.empty())
				{
					message += ": " + status.Error.Message;
				}
				throw std::runtime_error(std::move(message));
			}

			const Swim::Assets::ContentHash graphRevision = assets->ComputeDependencyRevisionHash(modelHandle.GetId());
			if (graphRevision.IsZero())
			{
				throw std::runtime_error("Cooked model dependency graph is incomplete: " + modelLogicalPath);
			}
			{
				std::lock_guard<std::mutex> lock(poolMutex);
				const auto existing = compositeMaterials.find(sourcePath);
				const auto existingRevision = compositeMaterialRevisions.find(sourcePath);
				if (existing != compositeMaterials.end() && existingRevision != compositeMaterialRevisions.end() && existingRevision->second == graphRevision)
				{
					return existing->second;
				}
			}

			const std::string revisionName = graphRevision.ToHex();
			std::vector<std::shared_ptr<LegacyRenderBinding>> loadedMaterials;
			std::vector<glm::mat4> worldTransforms(model->Nodes.size(), glm::mat4(1.0f));
			std::vector<std::uint8_t> visitState(model->Nodes.size(), 0);

			for (std::uint32_t nodeIndex = 0; nodeIndex < model->Nodes.size(); ++nodeIndex)
			{
				const Swim::Assets::ModelNode& node = model->Nodes[nodeIndex];
				if (!node.Mesh)
				{
					continue;
				}

				const Swim::Assets::MeshAsset* meshAsset = assets->Resolve(node.Mesh);
				if (!meshAsset)
				{
					throw std::runtime_error("Cooked model references a mesh that is not resident: " + modelLogicalPath);
				}
				const Swim::Assets::VertexAttributeDesc* positionAttribute = FindAttribute(*meshAsset, Swim::Assets::VertexSemantic::Position);
				if (!positionAttribute)
				{
					throw std::runtime_error("Cooked mesh does not contain a position stream: " + modelLogicalPath);
				}
				const Swim::Assets::VertexAttributeDesc* uvAttribute = FindAttribute(*meshAsset, Swim::Assets::VertexSemantic::TexCoord0);
				const Swim::Assets::VertexAttributeDesc* colorAttribute = FindAttribute(*meshAsset, Swim::Assets::VertexSemantic::Color0);
				const glm::mat4 worldTransform = ResolveWorldTransform(*model, nodeIndex, worldTransforms, visitState);

				for (std::size_t primitiveIndex = 0; primitiveIndex < meshAsset->Primitives.size(); ++primitiveIndex)
				{
					const Swim::Assets::MeshPrimitive& primitive = meshAsset->Primitives[primitiveIndex];
					const Swim::Assets::MaterialInstanceAsset* materialAsset = nullptr;
					std::shared_ptr<Texture2D> albedoMap;
					if (primitive.MaterialSlot < node.Materials.size() && node.Materials[primitive.MaterialSlot])
					{
						materialAsset = assets->Resolve(node.Materials[primitive.MaterialSlot]);
						if (!materialAsset)
						{
							throw std::runtime_error("Cooked model references a material that is not resident: " + modelLogicalPath);
						}
						for (const Swim::Assets::MaterialTextureBinding& textureBinding : materialAsset->Textures)
						{
							if (textureBinding.Name == "BaseColorTexture" && textureBinding.Texture)
							{
								const std::string textureName = modelLogicalPath + "@" + revisionName + "#node/" + std::to_string(nodeIndex) + "/primitive/" + std::to_string(primitiveIndex) + "/base-color";
								albedoMap = textures->GetOrCreateTextureFromAsset(*assets, textureBinding.Texture, textureName);
								break;
							}
						}
					}

					const glm::vec3 baseColorFactor = ResolveBaseColorFactor(materialAsset);
					std::vector<Vertex> vertices;
					std::vector<std::uint32_t> indices;
					vertices.reserve(primitive.IndexCount);
					indices.reserve(primitive.IndexCount);
					std::unordered_map<std::uint64_t, std::uint32_t> vertexRemap;
					vertexRemap.reserve(primitive.IndexCount);

					for (std::uint32_t indexOffset = 0; indexOffset < primitive.IndexCount; ++indexOffset)
					{
						const std::uint32_t localSourceIndex = ReadIndex(*meshAsset, static_cast<std::uint64_t>(primitive.FirstIndex) + indexOffset);
						const std::int64_t globalSourceIndex = static_cast<std::int64_t>(primitive.VertexOffset) + localSourceIndex;
						if (globalSourceIndex < 0)
						{
							throw std::runtime_error("Cooked mesh primitive references a negative vertex index.");
						}
						const std::uint64_t sourceVertex = static_cast<std::uint64_t>(globalSourceIndex);

						auto existing = vertexRemap.find(sourceVertex);
						if (existing != vertexRemap.end())
						{
							indices.push_back(existing->second);
							continue;
						}

						Vertex vertex{};
						const glm::vec3 position = ReadFloat3(*meshAsset, *positionAttribute, sourceVertex);
						vertex.position = glm::vec3(worldTransform * glm::vec4(position, 1.0f));
						vertex.uv = uvAttribute ? ReadFloat2(*meshAsset, *uvAttribute, sourceVertex) : glm::vec2(0.0f);
						vertex.color = (colorAttribute ? ReadFloat3(*meshAsset, *colorAttribute, sourceVertex) : glm::vec3(1.0f)) * baseColorFactor;

						const std::uint32_t legacyIndex = static_cast<std::uint32_t>(vertices.size());
						vertices.push_back(vertex);
						vertexRemap.emplace(sourceVertex, legacyIndex);
						indices.push_back(legacyIndex);
					}

					const std::string bindingName = modelLogicalPath + "@" + revisionName + "#node/" + std::to_string(nodeIndex) + "/primitive/" + std::to_string(primitiveIndex);
					const std::shared_ptr<Mesh> mesh = meshes->GetOrCreateAndRegisterMesh(bindingName, vertices, indices);
					Swim::Assets::AssetId materialAssetId{};
					if (primitive.MaterialSlot < node.Materials.size() && node.Materials[primitive.MaterialSlot])
					{
						materialAssetId = node.Materials[primitive.MaterialSlot].GetId();
					}
					loadedMaterials.push_back(RegisterMaterialBinding(
						bindingName + "/material",
						mesh,
						std::move(albedoMap),
						materialAssetId,
						node.Mesh.GetId()
					));
				}
			}

			{
				std::lock_guard<std::mutex> lock(poolMutex);
				const auto existing = compositeMaterials.find(sourcePath);
				const auto existingRevision = compositeMaterialRevisions.find(sourcePath);
				if (existing != compositeMaterials.end() && existingRevision != compositeMaterialRevisions.end() && existingRevision->second == graphRevision)
				{
					return existing->second;
				}
				compositeMaterials[sourcePath] = loadedMaterials;
				compositeMaterialRevisions[sourcePath] = graphRevision;
			}

			return loadedMaterials;
		}
		catch (const std::exception& error)
		{
			const auto cached = getCachedBinding();
			std::cerr << "[MaterialPool] Failed to resolve cooked model for '" << sourcePath
				<< "': " << error.what();
			if (!cached.empty())
			{
				std::cerr << " Using the previous resident compatibility binding.";
			}
			std::cerr << '\n';
			return cached;
		}
	}

	void MaterialPool::Flush()
	{
		std::lock_guard<std::mutex> lock(poolMutex);
		materials.clear();
		compositeMaterials.clear();
		compositeMaterialRevisions.clear();
		compositeMaterialAssetIds.clear();

	}

}
