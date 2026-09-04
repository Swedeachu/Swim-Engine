#include "Tools/AssetCompiler/StaticModelCompiler.h"

#include "Engine/Assets/AssetDatabase.h"
#include "Engine/Assets/AssetSystem.h"
#include "Engine/Assets/MaterialAsset.h"
#include "Engine/Assets/MeshAsset.h"
#include "Engine/Assets/ModelAsset.h"
#include "Engine/Assets/TextureAsset.h"
#include "Tools/AssetCompiler/Ktx2TextureCompiler.h"
#include "Tools/AssetCompiler/SassetWriter.h"
#include "Tools/AssetCompiler/SourceImageTextureCompiler.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

namespace Swim::AssetCompiler
{
	namespace
	{
		constexpr std::uint64_t MaterialFeatureUnlit = 1ull << 0;
		constexpr std::uint64_t MaterialFeatureDoubleSided = 1ull << 1;
		constexpr std::uint64_t MaterialFeatureAlphaMask = 1ull << 2;
		constexpr std::uint64_t MaterialFeatureAlphaBlend = 1ull << 3;

		struct PackedStaticVertex
		{
			std::array<float, 3> Position{};
			std::array<float, 3> Normal{};
			std::array<float, 4> Tangent{ 0.0f, 0.0f, 0.0f, 1.0f };
			std::array<float, 2> TexCoord0{};
		};

		struct TextureKey
		{
			std::uint32_t TextureIndex = 0;
			Swim::Assets::TextureColorSpace ColorSpace = Swim::Assets::TextureColorSpace::Linear;
			Swim::Assets::TextureSemantic Semantic = Swim::Assets::TextureSemantic::Color;

			bool operator==(const TextureKey&) const = default;
		};

		struct TextureKeyHash
		{
			std::size_t operator()(const TextureKey& key) const noexcept
			{
				return
					(static_cast<std::size_t>(key.TextureIndex) * 1315423911u) ^
					(static_cast<std::size_t>(key.ColorSpace) << 8) ^
					static_cast<std::size_t>(key.Semantic);
			}
		};

		StaticModelCompileResult MakeError(StaticModelCompileErrorCode code, std::string message)
		{
			StaticModelCompileResult result;
			result.Error.Code = code;
			result.Error.Message = std::move(message);
			return result;
		}

		std::string MakeRootLogicalPath(std::string_view sourceLogicalPath)
		{
			const std::string normalized = Swim::Assets::NormalizeAssetPath(sourceLogicalPath);
			std::filesystem::path path(normalized);
			path.replace_extension(".model");
			return Swim::Assets::NormalizeAssetPath(path.generic_string());
		}

		std::string ChildPath(std::string_view root, std::string_view kind, std::size_t index)
		{
			return std::string(root) + "#" + std::string(kind) + "/" + std::to_string(index);
		}

		void AppendBytes(std::vector<std::byte>& destination, const void* data, std::size_t size)
		{
			const std::byte* begin = static_cast<const std::byte*>(data);
			destination.insert(destination.end(), begin, begin + size);
		}

		void ExpandBounds(Swim::Assets::AssetBounds& destination, const Swim::Assets::AssetBounds& source)
		{
			for (std::size_t axis = 0; axis < 3; ++axis)
			{
				destination.Min[axis] = std::min(destination.Min[axis], source.Min[axis]);
				destination.Max[axis] = std::max(destination.Max[axis], source.Max[axis]);
			}
		}

		Swim::Assets::SamplerFilter ConvertMinFilter(SourceFilter filter)
		{
			switch (filter)
			{
			case SourceFilter::Nearest:
			case SourceFilter::NearestMipmapNearest:
			case SourceFilter::NearestMipmapLinear:
				return Swim::Assets::SamplerFilter::Nearest;
			default:
				return Swim::Assets::SamplerFilter::Linear;
			}
		}

		Swim::Assets::SamplerFilter ConvertMagFilter(SourceFilter filter)
		{
			return filter == SourceFilter::Nearest
				? Swim::Assets::SamplerFilter::Nearest
				: Swim::Assets::SamplerFilter::Linear;
		}

