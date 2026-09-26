#include "Game/ModelImport.h"

#include "Engine/Assets/AssetSystem.h"
#include "Engine/Assets/MaterialAsset.h"
#include "Engine/Assets/MeshAsset.h"
#include "Engine/Components/MeshRenderer.h"
#include "Engine/Components/Transform.h"
#include "Engine/Systems/Renderer/Residency/AssetResidencyService.h"
#include "Engine/Systems/Renderer/Runtime/FrameRenderer.h"
#include "Engine/Systems/Renderer/Runtime/MaterialLibrary.h"
#include "Engine/Systems/Renderer/Runtime/MeshLibrary.h"
#include "Engine/Systems/Renderer/Runtime/ProceduralMeshes.h"
#include "Engine/Systems/Renderer/Runtime/RenderServices.h"
#include "Engine/Systems/Scene/Scene.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace Game
{
	namespace
	{
		namespace Assets = Swim::Assets;

		// MaterialTemplateAsset::FeatureMask bits written by the static model compiler.
		constexpr std::uint64_t FeatureDoubleSided = 1ull << 1;
		constexpr std::uint64_t FeatureAlphaMask = 1ull << 2;
		constexpr std::uint64_t FeatureAlphaBlend = 1ull << 3;

		std::string Lower(std::string_view text)
		{
			std::string result(text);
			std::transform(result.begin(), result.end(), result.begin(),
				[](unsigned char c)
				{
					return static_cast<char>(std::tolower(c));
				});
			return result;
		}

		bool EndsWith(std::string_view text, std::string_view suffix)
		{
			return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
		}

		glm::mat4 ToMatrix(const Assets::AssetTransform& transform)
		{
			const glm::quat rotation(transform.Rotation[3], transform.Rotation[0], transform.Rotation[1], transform.Rotation[2]);
			return glm::translate(
					   glm::mat4(1.0f), glm::vec3(transform.Translation[0], transform.Translation[1], transform.Translation[2])) *
				glm::mat4_cast(glm::normalize(rotation)) *
				glm::scale(glm::mat4(1.0f), glm::vec3(transform.Scale[0], transform.Scale[1], transform.Scale[2]));
		}

		// Byte offsets of the float attributes this importer reads (stream 0).
		struct VertexLayout
		{
			std::uint32_t Stride = 0;
			std::optional<std::uint32_t> Position;
			std::optional<std::uint32_t> Normal;
			std::optional<std::uint32_t> Tangent;
			std::optional<std::uint32_t> TexCoord;
		};

		std::optional<VertexLayout> ReadLayout(const Assets::MeshAsset& mesh)
		{
			if (mesh.VertexStreams.empty() || mesh.VertexStreams.front().StrideBytes == 0)
			{
				return std::nullopt;
			}
			VertexLayout layout;
			layout.Stride = mesh.VertexStreams.front().StrideBytes;
			for (const auto& attribute : mesh.VertexAttributes)
			{
				if (attribute.StreamIndex != 0)
				{
					continue;
				}
				switch (attribute.Semantic)
				{
				case Assets::VertexSemantic::Position:
					if (attribute.Format == Assets::VertexElementFormat::Float32x3)
					{
						layout.Position = attribute.OffsetBytes;
					}
					break;
				case Assets::VertexSemantic::Normal:
					if (attribute.Format == Assets::VertexElementFormat::Float32x3)
					{
						layout.Normal = attribute.OffsetBytes;
					}
					break;
				case Assets::VertexSemantic::Tangent:
					if (attribute.Format == Assets::VertexElementFormat::Float32x4)
					{
						layout.Tangent = attribute.OffsetBytes;
					}
					break;
				case Assets::VertexSemantic::TexCoord0:
					if (attribute.Format == Assets::VertexElementFormat::Float32x2)
					{
						layout.TexCoord = attribute.OffsetBytes;
					}
					break;
				default:
					break;
				}
			}
			if (!layout.Position)
			{
				return std::nullopt;
			}
			return layout;
		}

		template <std::size_t N>
		std::array<float, N> ReadFloats(const std::byte* vertex, std::optional<std::uint32_t> offset, std::array<float, N> fallback)
		{
			if (!offset)
			{
				return fallback;
			}
			std::array<float, N> value{};
			std::memcpy(value.data(), vertex + *offset, sizeof(float) * N);
			return value;
		}

		std::uint32_t ReadIndex(const Assets::MeshAsset& mesh, std::size_t index)
		{
			if (mesh.IndexFormat == Assets::IndexElementFormat::UInt16)
			{
				std::uint16_t value = 0;
				std::memcpy(&value, mesh.IndexBytes.data() + index * sizeof(std::uint16_t), sizeof(value));
				return value;
			}
			std::uint32_t value = 0;
			std::memcpy(&value, mesh.IndexBytes.data() + index * sizeof(std::uint32_t), sizeof(value));
			return value;
		}

		const Assets::MaterialParameterValue* FindParameter(const Assets::MaterialInstanceAsset& material, std::string_view name)
		{
			for (const auto& parameter : material.Parameters)
			{
				if (parameter.Name == name)
				{
					return &parameter;
				}
			}
			return nullptr;
		}

		Assets::AssetHandle<Assets::TextureAsset> FindTexture(const Assets::MaterialInstanceAsset& material, std::string_view name)
		{
			for (const auto& binding : material.Textures)
			{
				if (binding.Name == name)
				{
					return binding.Texture;
				}
			}
			return {};
		}

		// One material's worth of geometry, in model space (node transforms applied).
		struct Group
		{
			Assets::AssetHandle<Assets::MaterialInstanceAsset> Material;
			Engine::ProceduralMeshes::MeshData Mesh;
			bool HasTangents = true;
		};
	} // namespace

	Assets::AssetHandle<Assets::ModelAsset> FindCookedModel(const Assets::AssetSystem& assets, const std::vector<std::string>& keywords,
		const std::vector<std::string>& prefer, const std::vector<std::string>& avoid)
	{
		Assets::AssetHandle<Assets::ModelAsset> best;
		int bestScore = std::numeric_limits<int>::min();
		std::string bestPath;
		for (const auto& entry : assets.GetDatabase().Snapshot())
		{
			const std::string path = Lower(entry.LogicalPath);
			if (!EndsWith(path, ".model"))
			{
				continue;
			}
			bool matches = true;
			for (const auto& keyword : keywords)
			{
				matches = matches && path.find(Lower(keyword)) != std::string::npos;
			}
			if (!matches)
			{
				continue;
			}
			const auto handle = assets.Find<Assets::ModelAsset>(entry.LogicalPath);
			if (!handle.IsValid() || !assets.Resolve(handle))
			{
				continue;
			}
			int score = 0;
			for (std::size_t i = 0; i < prefer.size(); ++i)
			{
				score += path.find(Lower(prefer[i])) != std::string::npos ? static_cast<int>(100 * (prefer.size() - i)) : 0;
			}
			for (const auto& keyword : avoid)
			{
				score -= path.find(Lower(keyword)) != std::string::npos ? 1000 : 0;
			}
			// Deterministic among equals: the shortest, then alphabetically first path.
			if (score > bestScore ||
				(score == bestScore && (path.size() < bestPath.size() || (path.size() == bestPath.size() && path < bestPath))))
			{
				best = handle;
				bestScore = score;
				bestPath = path;
			}
		}
		return best;
	}

	ImportedModel SpawnCookedModel(Engine::Scene& scene, Engine::RenderServices& render, Assets::AssetSystem& assets,
		Assets::AssetHandle<Assets::ModelAsset> modelHandle, std::string_view name, const ModelPlacement& placement)
	{
		ImportedModel result;
		if (!render.HasRenderer())
		{
			return result;
		}
		const Assets::ModelAsset* model = modelHandle.IsValid() ? assets.Resolve(modelHandle) : nullptr;
		if (!model)
		{
			return result;
		}

		// Node world matrices (parents may follow their children in the node array).
		std::vector<std::optional<glm::mat4>> world(model->Nodes.size());
		const auto worldOf = [&](std::uint32_t node, auto&& self) -> glm::mat4
		{
			if (world[node])
			{
				return *world[node];
			}
			glm::mat4 matrix = ToMatrix(model->Nodes[node].LocalTransform);
			const std::uint32_t parent = model->Nodes[node].Parent;
			if (parent != Assets::ModelNode::InvalidNode && parent < model->Nodes.size() && parent != node)
			{
				matrix = self(parent, self) * matrix;
			}
			world[node] = matrix;
			return matrix;
		};

		// Regroup every primitive by material.
		std::map<std::uint64_t, Group> groups; // By material asset id (0: no material).
		glm::vec3 boundsMin(std::numeric_limits<float>::max());
		glm::vec3 boundsMax(std::numeric_limits<float>::lowest());
		for (std::uint32_t nodeIndex = 0; nodeIndex < model->Nodes.size(); ++nodeIndex)
		{
			const auto& node = model->Nodes[nodeIndex];
			const Assets::MeshAsset* mesh = node.Mesh.IsValid() ? assets.Resolve(node.Mesh) : nullptr;
			if (!mesh)
			{
				continue;
			}
			const auto layout = ReadLayout(*mesh);
			if (!layout || mesh->VertexBytes.empty() || mesh->IndexBytes.empty())
			{
				continue;
			}
			const glm::mat4 nodeWorld = worldOf(nodeIndex, worldOf);
			const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(nodeWorld)));
			const bool mirrored = glm::determinant(glm::mat3(nodeWorld)) < 0.0f;
			const auto& stream = mesh->VertexStreams.front();
			const std::size_t vertexCount = static_cast<std::size_t>(stream.DataSizeBytes / layout->Stride);
			const std::size_t indexSize = mesh->IndexFormat == Assets::IndexElementFormat::UInt16 ? 2 : 4;
			const std::size_t indexCount = mesh->IndexBytes.size() / indexSize;

			// The finest LOD's primitives (all of them when the mesh has no LOD table).
			std::uint32_t first = 0;
			std::uint32_t count = static_cast<std::uint32_t>(mesh->Primitives.size());
			if (!mesh->Lods.empty())
			{
				first = std::min<std::uint32_t>(mesh->Lods.front().FirstPrimitive, count);
				count = std::min<std::uint32_t>(mesh->Lods.front().PrimitiveCount, count - first);
			}
			for (std::uint32_t p = first; p < first + count; ++p)
			{
				const auto& primitive = mesh->Primitives[p];
				if (primitive.IndexCount < 3 || static_cast<std::size_t>(primitive.FirstIndex) + primitive.IndexCount > indexCount)
				{
					continue;
				}
				Assets::AssetHandle<Assets::MaterialInstanceAsset> material;
				if (primitive.MaterialSlot < node.Materials.size())
				{
					material = node.Materials[primitive.MaterialSlot];
				}
				const std::uint64_t key = material.IsValid() ? material.GetId().Value : 0;
				auto& group = groups[key];
				group.Material = material;
				auto& out = group.Mesh;

				std::unordered_map<std::uint32_t, std::uint32_t> remap;
				remap.reserve(primitive.IndexCount);
				std::vector<std::uint32_t> triangle;
				triangle.reserve(3);
				for (std::uint32_t i = 0; i < primitive.IndexCount; ++i)
				{
					const std::int64_t source =
						static_cast<std::int64_t>(ReadIndex(*mesh, primitive.FirstIndex + i)) + primitive.VertexOffset;
					if (source < 0 || static_cast<std::size_t>(source) >= vertexCount)
					{
						triangle.clear();
						i += 2 - (i % 3); // Skip the rest of this triangle.
						continue;
					}
					const auto vertexIndex = static_cast<std::uint32_t>(source);
					auto found = remap.find(vertexIndex);
					if (found == remap.end())
					{
						const std::byte* vertex =
							mesh->VertexBytes.data() + stream.DataOffsetBytes + std::size_t(vertexIndex) * layout->Stride;
						const auto position = ReadFloats<3>(vertex, layout->Position, { 0, 0, 0 });
						const auto normal = ReadFloats<3>(vertex, layout->Normal, { 0, 1, 0 });
						const auto tangent = ReadFloats<4>(vertex, layout->Tangent, { 0, 0, 0, 1 });
						const auto uv = ReadFloats<2>(vertex, layout->TexCoord, { 0, 0 });

						const glm::vec3 p3 = glm::vec3(nodeWorld * glm::vec4(position[0], position[1], position[2], 1.0f));
						glm::vec3 n3 = normalMatrix * glm::vec3(normal[0], normal[1], normal[2]);
						n3 = glm::dot(n3, n3) > 1e-20f ? glm::normalize(n3) : glm::vec3(0, 1, 0);
						glm::vec3 t3 = glm::mat3(nodeWorld) * glm::vec3(tangent[0], tangent[1], tangent[2]);
						if (glm::dot(t3, t3) > 1e-20f)
						{
							t3 = glm::normalize(t3);
						}
						else
						{
							group.HasTangents = false;
						}
						boundsMin = glm::min(boundsMin, p3);
						boundsMax = glm::max(boundsMax, p3);

						Swim::Render::StandardVertex v;
						v.Position = { p3.x, p3.y, p3.z };
						v.Normal = { n3.x, n3.y, n3.z };
						v.Tangent = { t3.x, t3.y, t3.z, (tangent[3] < 0.0f) != mirrored ? -1.0f : 1.0f };
						v.TexCoord0 = uv;
						out.Vertices.push_back(v);
						found = remap.emplace(vertexIndex, static_cast<std::uint32_t>(out.Vertices.size() - 1)).first;
					}
					triangle.push_back(found->second);
					if (triangle.size() == 3)
					{
						if (mirrored)
						{
							std::swap(triangle[1], triangle[2]);
						}
						out.Indices.insert(out.Indices.end(), triangle.begin(), triangle.end());
						triangle.clear();
					}
				}
			}
		}
		if (groups.empty() || boundsMin.x > boundsMax.x)
		{
			return result;
		}

		// Placement: bottom center on Position, optional uniform scale to a target length.
		const glm::vec3 extent = boundsMax - boundsMin;
		const float longest = std::max(extent.x, extent.z);
		const float scale = placement.TargetLength > 0.0f && longest > 1e-6f ? placement.TargetLength / longest : 1.0f;
		const glm::vec3 pivot((boundsMin.x + boundsMax.x) * 0.5f, boundsMin.y, (boundsMin.z + boundsMax.z) * 0.5f);
		const glm::quat rotation = glm::angleAxis(glm::radians(placement.YawDegrees), glm::vec3(0, 1, 0));
		const glm::mat4 placeMatrix = glm::translate(glm::mat4(1.0f), placement.Position) * glm::mat4_cast(rotation) *
			glm::scale(glm::mat4(1.0f), glm::vec3(scale)) * glm::translate(glm::mat4(1.0f), -pivot);
		result.BoundsMin = glm::vec3(std::numeric_limits<float>::max());
		result.BoundsMax = glm::vec3(std::numeric_limits<float>::lowest());
		for (int corner = 0; corner < 8; ++corner)
		{
			const glm::vec3 local((corner & 1) ? boundsMax.x : boundsMin.x, (corner & 2) ? boundsMax.y : boundsMin.y,
				(corner & 4) ? boundsMax.z : boundsMin.z);
			const glm::vec3 placed = glm::vec3(placeMatrix * glm::vec4(local, 1.0f));
			result.BoundsMin = glm::min(result.BoundsMin, placed);
			result.BoundsMax = glm::max(result.BoundsMax, placed);
		}

		auto& meshes = *render.Meshes;
		auto& materials = *render.Materials;
		auto& residency = render.Renderer->GetResidency();
		std::unordered_set<std::uint64_t> requestedTextures;
		std::uint32_t groupIndex = 0;
		for (auto& [key, group] : groups)
		{
			const std::string groupName = std::string(name) + "/" + std::to_string(groupIndex++);
			if (group.Mesh.Indices.size() < 3)
			{
				continue;
			}
			result.Triangles += static_cast<std::uint32_t>(group.Mesh.Indices.size() / 3);

			// Material.
			Engine::MaterialDesc desc;
			desc.Name = groupName;
			desc.Roughness = 1.0f;
			desc.Metallic = 1.0f;
			if (const auto* material = group.Material.IsValid() ? assets.Resolve(group.Material) : nullptr)
			{
				if (const auto* p = FindParameter(*material, "BaseColorFactor"))
				{
					desc.BaseColor = p->Value;
				}
				if (const auto* p = FindParameter(*material, "EmissiveFactor"))
				{
					desc.Emissive = { p->Value[0], p->Value[1], p->Value[2] };
				}
				if (const auto* p = FindParameter(*material, "MetallicFactor"))
				{
					desc.Metallic = p->Value[0];
				}
				if (const auto* p = FindParameter(*material, "RoughnessFactor"))
				{
					desc.Roughness = p->Value[0];
				}
				if (const auto* p = FindParameter(*material, "AlphaCutoff"))
				{
					desc.AlphaCutoff = p->Value[0];
				}
				if (const auto* materialTemplate = material->Template.IsValid() ? assets.Resolve(material->Template) : nullptr)
				{
					desc.DoubleSided = (materialTemplate->FeatureMask & FeatureDoubleSided) != 0;
					desc.Blend = (materialTemplate->FeatureMask & FeatureAlphaBlend) != 0 ? Engine::MaterialBlend::Transparent
						: (materialTemplate->FeatureMask & FeatureAlphaMask) != 0		  ? Engine::MaterialBlend::Masked
																						  : Engine::MaterialBlend::Opaque;
				}
				desc.BaseColorTexture = FindTexture(*material, "BaseColorTexture");
				desc.MetallicRoughnessTexture = FindTexture(*material, "MetallicRoughnessTexture");
				desc.NormalTexture = FindTexture(*material, "NormalTexture");
				desc.OcclusionTexture = FindTexture(*material, "OcclusionTexture");
				desc.EmissiveTexture = FindTexture(*material, "EmissiveTexture");
				for (const auto& texture : { desc.BaseColorTexture, desc.MetallicRoughnessTexture, desc.NormalTexture,
						 desc.OcclusionTexture, desc.EmissiveTexture })
				{
					if (texture.IsValid() && requestedTextures.insert(texture.GetId().Value).second)
					{
						residency.RequestTexture(texture);
					}
				}
				++result.Materials;
			}
			if (desc.Blend == Engine::MaterialBlend::Masked)
			{
				desc.DoubleSided = true; // Foliage and fabric cards are seen from both sides.
			}
			const std::uint32_t materialSet = materials.GetOrCreate(desc);

			// Mesh (registered once; a scene reload reuses it).
			auto meshHandle = meshes.Find(groupName);
			if (!meshHandle.IsValid())
			{
				if (!group.HasTangents)
				{
					Engine::ProceduralMeshes::GenerateTangents(group.Mesh);
				}
				meshHandle = meshes.Register(groupName, group.Mesh);
			}

			result.Parts.push_back({ groupName, meshHandle, materialSet });
		}
		result.Textures = static_cast<std::uint32_t>(requestedTextures.size());
		result.Position = glm::vec3(placeMatrix[3]);
		result.Rotation = rotation;
		result.Scale = scale;
		result.Entities = RespawnModel(scene, result, placement.Tags);
		return result;
	}

	std::vector<entt::entity> RespawnModel(Engine::Scene& scene, const ImportedModel& model, const std::vector<Engine::TagId>& tags)
	{
		std::vector<entt::entity> entities;
		entities.reserve(model.Parts.size());
		for (const auto& part : model.Parts)
		{
			const entt::entity entity = scene.CreateEntity(part.Name);
			scene.AddComponent<Engine::Transform>(entity, Engine::Transform(model.Position, glm::vec3(model.Scale), model.Rotation));
			Engine::MeshRenderer renderer;
			renderer.Parts.push_back({ part.Mesh, part.MaterialSet });
			renderer.Flags = Swim::Render::RenderObjectFlags::Default | Swim::Render::RenderObjectFlags::Static;
			scene.AddComponent<Engine::MeshRenderer>(entity, std::move(renderer));
			for (const auto tag : tags)
			{
				scene.AddTag(entity, tag);
			}
			entities.push_back(entity);
		}
		return entities;
	}
} // namespace Game
