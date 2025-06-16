#include "PCH.h"
#include "MaterialPool.h"
#include "Engine/Systems/Renderer/Core/Material/MaterialData.h"
#include "Library/tiny_gltf/tiny_gltf.h"
#include "Library/stb/stb_image.h"
#include "Engine/Systems/Renderer/Core/Meshes/MeshPool.h"
#include "Engine/Systems/Renderer/Core/Textures/TexturePool.h"
#include <filesystem>

namespace Engine
{

	MaterialPool& MaterialPool::GetInstance()
	{
		static MaterialPool instance;
		return instance;
	}

	std::shared_ptr<MaterialData> MaterialPool::GetMaterialData(const std::string& name)
	{
		std::lock_guard<std::mutex> lock(poolMutex);

		auto it = materials.find(name);
		if (it != materials.end())
		{
			return it->second;
		}

		return nullptr;
	}

	std::shared_ptr<MaterialData> MaterialPool::RegisterMaterialData(const std::string& name, std::shared_ptr<Mesh> mesh, std::shared_ptr<Texture2D> albedoMap)
	{
		std::lock_guard<std::mutex> lock(poolMutex);

		auto it = materials.find(name);
		if (it != materials.end())
		{
			return it->second;
		}

		auto data = std::make_shared<MaterialData>(mesh, albedoMap);
		materials.emplace(name, data);

		return data;
	}

	std::vector<std::shared_ptr<MaterialData>> MaterialPool::GetCompositeMaterialData(const std::string& name)
	{
		std::lock_guard<std::mutex> lock(poolMutex);

		auto it = compositeMaterials.find(name);
		if (it != compositeMaterials.end())
		{
			return it->second;
		}

		throw std::runtime_error("Failed to find composite material data: " + name);
	}