		Swim::Assets::SamplerFilter ConvertMipFilter(SourceFilter filter)
		{
			switch (filter)
			{
			case SourceFilter::NearestMipmapNearest:
			case SourceFilter::LinearMipmapNearest:
				return Swim::Assets::SamplerFilter::Nearest;
			default:
				return Swim::Assets::SamplerFilter::Linear;
			}
		}

		Swim::Assets::SamplerAddressMode ConvertWrap(SourceWrap wrap)
		{
			switch (wrap)
			{
			case SourceWrap::MirroredRepeat:
				return Swim::Assets::SamplerAddressMode::MirroredRepeat;
			case SourceWrap::ClampToEdge:
				return Swim::Assets::SamplerAddressMode::ClampToEdge;
			default:
				return Swim::Assets::SamplerAddressMode::Repeat;
			}
		}

		Swim::Assets::MaterialTemplateAsset BuildMaterialTemplate(const SourceMaterial& source)
		{
			Swim::Assets::MaterialTemplateAsset asset;
			asset.ShaderFamily = source.Unlit ? "PBR/Unlit" : "PBR/MetallicRoughness";
			if (source.Unlit)
			{
				asset.FeatureMask |= MaterialFeatureUnlit;
			}
			if (source.DoubleSided)
			{
				asset.FeatureMask |= MaterialFeatureDoubleSided;
			}
			if (source.AlphaMode == SourceAlphaMode::Mask)
			{
				asset.FeatureMask |= MaterialFeatureAlphaMask;
			}
			else if (source.AlphaMode == SourceAlphaMode::Blend)
			{
				asset.FeatureMask |= MaterialFeatureAlphaBlend;
			}

			asset.Parameters =
			{
				{ "BaseColorFactor", Swim::Assets::MaterialParameterType::Float4, { 1.0f, 1.0f, 1.0f, 1.0f } },
				{ "EmissiveFactor", Swim::Assets::MaterialParameterType::Float3, {} },
				{ "MetallicFactor", Swim::Assets::MaterialParameterType::Float, { 1.0f, 0.0f, 0.0f, 0.0f } },
				{ "RoughnessFactor", Swim::Assets::MaterialParameterType::Float, { 1.0f, 0.0f, 0.0f, 0.0f } },
				{ "AlphaCutoff", Swim::Assets::MaterialParameterType::Float, { 0.5f, 0.0f, 0.0f, 0.0f } }
			};
			return asset;
		}

		Swim::Assets::MaterialInstanceAsset BuildMaterialInstance(
			const SourceMaterial& source,
			Swim::Assets::AssetHandle<Swim::Assets::MaterialTemplateAsset> materialTemplate)
		{
			Swim::Assets::MaterialInstanceAsset asset;
			asset.Template = materialTemplate;
			asset.Parameters =
			{
				{ "BaseColorFactor", source.BaseColorFactor },
				{ "EmissiveFactor", { source.EmissiveFactor[0], source.EmissiveFactor[1], source.EmissiveFactor[2], 0.0f } },
				{ "MetallicFactor", { source.MetallicFactor, 0.0f, 0.0f, 0.0f } },
				{ "RoughnessFactor", { source.RoughnessFactor, 0.0f, 0.0f, 0.0f } },
				{ "AlphaCutoff", { source.AlphaCutoff, 0.0f, 0.0f, 0.0f } }
			};
			return asset;
		}

		std::vector<Swim::Assets::AssetId> UniqueDependencies(std::vector<Swim::Assets::AssetId> dependencies)
		{
			std::sort(dependencies.begin(), dependencies.end(), [](auto left, auto right)
			{
				return left.Value < right.Value;
			});
			dependencies.erase(std::unique(dependencies.begin(), dependencies.end()), dependencies.end());
			return dependencies;
		}
	}

	Swim::Assets::ContentHash GetStaticModelCompilerProfileHash()
	{
		return Swim::Assets::ComputeContentHash(
			"SwimStaticModelCompiler:v1;sasset=1;fastgltf=0.9.0;meshoptimizer=1.1;"
			"mesh=float32-interleaved-v1;texture=ktx2-or-rgba8-mips-v3");
	}

