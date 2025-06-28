#include "PCH.h"
#include "MaterialPool.h"
#include "Engine/Systems/Renderer/Core/Material/MaterialData.h"
#include "Library/stb/stb_image.h"
#include "Engine/Systems/Renderer/Core/Meshes/MeshPool.h"
#include "Engine/Systems/Renderer/Core/Textures/TexturePool.h"
#include <filesystem>

#define BASISU_FORCE_DEVEL_MESSAGES 0
#define BASISD_SUPPORT_KTX2 1
#define BASISD_SUPPORT_BASIS 1
#define BASISD_SUPPORT_ZSTD 1
#define BASISD_SUPPORT_ETC1S 1
#define BASISD_SUPPORT_UASTC 1

#include "Library/basis/basisu_transcoder.h"

#include "Library/webp/include/webp/decode.h"

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

	bool MaterialPool::MaterialExists(const std::string& name)
	{
		auto it = materials.find(name);
		if (it != materials.end())
		{
			return true;
		}

		return false;
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

	// Will return the composite material data instantly if it already has been loaded.
	// This is honestly the best safe way to load our 3D models without having to think much.
	std::vector<std::shared_ptr<MaterialData>> MaterialPool::LazyLoadAndGetCompositeMaterial(const std::string& path)
	{
		return CompositeMaterialExists(path) ? GetCompositeMaterialData(path) : LoadAndRegisterCompositeMaterialFromGLB(path);
	}

	bool MaterialPool::CompositeMaterialExists(const std::string& name)
	{
		auto it = compositeMaterials.find(name);
		if (it != compositeMaterials.end())
		{
			return true;
		}

		return false;
	}

	static bool LoadKTX2Image(tinygltf::Image* image, const unsigned char* bytes, int size, std::string* err, int image_idx)
	{
		// Initialize the transcoder once globally
		static bool transcoderInitialized = false;
		if (!transcoderInitialized)
		{
			basist::basisu_transcoder_init();
			transcoderInitialized = true;
		}

		basist::ktx2_transcoder ktx2;

		// Parse the KTX2 header
		if (!ktx2.init(bytes, size))
		{
			if (err)
			{
				*err += "[KTX2 Loader] Failed to parse KTX2 header for image index " + std::to_string(image_idx) + "\n";
			}
			return false;
		}

		// Begin transcoding
		if (!ktx2.start_transcoding())
		{
			if (err)
			{
				*err += "[KTX2 Loader] start_transcoding() failed for image index " + std::to_string(image_idx) + "\n";
			}
			return false;
		}

		// Use RGBA32 uncompressed format
		basist::transcoder_texture_format format = basist::transcoder_texture_format::cTFRGBA32;

		uint32_t mipLevel = 0;
		uint32_t layerIndex = 0;
		uint32_t faceIndex = 0;

		// I guess we don't worry about mip map levels?
		uint32_t width = ktx2.get_width();
		uint32_t height = ktx2.get_height();

		uint32_t bytesPerPixel = basist::basis_get_uncompressed_bytes_per_pixel(format);
		uint32_t totalPixels = width * height;
		uint32_t outputSize = totalPixels * bytesPerPixel;

		std::vector<uint8_t> decodedData(outputSize);

		// Transcode the image level (base mip, layer 0, face 0)
		if (!ktx2.transcode_image_level(
			mipLevel,
			layerIndex,
			faceIndex,
			decodedData.data(),
			totalPixels,
			format))
		{
			if (err)
			{
				*err += "[KTX2 Loader] Failed to transcode image for index " + std::to_string(image_idx) + "\n";
			}
			return false;
		}

		// Fill out tinygltf::Image
		image->width = static_cast<int>(width);
		image->height = static_cast<int>(height);
		image->component = 4;
		image->bits = 8;
		image->pixel_type = TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE;
		image->image = std::move(decodedData);

		return true;
	}

	void MaterialPool::LoadNodeRecursive(
		const tinygltf::Model& model,
		int nodeIndex,
		const glm::mat4& parentTransform,
		const std::string& path,
		std::vector<std::shared_ptr<MaterialData>>& loadedMaterials)
	{
		const tinygltf::Node& node = model.nodes[nodeIndex];
		std::string nodeName = node.name.empty() ? "Node_" + std::to_string(nodeIndex) : node.name;

		glm::mat4 localTransform(1.0f);
		if (!node.matrix.empty())
		{
			for (int col = 0; col < 4; ++col)
			{
				for (int row = 0; row < 4; ++row)
				{
					localTransform[col][row] = static_cast<float>(node.matrix[col * 4 + row]);
				}
			}
		}
		else
		{
			glm::vec3 translation = node.translation.empty() ? glm::vec3(0.0f) : glm::vec3(
				static_cast<float>(node.translation[0]),
				static_cast<float>(node.translation[1]),
				static_cast<float>(node.translation[2]));

			glm::quat rotation = node.rotation.empty() ? glm::quat(1.0f, 0.0f, 0.0f, 0.0f) : glm::quat(
				static_cast<float>(node.rotation[3]),
				static_cast<float>(node.rotation[0]),
				static_cast<float>(node.rotation[1]),
				static_cast<float>(node.rotation[2]));

			glm::vec3 scale = node.scale.empty() ? glm::vec3(1.0f) : glm::vec3(
				static_cast<float>(node.scale[0]),
				static_cast<float>(node.scale[1]),
				static_cast<float>(node.scale[2]));

			localTransform = glm::translate(glm::mat4(1.0f), translation)
				* glm::mat4_cast(rotation)
				* glm::scale(glm::mat4(1.0f), scale);
		}

		glm::mat4 worldTransform = parentTransform * localTransform;

		if (node.mesh >= 0)
		{
			const tinygltf::Mesh& gltfMesh = model.meshes[node.mesh];
			std::string meshName = gltfMesh.name.empty() ? "UnnamedMesh" : gltfMesh.name;

			for (size_t primIdx = 0; primIdx < gltfMesh.primitives.size(); ++primIdx)
			{
				const tinygltf::Primitive& primitive = gltfMesh.primitives[primIdx];

				if (!primitive.attributes.contains("POSITION"))
				{
					continue;
				}

				const tinygltf::Accessor& posAccessor = model.accessors[primitive.attributes.at("POSITION")];

				if (posAccessor.count == 0)
				{
					continue;
				}

				if (posAccessor.bufferView < 0)
				{
					continue;
				}

				const tinygltf::BufferView& posView = model.bufferViews[posAccessor.bufferView];
				const tinygltf::Buffer& posBuffer = model.buffers[posView.buffer];
				const unsigned char* posBase = posBuffer.data.data() + posView.byteOffset + posAccessor.byteOffset;
				const size_t posStride = posView.byteStride > 0 ? posView.byteStride : sizeof(float) * 3;

				bool hasUV = primitive.attributes.contains("TEXCOORD_0");
				bool hasColor = primitive.attributes.contains("COLOR_0");

				const unsigned char* uvRawBase = nullptr;
				size_t uvStride = 0;
				const tinygltf::Accessor* uvAccessor = nullptr;

				if (hasUV)
				{
					uvAccessor = &model.accessors[primitive.attributes.at("TEXCOORD_0")];
					if (uvAccessor->count > 0 && uvAccessor->bufferView >= 0)
					{
						const auto& uvView = model.bufferViews[uvAccessor->bufferView];
						const auto& uvBuffer = model.buffers[uvView.buffer];
						uvRawBase = uvBuffer.data.data() + uvView.byteOffset + uvAccessor->byteOffset;
						uvStride = uvView.byteStride > 0 ? uvView.byteStride : sizeof(float) * 2;
					}
					else
					{
						hasUV = false;
					}
				}

				const unsigned char* colorRawBase = nullptr;
				size_t colorStride = 0;
				const tinygltf::Accessor* colorAccessor = nullptr;

				if (hasColor)
				{
					colorAccessor = &model.accessors[primitive.attributes.at("COLOR_0")];
					if (colorAccessor->count > 0 && colorAccessor->bufferView >= 0)
					{
						const auto& colorView = model.bufferViews[colorAccessor->bufferView];
						const auto& colorBuffer = model.buffers[colorView.buffer];
						colorRawBase = colorBuffer.data.data() + colorView.byteOffset + colorAccessor->byteOffset;
						colorStride = colorView.byteStride > 0 ? colorView.byteStride : sizeof(float) * 3;
					}
					else
					{
						hasColor = false;
					}
				}

				std::vector<Vertex> vertices;
				vertices.reserve(posAccessor.count);

				for (size_t i = 0; i < posAccessor.count; ++i)
				{
					Vertex v;

					const float* posPtr = reinterpret_cast<const float*>(posBase + i * posStride);
					glm::vec4 rawPos = glm::vec4(posPtr[0], posPtr[1], posPtr[2], 1.0f);
					v.position = glm::vec3(worldTransform * rawPos);

					if (hasUV)
					{
						if (uvAccessor->componentType == TINYGLTF_COMPONENT_TYPE_FLOAT)
						{
							const float* uvPtr = reinterpret_cast<const float*>(uvRawBase + i * uvStride);
							v.uv = glm::vec2(uvPtr[0], uvPtr[1]);
						}
						else if (uvAccessor->componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE)
						{
							const uint8_t* uvPtr = reinterpret_cast<const uint8_t*>(uvRawBase + i * uvStride);
							v.uv = glm::vec2(uvPtr[0], uvPtr[1]) / 255.0f;
						}
						else if (uvAccessor->componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
						{
							const uint16_t* uvPtr = reinterpret_cast<const uint16_t*>(uvRawBase + i * uvStride);
							v.uv = glm::vec2(uvPtr[0], uvPtr[1]) / 65535.0f;
						}
						else
						{
							v.uv = glm::vec2(0.0f);
						}
					}
					else
					{
						v.uv = glm::vec2(0.0f);
					}

					if (hasColor)
					{
						if (colorAccessor->componentType == TINYGLTF_COMPONENT_TYPE_FLOAT)
						{
							const float* colorPtr = reinterpret_cast<const float*>(colorRawBase + i * colorStride);
							v.color = glm::vec3(colorPtr[0], colorPtr[1], colorPtr[2]);
						}
						else if (colorAccessor->componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE)
						{
							const uint8_t* colorPtr = reinterpret_cast<const uint8_t*>(colorRawBase + i * colorStride);
							v.color = glm::vec3(colorPtr[0], colorPtr[1], colorPtr[2]) / 255.0f;
						}
						else if (colorAccessor->componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
						{
							const uint16_t* colorPtr = reinterpret_cast<const uint16_t*>(colorRawBase + i * colorStride);
							v.color = glm::vec3(colorPtr[0], colorPtr[1], colorPtr[2]) / 65535.0f;
						}
						else
						{
							v.color = glm::vec3(1.0f);
						}
					}
					else
					{
						v.color = glm::vec3(1.0f);
					}

					vertices.push_back(v);
				}

				std::vector<uint32_t> indices;
				bool hasIndices = (primitive.indices >= 0);

				if (hasIndices)
				{
					const tinygltf::Accessor& idxAccessor = model.accessors[primitive.indices];
					const tinygltf::BufferView& idxView = model.bufferViews[idxAccessor.bufferView];
					const tinygltf::Buffer& idxBuffer = model.buffers[idxView.buffer];
					const unsigned char* idxBase = idxBuffer.data.data() + idxView.byteOffset + idxAccessor.byteOffset;

					indices.reserve(idxAccessor.count);

					for (size_t i = 0; i < idxAccessor.count; ++i)
					{
						uint32_t index = 0;

						switch (idxAccessor.componentType)
						{
							case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
							{
								index = reinterpret_cast<const uint8_t*>(idxBase)[i];
							}
							break;
							case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
							{
								index = reinterpret_cast<const uint16_t*>(idxBase)[i];
							}
							break;
							case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
							{
								index = reinterpret_cast<const uint32_t*>(idxBase)[i];
							}
							break;
							default:
							{
								std::cout << "[ERROR] Unsupported index component type: " << idxAccessor.componentType << std::endl;
								throw std::runtime_error("Unsupported index type in GLB");
							}
						}

						indices.push_back(index);
					}
				}
				else
				{
					indices.reserve(vertices.size());
					for (uint32_t i = 0; i < vertices.size(); ++i)
					{
						indices.push_back(i);
					}
				}

				std::string finalMeshName = gltfMesh.name.empty() ? "mesh_" + std::to_string(nodeIndex) : gltfMesh.name;
				finalMeshName += "_prim" + std::to_string(primIdx);

				std::shared_ptr<Texture2D> texture = nullptr;
				if (primitive.material >= 0 && primitive.material < model.materials.size())
				{
					const tinygltf::Material& material = model.materials[primitive.material];
					const tinygltf::TextureInfo& baseColorTex = material.pbrMetallicRoughness.baseColorTexture;

					if (baseColorTex.index >= 0 && baseColorTex.index < model.textures.size())
					{
						const tinygltf::Texture& textureDef = model.textures[baseColorTex.index];
						int imageSource = -1;

						// First check if this is a regular png with no extensions (cheap to do and often is the case, so we check this first)
						if (textureDef.extensions.empty() && textureDef.source >= 0 && textureDef.source < model.images.size())
						{
							imageSource = textureDef.source;
						}
						else if (textureDef.extensions.contains("KHR_texture_basisu"))
						{
							const tinygltf::Value& ext = textureDef.extensions.at("KHR_texture_basisu");
							if (ext.Has("source"))
							{
								imageSource = ext.Get("source").Get<int>();
							}
						}
						else if (textureDef.extensions.contains("EXT_texture_webp")) 
						{
							const auto& ext = textureDef.extensions.at("EXT_texture_webp");
							if (ext.Has("source")) { imageSource = ext.Get("source").Get<int>(); }
						}

						if (imageSource >= 0 && imageSource < model.images.size())
						{
							const tinygltf::Image& img = model.images[imageSource];
							TexturePool& texturePool = TexturePool::GetInstance();
							texture = texturePool.CreateTextureFromTinyGltfImage(img, path + "_" + std::to_string(nodeIndex));
						}
					}
				}

				MeshPool& meshPool = MeshPool::GetInstance();
				std::shared_ptr<Mesh> mesh = meshPool.RegisterMesh(finalMeshName, vertices, indices);

				std::string matName = finalMeshName + "_material";
				std::shared_ptr<MaterialData> matData = RegisterMaterialData(matName, mesh, texture);
				loadedMaterials.push_back(matData);
			}
		}

		for (size_t i = 0; i < node.children.size(); ++i)
		{
			int childIndex = node.children[i];
			LoadNodeRecursive(model, childIndex, worldTransform, path, loadedMaterials);
		}
	}

	std::vector<std::shared_ptr<MaterialData>> MaterialPool::LoadAndRegisterCompositeMaterialFromGLB(const std::string& path)
	{
		struct ImageCollector
		{
			std::unordered_map<int, tinygltf::Image> images;
		};

		std::vector<std::shared_ptr<MaterialData>> loadedMaterials;
		tinygltf::Model model;
		tinygltf::TinyGLTF loader;
		std::string err, warn;
		ImageCollector imageCollector;

		std::cout << "[DEBUG] Loading GLB file: " << path << std::endl;

		// Image loader with KTX2 support
		tinygltf::LoadImageDataFunction CustomImageLoader = [](
			tinygltf::Image* image,
			const int image_idx,
			std::string* err,
			std::string* warn,
			int req_width,
			int req_height,
			const unsigned char* bytes,
			int size,
			void* user_data) -> bool
		{
			ImageCollector* collector = reinterpret_cast<ImageCollector*>(user_data);

			if (image->mimeType == "image/ktx2" || (size >= 12 && std::memcmp(bytes, "\xABKTX 20\xBB\r\n\x1A\n", 12) == 0))
			{
				if (!LoadKTX2Image(image, bytes, size, err, image_idx))
				{
					if (err != nullptr)
					{
						*err += "[GLTF Loader] Failed to load KTX2 image at index " + std::to_string(image_idx) + "\n";
					}
					return false;
				}
			}
			else if (image->mimeType == "image/webp" || (size >= 12 && std::memcmp(bytes, "RIFF", 4) == 0 && std::memcmp(bytes + 8, "WEBP", 4) == 0))
			{
				int width = 0;
				int height = 0;

				if (!WebPGetInfo(bytes, size, &width, &height))
				{
					if (err != nullptr)
					{
						*err += "[GLTF Loader] WebPGetInfo failed (corrupt?) at index " + std::to_string(image_idx) + '\n';
					}
					return false;
				}

				// Allocate once, decode directly into final buffer
				std::vector<uint8_t> rgba(width * height * 4);

				if (!WebPDecodeRGBAInto(bytes, size, rgba.data(), static_cast<int>(rgba.size()), width * 4))
				{
					if (err != nullptr)
					{
						*err += "[GLTF Loader] WebPDecodeRGBAInto failed at index " + std::to_string(image_idx) + '\n';
					}
					return false;
				}

				// Fill tinygltf::Image with raw pixels
				image->width = width;
				image->height = height;
				image->component = 4;   // RGBA
				image->bits = 8;   // 8-bit per channel
				image->pixel_type = TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE;
				image->image = std::move(rgba);
			}
			else
			{
				int width = 0;
				int height = 0;
				int channels = 0;
				stbi_uc* decoded = stbi_load_from_memory(bytes, size, &width, &height, &channels, STBI_rgb_alpha);
				if (!decoded)
				{
					if (err != nullptr)
					{
						*err += "[GLTF Loader] stb_image failed: " + std::string(stbi_failure_reason()) + "\n";
					}
					return false;
				}

				image->width = width;
				image->height = height;
				image->component = 4;
				image->bits = 8;
				image->pixel_type = TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE;
				image->image.resize(width * height * 4);
				std::memcpy(image->image.data(), decoded, image->image.size());
				stbi_image_free(decoded);
			}

			collector->images[image_idx] = *image;
			return true;
		};

		loader.SetImageLoader(CustomImageLoader, &imageCollector);

		// Load GLB binary
		if (!loader.LoadBinaryFromFile(&model, &err, &warn, path))
		{
			std::cerr << "GLTF Error: " << err << std::endl;
			throw std::runtime_error("Failed to load GLB file: " + path);
		}
		if (!warn.empty())
		{
			std::cerr << "GLTF Warning: " << warn << std::endl;
		}

		// Inject custom-loaded images into the GLTF model
		for (std::pair<const int, tinygltf::Image>& entry : imageCollector.images)
		{
			int index = entry.first;
			const tinygltf::Image& img = entry.second;
			if (index >= model.images.size())
			{
				model.images.resize(index + 1);
			}
			model.images[index] = img;
		}

		// Print model stats
		std::cout << "[DEBUG] Model loaded successfully:" << std::endl;
		std::cout << "  - Nodes: " << model.nodes.size() << std::endl;
		std::cout << "  - Meshes: " << model.meshes.size() << std::endl;
		std::cout << "  - Materials: " << model.materials.size() << std::endl;
		std::cout << "  - Textures: " << model.textures.size() << std::endl;
		std::cout << "  - Images: " << model.images.size() << std::endl;
		std::cout << "  - Scenes: " << model.scenes.size() << std::endl;

		// Recursive scene walker
		if (model.scenes.empty())
		{
			throw std::runtime_error("GLTF file contains no scenes.");
		}

		int sceneIndex = model.defaultScene >= 0 ? model.defaultScene : 0;
		const tinygltf::Scene& scene = model.scenes[sceneIndex];

		std::cout << "[DEBUG] Processing scene " << sceneIndex << " with " << scene.nodes.size() << " root nodes" << std::endl;

		std::unordered_set<int> visitedNodes;

		auto TrackedLoad = [&](int rootIndex)
		{
			LoadNodeRecursive(model, rootIndex, glm::mat4(1.0f), path, loadedMaterials);
			visitedNodes.insert(rootIndex);
		};

		// Replace LoadNodeRecursive call with TrackedLoad
		for (int rootNode : scene.nodes)
		{
			TrackedLoad(rootNode);
		}

		// Afterwards, detect orphan nodes
		for (int i = 0; i < model.nodes.size(); ++i)
		{
			if (visitedNodes.find(i) == visitedNodes.end())
			{
				std::cout << "[WARNING] Node " << i << " (" << model.nodes[i].name << ") was not visited!" << std::endl;
			}
		}

		for (size_t i = 0; i < scene.nodes.size(); ++i)
		{
			const int rootNodeIndex = scene.nodes[i];
			LoadNodeRecursive(model, rootNodeIndex, glm::mat4(1.0f), path, loadedMaterials);
		}

		for (int i = 0; i < model.nodes.size(); ++i)
		{
			if (!visitedNodes.contains(i))
			{
				std::cout << "[DEBUG] Forcing load of orphaned node: " << i << std::endl;
				LoadNodeRecursive(model, i, glm::mat4(1.0f), path, loadedMaterials);
			}
		}

		std::cout << "[DEBUG] Total materials loaded: " << loadedMaterials.size() << std::endl;

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