	std::vector<std::shared_ptr<MaterialData>> MaterialPool::LoadAndRegisterCompositeMaterialFromGLB(const std::string& path)
	{
		std::vector<std::shared_ptr<MaterialData>> loadedMaterials;

		tinygltf::Model model;
		tinygltf::TinyGLTF loader;
		std::string err, warn;

		// Load GLB binary model
		bool loaded = loader.LoadBinaryFromFile(&model, &err, &warn, path);
		if (!warn.empty())
		{
			std::cerr << "GLTF Warning: " << warn << std::endl;
		}
		if (!err.empty())
		{
			std::cerr << "GLTF Error: " << err << std::endl;
		}
		if (!loaded)
		{
			throw std::runtime_error("Failed to load GLB file: " + path);
		}

		// Get access to texture and material pools
		TexturePool& texturePool = TexturePool::GetInstance();
		MeshPool& meshPool = MeshPool::GetInstance();

		// Root directory of textures (if present)
		std::filesystem::path rootPath = std::filesystem::path(path).parent_path();

		// Iterate over all meshes in the model
		for (size_t meshIdx = 0; meshIdx < model.meshes.size(); ++meshIdx)
		{
			const tinygltf::Mesh& gltfMesh = model.meshes[meshIdx];

			// Iterate over primitives (each is a submesh)
			for (size_t primIdx = 0; primIdx < gltfMesh.primitives.size(); ++primIdx)
			{
				const tinygltf::Primitive& primitive = gltfMesh.primitives[primIdx];

				// Only support triangle primitives
				if (primitive.mode != TINYGLTF_MODE_TRIANGLES)
				{
					continue;
				}

				// ------------------- Parse VERTICES -------------------

				std::vector<Vertex> vertices;
				std::vector<uint16_t> indices;

				// POSITION
				const auto& posAccessor = model.accessors[primitive.attributes.at("POSITION")];
				const auto& posBufferView = model.bufferViews[posAccessor.bufferView];
				const auto& posBuffer = model.buffers[posBufferView.buffer];

				const float* positionData = reinterpret_cast<const float*>(posBuffer.data.data() + posBufferView.byteOffset + posAccessor.byteOffset);
				size_t vertexCount = posAccessor.count;

				// UV (optional)
				const float* uvData = nullptr;
				if (primitive.attributes.contains("TEXCOORD_0"))
				{
					const auto& uvAccessor = model.accessors[primitive.attributes.at("TEXCOORD_0")];
					const auto& uvBufferView = model.bufferViews[uvAccessor.bufferView];
					const auto& uvBuffer = model.buffers[uvBufferView.buffer];

					uvData = reinterpret_cast<const float*>(uvBuffer.data.data() + uvBufferView.byteOffset + uvAccessor.byteOffset);
				}

				// COLOR (optional, default white)
				const float* colorData = nullptr;
				if (primitive.attributes.contains("COLOR_0"))
				{
					const auto& colorAccessor = model.accessors[primitive.attributes.at("COLOR_0")];
					const auto& colorBufferView = model.bufferViews[colorAccessor.bufferView];
					const auto& colorBuffer = model.buffers[colorBufferView.buffer];

					colorData = reinterpret_cast<const float*>(colorBuffer.data.data() + colorBufferView.byteOffset + colorAccessor.byteOffset);
				}

				for (size_t i = 0; i < vertexCount; ++i)
				{
					Vertex v;
					v.position = glm::vec3(
						positionData[i * 3 + 0],
						positionData[i * 3 + 1],
						positionData[i * 3 + 2]
					);

					if (uvData)
					{
						v.uv = glm::vec2(uvData[i * 2 + 0], uvData[i * 2 + 1]);
					}
					else
					{
						v.uv = glm::vec2(0.0f);
					}

					if (colorData)
					{
						v.color = glm::vec3(
							colorData[i * 3 + 0],
							colorData[i * 3 + 1],
							colorData[i * 3 + 2]
						);
					}
					else
					{
						v.color = glm::vec3(1.0f);
					}

					vertices.push_back(v);
				}

				// ------------------- Parse INDICES -------------------

				const auto& idxAccessor = model.accessors[primitive.indices];
				const auto& idxBufferView = model.bufferViews[idxAccessor.bufferView];
				const auto& idxBuffer = model.buffers[idxBufferView.buffer];

				const uint8_t* idxData = idxBuffer.data.data() + idxBufferView.byteOffset + idxAccessor.byteOffset;

				for (size_t i = 0; i < idxAccessor.count; ++i)
				{
					uint16_t index = 0;
					switch (idxAccessor.componentType)
					{
						case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
						index = reinterpret_cast<const uint16_t*>(idxData)[i];
						break;
						case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
						index = static_cast<uint16_t>(reinterpret_cast<const uint32_t*>(idxData)[i]);
						break;
						case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
						index = static_cast<uint16_t>(reinterpret_cast<const uint8_t*>(idxData)[i]);
						break;
						default:
						throw std::runtime_error("Unsupported index type in GLB");
					}
					indices.push_back(index);
				}

				// ------------------- Register Mesh -------------------

				std::string meshName = gltfMesh.name.empty() ? "mesh_" + std::to_string(meshIdx) : gltfMesh.name;
				meshName += "_prim" + std::to_string(primIdx);

				std::shared_ptr<Mesh> mesh = meshPool.RegisterMesh(meshName, vertices, indices);

				// ------------------- Load Texture (Albedo) -------------------

				std::shared_ptr<Texture2D> texture = nullptr;

				if (primitive.material >= 0 && primitive.material < model.materials.size())
				{
					const auto& material = model.materials[primitive.material];

					if (material.pbrMetallicRoughness.baseColorTexture.index >= 0)
					{
						int texIndex = material.pbrMetallicRoughness.baseColorTexture.index;
						if (texIndex < model.textures.size())
						{
							const auto& textureDef = model.textures[texIndex];
							const auto& imageDef = model.images[textureDef.source];

							if (!imageDef.uri.empty())
							{
								std::string texPath = (rootPath / imageDef.uri).string();
								texture = texturePool.GetTexture2DLazy(texPath);
							}
							else if (imageDef.bufferView >= 0 && imageDef.bufferView < model.bufferViews.size())
							{
								const auto& imageBufferView = model.bufferViews[imageDef.bufferView];
								const auto& imageBuffer = model.buffers[imageBufferView.buffer];

								// Get pointer to raw image bytes
								const unsigned char* imageData = imageBuffer.data.data() + imageBufferView.byteOffset;
								size_t imageSize = imageBufferView.byteLength;

								int texWidth = 0, texHeight = 0, texChannels = 0;
								stbi_uc* decodedData = stbi_load_from_memory(
									imageData,
									static_cast<int>(imageSize),
									&texWidth,
									&texHeight,
									&texChannels,
									STBI_rgb_alpha // Force RGBA
								);

								if (!decodedData)
								{
									std::cerr << "[MeshPool] Failed to decode embedded texture " << texIndex << std::endl;
								}
								else
								{
									// Upload decoded texture to GPU
									texture = std::make_shared<Texture2D>(texWidth, texHeight, decodedData);
									texture->isPixelDataSTB = true; // because we did actually load it from stb image
									std::string name = path + " " + std::to_string(meshIdx);
									texturePool.StoreTextureManually(texture, name);
								}
							}
							else
							{
								std::cerr << "[MeshPool] Texture " << texIndex << " has no URI and no valid bufferView!" << std::endl;
							}
						}
					}
				}

				// ------------------- Register MaterialData -------------------

				std::string matName = meshName + "_material";
				std::shared_ptr<MaterialData> matData = RegisterMaterialData(matName, mesh, texture);
				loadedMaterials.push_back(matData);
			}
		}

		compositeMaterials.emplace(path, loadedMaterials);

		return loadedMaterials;
	}

	void MaterialPool::Flush()
	{
		std::lock_guard<std::mutex> lock(poolMutex);
		materials.clear();
		compositeMaterials.clear();
	}

}