	StaticModelCompileResult StaticModelCompiler::Compile(
		const IntermediateModel& model,
		std::string_view sourceLogicalPath,
		std::vector<Swim::Assets::SassetSourceDependency> sourceDependencies) const
	{
		StaticModelCompileResult result;
		try
		{
			result.RootLogicalPath = MakeRootLogicalPath(sourceLogicalPath);
			const Swim::Assets::ContentHash compilerHash = GetStaticModelCompilerProfileHash();
			const Swim::Assets::ContentHash sourceHash = ComputeSourceGraphHash(sourceDependencies);

			Swim::Assets::AssetSystem ids;
			if (!ids.Initialize())
			{
				return MakeError(StaticModelCompileErrorCode::InvalidSourceData, "could not initialize compiler-side asset identity context");
			}

			auto emit = [&](Swim::Assets::SassetAssetType type, Swim::Assets::AssetId id, std::string logicalPath,
				std::vector<Swim::Assets::AssetId> dependencies, std::vector<std::byte> payload, bool isRoot)
			{
				SassetBuildInput input;
				input.Type = type;
				input.Id = id;
				input.LogicalPath = std::move(logicalPath);
				input.CompilerProfileHash = compilerHash;
				input.SourceHash = sourceHash;
				input.Dependencies = UniqueDependencies(std::move(dependencies));
				input.SourceDependencies = sourceDependencies;
				input.Payload = std::move(payload);
				SassetBuildResult built = BuildSasset(input);
				if (!built)
				{
					throw std::runtime_error(built.Error.Message);
				}
				result.Assets.push_back(CompiledSasset{ id, type, std::move(input.LogicalPath), std::move(built.Bytes), isRoot });
			};

			std::vector<Swim::Assets::AssetHandle<Swim::Assets::SamplerAsset>> samplerHandles(model.Samplers.size());
			for (std::size_t index = 0; index < model.Samplers.size(); ++index)
			{
				const std::string path = ChildPath(result.RootLogicalPath, "sampler", index);
				const auto handle = ids.Declare<Swim::Assets::SamplerAsset>(path);
				samplerHandles[index] = handle;
				const SourceSampler& source = model.Samplers[index];
				Swim::Assets::SamplerAsset asset;
				asset.MinFilter = ConvertMinFilter(source.MinFilter);
				asset.MagFilter = ConvertMagFilter(source.MagFilter);
				asset.MipFilter = ConvertMipFilter(source.MinFilter);
				asset.AddressU = ConvertWrap(source.WrapU);
				asset.AddressV = ConvertWrap(source.WrapV);
				emit(Swim::Assets::SassetAssetType::Sampler, handle.GetId(), path, {}, SerializeAssetPayload(asset), false);
				++result.Stats.Samplers;
			}

			const std::string defaultSamplerPath = std::string(result.RootLogicalPath) + "#sampler/default";
			const auto defaultSampler = ids.Declare<Swim::Assets::SamplerAsset>(defaultSamplerPath);
			Swim::Assets::SamplerAsset defaultSamplerAsset;
			emit(Swim::Assets::SassetAssetType::Sampler, defaultSampler.GetId(), defaultSamplerPath, {}, SerializeAssetPayload(defaultSamplerAsset), false);
			++result.Stats.Samplers;

			std::unordered_map<TextureKey, Swim::Assets::AssetHandle<Swim::Assets::TextureAsset>, TextureKeyHash> textureHandles;
			auto resolveTexture = [&](std::uint32_t textureIndex, Swim::Assets::TextureColorSpace colorSpace,
				Swim::Assets::TextureSemantic semantic) -> Swim::Assets::AssetHandle<Swim::Assets::TextureAsset>
			{
				const TextureKey key{ textureIndex, colorSpace, semantic };
				if (const auto existing = textureHandles.find(key); existing != textureHandles.end())
				{
					return existing->second;
				}
				if (textureIndex >= model.Textures.size())
				{
					throw std::runtime_error("material references a texture outside the glTF texture table");
				}
				const SourceTexture& texture = model.Textures[textureIndex];
				if (!texture.ImageIndex.has_value() || *texture.ImageIndex >= model.Images.size())
				{
					throw std::runtime_error("glTF texture has no valid source image");
				}
				const SourceImage& image = model.Images[*texture.ImageIndex];
				if (image.EncodedBytes.empty())
				{
					throw std::runtime_error("source texture has no encoded image bytes");
				}
				const SourceImageMimeType mimeType = DetectSourceImageMimeType(image.EncodedBytes, image.MimeType);
				Swim::Assets::TextureAsset textureAsset;
				if (mimeType == SourceImageMimeType::Ktx2)
				{
					const Ktx2TextureCompileResult compiled = CompileKtx2Texture(image.EncodedBytes, colorSpace, semantic);
					if (!compiled)
					{
						throw std::runtime_error("KTX2 texture compile failed: " + compiled.Error.Message);
					}
					textureAsset = compiled.Asset;
				}
				else
				{
					const SourceImageTextureCompileResult compiled = CompileSourceImageTexture(
						image.EncodedBytes, mimeType, colorSpace, semantic);
					if (!compiled)
					{
						const std::string prefix = compiled.Error.Code == SourceImageTextureCompileErrorCode::UnsupportedSource
							? "unsupported source texture: "
							: "source image texture compile failed: ";
						throw std::runtime_error(prefix + compiled.Error.Message);
					}
					textureAsset = compiled.Asset;
				}

				const std::string path = ChildPath(result.RootLogicalPath, "texture", textureHandles.size());
				const auto handle = ids.Declare<Swim::Assets::TextureAsset>(path);
				emit(Swim::Assets::SassetAssetType::Texture, handle.GetId(), path, {}, SerializeAssetPayload(textureAsset), false);
				textureHandles.emplace(key, handle);
				++result.Stats.Textures;
				return handle;
			};

			std::vector<Swim::Assets::AssetHandle<Swim::Assets::MaterialInstanceAsset>> materialHandles(model.Materials.size());
			for (std::size_t index = 0; index < model.Materials.size(); ++index)
			{
				const SourceMaterial& source = model.Materials[index];
				const std::string templatePath = ChildPath(result.RootLogicalPath, "material-template", index);
				const auto templateHandle = ids.Declare<Swim::Assets::MaterialTemplateAsset>(templatePath);
				const auto templateAsset = BuildMaterialTemplate(source);
				emit(Swim::Assets::SassetAssetType::MaterialTemplate, templateHandle.GetId(), templatePath, {}, SerializeAssetPayload(templateAsset), false);

				const std::string materialPath = ChildPath(result.RootLogicalPath, "material", index);
				const auto materialHandle = ids.Declare<Swim::Assets::MaterialInstanceAsset>(materialPath);
				materialHandles[index] = materialHandle;
				auto materialAsset = BuildMaterialInstance(source, templateHandle);
				std::vector<Swim::Assets::AssetId> dependencies{ templateHandle.GetId() };

				auto bindTexture = [&](const char* name, const std::optional<std::uint32_t>& textureIndex,
					Swim::Assets::TextureColorSpace colorSpace, Swim::Assets::TextureSemantic semantic)
				{
					if (!textureIndex.has_value())
					{
						return;
					}
					const auto textureHandle = resolveTexture(*textureIndex, colorSpace, semantic);
					const SourceTexture& sourceTexture = model.Textures[*textureIndex];
					auto samplerHandle = defaultSampler;
					if (sourceTexture.SamplerIndex.has_value())
					{
						if (*sourceTexture.SamplerIndex >= samplerHandles.size())
						{
							throw std::runtime_error("glTF texture references a sampler outside the sampler table");
						}
						samplerHandle = samplerHandles[*sourceTexture.SamplerIndex];
					}
					materialAsset.Textures.push_back({ name, textureHandle, samplerHandle });
					dependencies.push_back(textureHandle.GetId());
					dependencies.push_back(samplerHandle.GetId());
				};

				bindTexture("BaseColorTexture", source.BaseColorTexture, Swim::Assets::TextureColorSpace::SRgb, Swim::Assets::TextureSemantic::Color);
				bindTexture("MetallicRoughnessTexture", source.MetallicRoughnessTexture, Swim::Assets::TextureColorSpace::Linear, Swim::Assets::TextureSemantic::Data);
				bindTexture("NormalTexture", source.NormalTexture, Swim::Assets::TextureColorSpace::Linear, Swim::Assets::TextureSemantic::Normal);
				bindTexture("OcclusionTexture", source.OcclusionTexture, Swim::Assets::TextureColorSpace::Linear, Swim::Assets::TextureSemantic::Data);
				bindTexture("EmissiveTexture", source.EmissiveTexture, Swim::Assets::TextureColorSpace::SRgb, Swim::Assets::TextureSemantic::Color);

				emit(Swim::Assets::SassetAssetType::MaterialInstance, materialHandle.GetId(), materialPath,
					std::move(dependencies), SerializeAssetPayload(materialAsset), false);
				++result.Stats.Materials;
			}

			std::vector<Swim::Assets::AssetHandle<Swim::Assets::MeshAsset>> meshHandles(model.Meshes.size());
			std::vector<std::vector<std::optional<std::uint32_t>>> meshMaterialSlots(model.Meshes.size());
			for (std::size_t meshIndex = 0; meshIndex < model.Meshes.size(); ++meshIndex)
			{
				const SourceMesh& sourceMesh = model.Meshes[meshIndex];
				Swim::Assets::MeshAsset meshAsset;
				meshAsset.IndexFormat = Swim::Assets::IndexElementFormat::UInt32;
				meshAsset.VertexStreams.push_back({ static_cast<std::uint32_t>(sizeof(PackedStaticVertex)), 0, 0 });
				meshAsset.VertexAttributes =
				{
					{ Swim::Assets::VertexSemantic::Position, Swim::Assets::VertexElementFormat::Float32x3, 0, static_cast<std::uint32_t>(offsetof(PackedStaticVertex, Position)) },
					{ Swim::Assets::VertexSemantic::Normal, Swim::Assets::VertexElementFormat::Float32x3, 0, static_cast<std::uint32_t>(offsetof(PackedStaticVertex, Normal)) },
					{ Swim::Assets::VertexSemantic::Tangent, Swim::Assets::VertexElementFormat::Float32x4, 0, static_cast<std::uint32_t>(offsetof(PackedStaticVertex, Tangent)) },
					{ Swim::Assets::VertexSemantic::TexCoord0, Swim::Assets::VertexElementFormat::Float32x2, 0, static_cast<std::uint32_t>(offsetof(PackedStaticVertex, TexCoord0)) }
				};

				std::vector<std::optional<std::uint32_t>>& slots = meshMaterialSlots[meshIndex];
				for (const SourcePrimitive& primitive : sourceMesh.Primitives)
				{
					if (primitive.Topology != SourcePrimitiveTopology::Triangles)
					{
						return MakeError(StaticModelCompileErrorCode::UnsupportedTopology, "static .sasset mesh v1 currently supports triangle primitives only");
					}
					if (primitive.Vertices.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
					{
						return MakeError(StaticModelCompileErrorCode::Overflow, "mesh vertex count exceeds .sasset v1 vertex-offset range");
					}
					const std::size_t baseVertex = meshAsset.VertexBytes.size() / sizeof(PackedStaticVertex);
					const std::size_t firstIndex = meshAsset.IndexBytes.size() / sizeof(std::uint32_t);
					if (baseVertex > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()) ||
						firstIndex > std::numeric_limits<std::uint32_t>::max() || primitive.Indices.size() > std::numeric_limits<std::uint32_t>::max())
					{
						return MakeError(StaticModelCompileErrorCode::Overflow, "mesh payload exceeds .sasset v1 index/vertex limits");
					}

					for (const SourceVertex& sourceVertex : primitive.Vertices)
					{
						PackedStaticVertex vertex;
						vertex.Position = sourceVertex.Position;
						if (sourceVertex.HasNormal)
						{
							vertex.Normal = sourceVertex.Normal;
						}
						if (sourceVertex.HasTangent)
						{
							vertex.Tangent = sourceVertex.Tangent;
						}
						if (sourceVertex.HasTexCoord0)
						{
							vertex.TexCoord0 = sourceVertex.TexCoord0;
						}
						AppendBytes(meshAsset.VertexBytes, &vertex, sizeof(vertex));
					}
					AppendBytes(meshAsset.IndexBytes, primitive.Indices.data(), primitive.Indices.size() * sizeof(std::uint32_t));

					auto slotIt = std::find(slots.begin(), slots.end(), primitive.MaterialIndex);
					if (slotIt == slots.end())
					{
						slots.push_back(primitive.MaterialIndex);
						slotIt = std::prev(slots.end());
					}
					const std::uint32_t slot = static_cast<std::uint32_t>(std::distance(slots.begin(), slotIt));
					meshAsset.Primitives.push_back({
						static_cast<std::uint32_t>(firstIndex),
						static_cast<std::uint32_t>(primitive.Indices.size()),
						static_cast<std::int32_t>(baseVertex),
						slot,
						primitive.Bounds
					});
					ExpandBounds(meshAsset.Bounds, primitive.Bounds);
				}
				meshAsset.VertexStreams.front().DataSizeBytes = meshAsset.VertexBytes.size();
				meshAsset.Lods.push_back({ 0, static_cast<std::uint32_t>(meshAsset.Primitives.size()), 1.0f });

				const std::string path = ChildPath(result.RootLogicalPath, "mesh", meshIndex);
				const auto handle = ids.Declare<Swim::Assets::MeshAsset>(path);
				meshHandles[meshIndex] = handle;
				emit(Swim::Assets::SassetAssetType::Mesh, handle.GetId(), path, {}, SerializeAssetPayload(meshAsset), false);
				++result.Stats.Meshes;
			}

			Swim::Assets::ModelAsset modelAsset;
			modelAsset.Roots = model.Roots;
			modelAsset.Nodes.resize(model.Nodes.size());
			std::vector<Swim::Assets::AssetId> modelDependencies;
			for (std::size_t nodeIndex = 0; nodeIndex < model.Nodes.size(); ++nodeIndex)
			{
				const SourceNode& sourceNode = model.Nodes[nodeIndex];
				Swim::Assets::ModelNode& node = modelAsset.Nodes[nodeIndex];
				node.Name = sourceNode.Name;
				node.Parent = sourceNode.Parent;
				node.LocalTransform = sourceNode.LocalTransform;
				if (!sourceNode.MeshIndex.has_value())
				{
					continue;
				}
				if (*sourceNode.MeshIndex >= meshHandles.size())
				{
					return MakeError(StaticModelCompileErrorCode::InvalidSourceData, "model node references a mesh outside the mesh table");
				}
				node.Mesh = meshHandles[*sourceNode.MeshIndex];
				modelDependencies.push_back(node.Mesh.GetId());
				for (const std::optional<std::uint32_t> sourceMaterialIndex : meshMaterialSlots[*sourceNode.MeshIndex])
				{
					if (!sourceMaterialIndex.has_value())
					{
						node.Materials.push_back({});
						continue;
					}
					if (*sourceMaterialIndex >= materialHandles.size())
					{
						return MakeError(StaticModelCompileErrorCode::InvalidSourceData, "mesh primitive references a material outside the material table");
					}
					node.Materials.push_back(materialHandles[*sourceMaterialIndex]);
					modelDependencies.push_back(materialHandles[*sourceMaterialIndex].GetId());
				}
			}

			const auto rootHandle = ids.Declare<Swim::Assets::ModelAsset>(result.RootLogicalPath);
			result.RootId = rootHandle.GetId();
			emit(Swim::Assets::SassetAssetType::Model, rootHandle.GetId(), result.RootLogicalPath,
				std::move(modelDependencies), SerializeAssetPayload(modelAsset), true);
			ids.Shutdown();
		}
		catch (const std::overflow_error& error)
		{
			return MakeError(StaticModelCompileErrorCode::Overflow, error.what());
		}
		catch (const std::exception& error)
		{
			const std::string message = error.what();
			if (message.find("unsupported source texture:") != std::string::npos)
			{
				return MakeError(StaticModelCompileErrorCode::UnsupportedTextureSource, message);
			}
			if (message.find("KTX2 texture compile failed:") != std::string::npos)
			{
				return MakeError(StaticModelCompileErrorCode::Ktx2CompileFailed, message);
			}
			if (message.find("source image texture compile failed:") != std::string::npos)
			{
				return MakeError(StaticModelCompileErrorCode::InvalidSourceData, message);
			}
			return MakeError(StaticModelCompileErrorCode::SassetBuildFailed, message);
		}
		return result;
	}

}
