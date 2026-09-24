#include "Tools/AssetCompiler/StaticModelCompiler.h"

#include "Engine/Assets/AnimationClipAsset.h"
#include "Engine/Assets/AssetDatabase.h"
#include "Engine/Assets/AssetSystem.h"
#include "Engine/Assets/MaterialAsset.h"
#include "Engine/Assets/MeshAsset.h"
#include "Engine/Assets/ModelAsset.h"
#include "Engine/Assets/SkeletonAsset.h"
#include "Engine/Assets/TextureAsset.h"
#include "Tools/AssetCompiler/Ktx2TextureCompiler.h"
#include "Tools/AssetCompiler/SassetWriter.h"
#include "Tools/AssetCompiler/SourceImageTextureCompiler.h"

#include <algorithm>
#include <array>
#include <cmath>
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

		// Second vertex stream of skinned meshes (MeshAsset: Joints0 UInt16x4 @0,
		// Weights0 Float32x4 @8).
		struct PackedSkinVertex
		{
			std::array<std::uint16_t, 4> Joints{};
			std::array<float, 4> Weights{ 1.0f, 0.0f, 0.0f, 0.0f };
		};

		static_assert(sizeof(PackedSkinVertex) == 24);

		using Matrix4 = std::array<float, 16>; // Column-major.

		constexpr Matrix4 IdentityMatrix{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };

		Matrix4 Multiply(const Matrix4& a, const Matrix4& b)
		{
			Matrix4 result{};
			for (int column = 0; column < 4; ++column)
			{
				for (int row = 0; row < 4; ++row)
				{
					float sum = 0.0f;
					for (int k = 0; k < 4; ++k)
					{
						sum += a[k * 4 + row] * b[column * 4 + k];
					}
					result[column * 4 + row] = sum;
				}
			}
			return result;
		}

		Matrix4 ToMatrix(const Swim::Assets::AssetTransform& transform)
		{
			const auto& q = transform.Rotation;
			const float x = q[0], y = q[1], z = q[2], w = q[3];
			const auto& s = transform.Scale;
			Matrix4 m = IdentityMatrix;
			m[0] = (1 - 2 * (y * y + z * z)) * s[0];
			m[1] = (2 * (x * y + z * w)) * s[0];
			m[2] = (2 * (x * z - y * w)) * s[0];
			m[4] = (2 * (x * y - z * w)) * s[1];
			m[5] = (1 - 2 * (x * x + z * z)) * s[1];
			m[6] = (2 * (y * z + x * w)) * s[1];
			m[8] = (2 * (x * z + y * w)) * s[2];
			m[9] = (2 * (y * z - x * w)) * s[2];
			m[10] = (1 - 2 * (x * x + y * y)) * s[2];
			m[12] = transform.Translation[0];
			m[13] = transform.Translation[1];
			m[14] = transform.Translation[2];
			return m;
		}

		// Affine inverse (no projective part).
		Matrix4 InverseAffine(const Matrix4& m)
		{
			const float a = m[0], b = m[4], c = m[8], d = m[1], e = m[5], f = m[9], g = m[2], h = m[6], i = m[10];
			const float det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
			if (std::abs(det) < 1e-20f)
			{
				throw std::runtime_error("skeleton contains a singular node transform");
			}
			const float inv = 1.0f / det;
			Matrix4 r = IdentityMatrix;
			r[0] = (e * i - f * h) * inv;
			r[4] = (c * h - b * i) * inv;
			r[8] = (b * f - c * e) * inv;
			r[1] = (f * g - d * i) * inv;
			r[5] = (a * i - c * g) * inv;
			r[9] = (c * d - a * f) * inv;
			r[2] = (d * h - e * g) * inv;
			r[6] = (b * g - a * h) * inv;
			r[10] = (a * e - b * d) * inv;
			for (int row = 0; row < 3; ++row)
			{
				r[12 + row] = -(r[row] * m[12] + r[4 + row] * m[13] + r[8 + row] * m[14]);
			}
			return r;
		}

		// TRS of an affine matrix without shear (scale sign folded into x when mirrored).
		Swim::Assets::AssetTransform Decompose(const Matrix4& m)
		{
			Swim::Assets::AssetTransform t{};
			t.Translation = { m[12], m[13], m[14] };
			std::array<float, 3> scale{};
			for (int column = 0; column < 3; ++column)
			{
				scale[column] = std::sqrt(
					m[column * 4] * m[column * 4] + m[column * 4 + 1] * m[column * 4 + 1] + m[column * 4 + 2] * m[column * 4 + 2]);
				if (scale[column] < 1e-20f)
				{
					throw std::runtime_error("skeleton contains a degenerate node scale");
				}
			}
			const float det =
				m[0] * (m[5] * m[10] - m[9] * m[6]) - m[4] * (m[1] * m[10] - m[9] * m[2]) + m[8] * (m[1] * m[6] - m[5] * m[2]);
			if (det < 0.0f)
			{
				scale[0] = -scale[0];
			}
			t.Scale = scale;
			const float r00 = m[0] / scale[0], r10 = m[1] / scale[0], r20 = m[2] / scale[0];
			const float r01 = m[4] / scale[1], r11 = m[5] / scale[1], r21 = m[6] / scale[1];
			const float r02 = m[8] / scale[2], r12 = m[9] / scale[2], r22 = m[10] / scale[2];
			const float trace = r00 + r11 + r22;
			float x, y, z, w;
			if (trace > 0.0f)
			{
				const float s = std::sqrt(trace + 1.0f) * 2.0f;
				w = 0.25f * s;
				x = (r21 - r12) / s;
				y = (r02 - r20) / s;
				z = (r10 - r01) / s;
			}
			else if (r00 > r11 && r00 > r22)
			{
				const float s = std::sqrt(1.0f + r00 - r11 - r22) * 2.0f;
				w = (r21 - r12) / s;
				x = 0.25f * s;
				y = (r01 + r10) / s;
				z = (r02 + r20) / s;
			}
			else if (r11 > r22)
			{
				const float s = std::sqrt(1.0f + r11 - r00 - r22) * 2.0f;
				w = (r02 - r20) / s;
				x = (r01 + r10) / s;
				y = 0.25f * s;
				z = (r12 + r21) / s;
			}
			else
			{
				const float s = std::sqrt(1.0f + r22 - r00 - r11) * 2.0f;
				w = (r10 - r01) / s;
				x = (r02 + r20) / s;
				y = (r12 + r21) / s;
				z = 0.25f * s;
			}
			const float length = std::sqrt(x * x + y * y + z * z + w * w);
			t.Rotation = { x / length, y / length, z / length, w / length };
			return t;
		}

		// Model-wide unique names: runtime clips bind joints and morph targets by name.
		std::vector<std::string> UniqueNodeNames(const IntermediateModel& model)
		{
			std::unordered_map<std::string, std::size_t> counts;
			for (const SourceNode& node : model.Nodes)
			{
				++counts[node.Name];
			}
			std::vector<std::string> names(model.Nodes.size());
			for (std::size_t index = 0; index < model.Nodes.size(); ++index)
			{
				const std::string& name = model.Nodes[index].Name;
				names[index] = !name.empty() && counts[name] == 1 ? name : (name.empty() ? "Node" : name) + "#" + std::to_string(index);
			}
			return names;
		}

		// Normalized, sorted by decreasing weight; an all-zero set binds skin joint 0.
		PackedSkinVertex PackInfluences(const SourceVertex& vertex, std::size_t jointCount)
		{
			PackedSkinVertex packed{};
			if (!vertex.HasSkin)
			{
				return packed;
			}
			std::array<std::pair<float, std::uint16_t>, 4> influences{};
			float sum = 0.0f;
			for (std::size_t i = 0; i < 4; ++i)
			{
				const float weight = std::isfinite(vertex.Weights[i]) ? std::max(vertex.Weights[i], 0.0f) : 0.0f;
				if (weight > 0.0f && vertex.Joints[i] >= jointCount)
				{
					throw std::runtime_error("skinned vertex references a joint outside its skin");
				}
				influences[i] = { weight, weight > 0.0f ? vertex.Joints[i] : std::uint16_t(0) };
				sum += weight;
			}
			if (!(sum > 0.0f))
			{
				return packed;
			}
			std::stable_sort(influences.begin(), influences.end(),
				[](const auto& a, const auto& b)
				{
					return a.first > b.first;
				});
			for (std::size_t i = 0; i < 4; ++i)
			{
				// Unused slots repeat the strongest joint, so they stay valid after remapping.
				packed.Joints[i] = influences[i].first > 0.0f ? influences[i].second : influences[0].second;
				packed.Weights[i] = influences[i].first / sum;
			}
			return packed;
		}

		struct SkeletonLayout
		{
			Swim::Assets::SkeletonAsset Asset;
			std::vector<std::uint16_t> Remap; // glTF skin joint index -> skeleton joint index.
		};

		SkeletonLayout BuildSkeleton(const IntermediateModel& model, const SourceSkin& skin, const std::vector<std::string>& names)
		{
			const std::size_t count = skin.Joints.size();
			std::vector<Matrix4> world(model.Nodes.size(), IdentityMatrix);
			std::vector<bool> computed(model.Nodes.size(), false);
			const auto worldOf = [&](auto&& self, std::uint32_t node) -> const Matrix4&
			{
				if (!computed[node])
				{
					const SourceNode& source = model.Nodes[node];
					const Matrix4 local = ToMatrix(source.LocalTransform);
					world[node] = source.Parent == SourceNode::InvalidNode ? local : Multiply(self(self, source.Parent), local);
					computed[node] = true;
				}
				return world[node];
			};

			std::unordered_map<std::uint32_t, std::size_t> jointOfNode;
			for (std::size_t joint = 0; joint < count; ++joint)
			{
				if (!jointOfNode.emplace(skin.Joints[joint], joint).second)
				{
					throw std::runtime_error("glTF skin lists a joint twice");
				}
			}
			// Nearest ancestor that is a joint of this skin, plus whether the link is direct.
			std::vector<std::optional<std::size_t>> parent(count);
			std::vector<bool> direct(count, false);
			std::vector<std::uint32_t> depth(count, 0);
			for (std::size_t joint = 0; joint < count; ++joint)
			{
				std::uint32_t node = model.Nodes[skin.Joints[joint]].Parent;
				bool first = true;
				std::uint32_t steps = 0;
				while (node != SourceNode::InvalidNode)
				{
					if (++steps > model.Nodes.size())
					{
						throw std::runtime_error("glTF node hierarchy contains a cycle");
					}
					if (const auto found = jointOfNode.find(node); found != jointOfNode.end())
					{
						parent[joint] = found->second;
						direct[joint] = first;
						break;
					}
					first = false;
					node = model.Nodes[node].Parent;
				}
			}
			for (std::size_t joint = 0; joint < count; ++joint)
			{
				for (auto walk = parent[joint]; walk.has_value(); walk = parent[*walk])
				{
					++depth[joint];
				}
			}
			std::vector<std::size_t> order(count);
			for (std::size_t joint = 0; joint < count; ++joint)
			{
				order[joint] = joint;
			}
			std::stable_sort(order.begin(), order.end(),
				[&](std::size_t a, std::size_t b)
				{
					return depth[a] < depth[b];
				});

			SkeletonLayout layout;
			layout.Remap.resize(count);
			for (std::size_t position = 0; position < count; ++position)
			{
				layout.Remap[order[position]] = static_cast<std::uint16_t>(position);
			}

			// RootTransform: the rest world transform above the first root joint.
			const std::uint32_t firstRootNode = skin.Joints[order[0]];
			const std::uint32_t rootParent = model.Nodes[firstRootNode].Parent;
			const Matrix4 rootTransform = rootParent == SourceNode::InvalidNode ? IdentityMatrix : worldOf(worldOf, rootParent);
			layout.Asset.RootTransform = rootTransform;
			const Matrix4 inverseRoot = InverseAffine(rootTransform);

			layout.Asset.Joints.resize(count);
			for (std::size_t position = 0; position < count; ++position)
			{
				const std::size_t joint = order[position];
				const std::uint32_t node = skin.Joints[joint];
				Swim::Assets::SkeletonJoint& out = layout.Asset.Joints[position];
				out.Name = names[node];
				out.SourceNode = node;
				out.InverseBind = skin.InverseBindMatrices[joint];
				if (parent[joint].has_value())
				{
					out.Parent = layout.Remap[*parent[joint]];
					// Non-joint nodes between two joints fold into the rest transform.
					out.RestTransform = direct[joint]
						? model.Nodes[node].LocalTransform
						: Decompose(Multiply(InverseAffine(worldOf(worldOf, skin.Joints[*parent[joint]])), worldOf(worldOf, node)));
				}
				else
				{
					out.RestTransform = model.Nodes[node].Parent == rootParent ? model.Nodes[node].LocalTransform
																			   : Decompose(Multiply(inverseRoot, worldOf(worldOf, node)));
				}
			}
			return layout;
		}

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
				return (static_cast<std::size_t>(key.TextureIndex) * 1315423911u) ^ (static_cast<std::size_t>(key.ColorSpace) << 8) ^
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
			return filter == SourceFilter::Nearest ? Swim::Assets::SamplerFilter::Nearest : Swim::Assets::SamplerFilter::Linear;
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

			asset.Parameters = { { "BaseColorFactor", Swim::Assets::MaterialParameterType::Float4, { 1.0f, 1.0f, 1.0f, 1.0f } },
				{ "EmissiveFactor", Swim::Assets::MaterialParameterType::Float3, {} },
				{ "MetallicFactor", Swim::Assets::MaterialParameterType::Float, { 1.0f, 0.0f, 0.0f, 0.0f } },
				{ "RoughnessFactor", Swim::Assets::MaterialParameterType::Float, { 1.0f, 0.0f, 0.0f, 0.0f } },
				{ "AlphaCutoff", Swim::Assets::MaterialParameterType::Float, { 0.5f, 0.0f, 0.0f, 0.0f } } };
			return asset;
		}

		Swim::Assets::MaterialInstanceAsset BuildMaterialInstance(
			const SourceMaterial& source, Swim::Assets::AssetHandle<Swim::Assets::MaterialTemplateAsset> materialTemplate)
		{
			Swim::Assets::MaterialInstanceAsset asset;
			asset.Template = materialTemplate;
			asset.Parameters = { { "BaseColorFactor", source.BaseColorFactor },
				{ "EmissiveFactor", { source.EmissiveFactor[0], source.EmissiveFactor[1], source.EmissiveFactor[2], 0.0f } },
				{ "MetallicFactor", { source.MetallicFactor, 0.0f, 0.0f, 0.0f } },
				{ "RoughnessFactor", { source.RoughnessFactor, 0.0f, 0.0f, 0.0f } },
				{ "AlphaCutoff", { source.AlphaCutoff, 0.0f, 0.0f, 0.0f } } };
			return asset;
		}

		std::vector<Swim::Assets::AssetId> UniqueDependencies(std::vector<Swim::Assets::AssetId> dependencies)
		{
			std::sort(dependencies.begin(), dependencies.end(),
				[](auto left, auto right)
				{
					return left.Value < right.Value;
				});
			dependencies.erase(std::unique(dependencies.begin(), dependencies.end()), dependencies.end());
			return dependencies;
		}
	} // namespace

	Swim::Assets::ContentHash GetStaticModelCompilerProfileHash()
	{
		return Swim::Assets::ComputeContentHash(
			"SwimStaticModelCompiler:v2;sasset=1;fastgltf=0.9.0;draco=1.5.7;meshoptimizer=1.1;"
			"mesh=float32-interleaved-v1;skin=u16x4-f32x4-sorted-v1;morph=dense-v1;skeleton=parents-first-v1;animation=v1;"
			"texture=ktx2-or-rgba8-mips-v3");
	}

	StaticModelCompileResult StaticModelCompiler::Compile(const IntermediateModel& model, std::string_view sourceLogicalPath,
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
				return MakeError(
					StaticModelCompileErrorCode::InvalidSourceData, "could not initialize compiler-side asset identity context");
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
			emit(Swim::Assets::SassetAssetType::Sampler, defaultSampler.GetId(), defaultSamplerPath, {},
				SerializeAssetPayload(defaultSamplerAsset), false);
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
					const SourceImageTextureCompileResult compiled =
						CompileSourceImageTexture(image.EncodedBytes, mimeType, colorSpace, semantic);
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
				emit(Swim::Assets::SassetAssetType::MaterialTemplate, templateHandle.GetId(), templatePath, {},
					SerializeAssetPayload(templateAsset), false);

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

				bindTexture("BaseColorTexture", source.BaseColorTexture, Swim::Assets::TextureColorSpace::SRgb,
					Swim::Assets::TextureSemantic::Color);
				bindTexture("MetallicRoughnessTexture", source.MetallicRoughnessTexture, Swim::Assets::TextureColorSpace::Linear,
					Swim::Assets::TextureSemantic::Data);
				bindTexture(
					"NormalTexture", source.NormalTexture, Swim::Assets::TextureColorSpace::Linear, Swim::Assets::TextureSemantic::Normal);
				bindTexture("OcclusionTexture", source.OcclusionTexture, Swim::Assets::TextureColorSpace::Linear,
					Swim::Assets::TextureSemantic::Data);
				bindTexture(
					"EmissiveTexture", source.EmissiveTexture, Swim::Assets::TextureColorSpace::SRgb, Swim::Assets::TextureSemantic::Color);

				emit(Swim::Assets::SassetAssetType::MaterialInstance, materialHandle.GetId(), materialPath, std::move(dependencies),
					SerializeAssetPayload(materialAsset), false);
				++result.Stats.Materials;
			}

			const std::vector<std::string> nodeNames = UniqueNodeNames(model);
			std::vector<Swim::Assets::AssetHandle<Swim::Assets::SkeletonAsset>> skeletonHandles(model.Skins.size());
			std::vector<std::vector<std::uint16_t>> skinRemaps(model.Skins.size());
			for (std::size_t skinIndex = 0; skinIndex < model.Skins.size(); ++skinIndex)
			{
				SkeletonLayout layout = BuildSkeleton(model, model.Skins[skinIndex], nodeNames);
				skinRemaps[skinIndex] = std::move(layout.Remap);
				const std::string path = ChildPath(result.RootLogicalPath, "skeleton", skinIndex);
				const auto handle = ids.Declare<Swim::Assets::SkeletonAsset>(path);
				skeletonHandles[skinIndex] = handle;
				emit(Swim::Assets::SassetAssetType::Skeleton, handle.GetId(), path, {}, SerializeAssetPayload(layout.Asset), false);
				++result.Stats.Skeletons;
			}
			// The skin a mesh's joint indices address (one per mesh: its remap is baked in).
			std::vector<std::optional<std::uint32_t>> meshSkins(model.Meshes.size());
			for (const SourceNode& node : model.Nodes)
			{
				if (!node.MeshIndex.has_value() || !node.SkinIndex.has_value() || *node.MeshIndex >= model.Meshes.size())
				{
					continue;
				}
				auto& skin = meshSkins[*node.MeshIndex];
				if (skin.has_value() && *skin != *node.SkinIndex && skinRemaps[*skin] != skinRemaps[*node.SkinIndex])
				{
					return MakeError(StaticModelCompileErrorCode::InvalidSourceData,
						"a mesh is instanced with two skins whose joint orders differ; split the mesh per skin");
				}
				skin = *node.SkinIndex;
			}

			std::vector<Swim::Assets::AssetHandle<Swim::Assets::MeshAsset>> meshHandles(model.Meshes.size());
			std::vector<std::vector<std::optional<std::uint32_t>>> meshMaterialSlots(model.Meshes.size());
			for (std::size_t meshIndex = 0; meshIndex < model.Meshes.size(); ++meshIndex)
			{
				const SourceMesh& sourceMesh = model.Meshes[meshIndex];
				Swim::Assets::MeshAsset meshAsset;
				meshAsset.IndexFormat = Swim::Assets::IndexElementFormat::UInt32;
				meshAsset.VertexStreams.push_back({ static_cast<std::uint32_t>(sizeof(PackedStaticVertex)), 0, 0 });
				meshAsset.VertexAttributes = { { Swim::Assets::VertexSemantic::Position, Swim::Assets::VertexElementFormat::Float32x3, 0,
												   static_cast<std::uint32_t>(offsetof(PackedStaticVertex, Position)) },
					{ Swim::Assets::VertexSemantic::Normal, Swim::Assets::VertexElementFormat::Float32x3, 0,
						static_cast<std::uint32_t>(offsetof(PackedStaticVertex, Normal)) },
					{ Swim::Assets::VertexSemantic::Tangent, Swim::Assets::VertexElementFormat::Float32x4, 0,
						static_cast<std::uint32_t>(offsetof(PackedStaticVertex, Tangent)) },
					{ Swim::Assets::VertexSemantic::TexCoord0, Swim::Assets::VertexElementFormat::Float32x2, 0,
						static_cast<std::uint32_t>(offsetof(PackedStaticVertex, TexCoord0)) } };

				bool skinned = false;
				std::size_t targetCount = 0;
				std::size_t totalVertices = 0;
				for (const SourcePrimitive& primitive : sourceMesh.Primitives)
				{
					skinned = skinned ||
						std::any_of(primitive.Vertices.begin(), primitive.Vertices.end(),
							[](const SourceVertex& v)
							{
								return v.HasSkin;
							});
					targetCount = std::max(targetCount, primitive.Targets.size());
					totalVertices += primitive.Vertices.size();
				}
				const std::vector<std::uint16_t>* remap = meshSkins[meshIndex].has_value() ? &skinRemaps[*meshSkins[meshIndex]] : nullptr;
				const std::size_t jointCount = remap ? remap->size() : 65536u;
				std::vector<std::byte> skinBytes;
				meshAsset.MorphTargets.resize(targetCount);
				for (std::size_t target = 0; target < targetCount; ++target)
				{
					meshAsset.MorphTargets[target].Name = "target" + std::to_string(target);
					for (const SourcePrimitive& primitive : sourceMesh.Primitives)
					{
						if (target >= primitive.Targets.size())
						{
							continue;
						}
						const SourceMorphTarget& source = primitive.Targets[target];
						auto& out = meshAsset.MorphTargets[target];
						if (!source.Position.empty())
						{
							out.PositionDeltas.assign(totalVertices * 3, 0.0f);
						}
						if (!source.Normal.empty())
						{
							out.NormalDeltas.assign(totalVertices * 3, 0.0f);
						}
						if (!source.Tangent.empty())
						{
							out.TangentDeltas.assign(totalVertices * 3, 0.0f);
						}
					}
				}
				if (targetCount > 0)
				{
					meshAsset.DefaultMorphWeights.assign(targetCount, 0.0f);
					for (std::size_t target = 0; target < std::min(targetCount, sourceMesh.DefaultWeights.size()); ++target)
					{
						meshAsset.DefaultMorphWeights[target] = sourceMesh.DefaultWeights[target];
					}
					result.Stats.MorphTargets += targetCount;
				}

				std::vector<std::optional<std::uint32_t>>& slots = meshMaterialSlots[meshIndex];
				for (const SourcePrimitive& primitive : sourceMesh.Primitives)
				{
					if (primitive.Topology != SourcePrimitiveTopology::Triangles)
					{
						return MakeError(StaticModelCompileErrorCode::UnsupportedTopology,
							"static .sasset mesh v1 currently supports triangle primitives only");
					}
					if (primitive.Vertices.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
					{
						return MakeError(StaticModelCompileErrorCode::Overflow, "mesh vertex count exceeds .sasset v1 vertex-offset range");
					}
					const std::size_t baseVertex = meshAsset.VertexBytes.size() / sizeof(PackedStaticVertex);
					const std::size_t firstIndex = meshAsset.IndexBytes.size() / sizeof(std::uint32_t);
					if (baseVertex > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()) ||
						firstIndex > std::numeric_limits<std::uint32_t>::max() ||
						primitive.Indices.size() > std::numeric_limits<std::uint32_t>::max())
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
						if (skinned)
						{
							PackedSkinVertex influences = PackInfluences(sourceVertex, jointCount);
							if (remap)
							{
								for (auto& joint : influences.Joints)
								{
									joint = (*remap)[joint];
								}
							}
							AppendBytes(skinBytes, &influences, sizeof(influences));
						}
					}
					for (std::size_t target = 0; target < primitive.Targets.size(); ++target)
					{
						const SourceMorphTarget& source = primitive.Targets[target];
						auto& out = meshAsset.MorphTargets[target];
						const auto scatter = [&](const std::vector<std::array<float, 3>>& deltas, std::vector<float>& destination)
						{
							for (std::size_t v = 0; v < deltas.size(); ++v)
							{
								for (std::size_t c = 0; c < 3; ++c)
								{
									destination[(baseVertex + v) * 3 + c] = deltas[v][c];
								}
							}
						};
						scatter(source.Position, out.PositionDeltas);
						scatter(source.Normal, out.NormalDeltas);
						scatter(source.Tangent, out.TangentDeltas);
					}
					AppendBytes(meshAsset.IndexBytes, primitive.Indices.data(), primitive.Indices.size() * sizeof(std::uint32_t));

					auto slotIt = std::find(slots.begin(), slots.end(), primitive.MaterialIndex);
					if (slotIt == slots.end())
					{
						slots.push_back(primitive.MaterialIndex);
						slotIt = std::prev(slots.end());
					}
					const std::uint32_t slot = static_cast<std::uint32_t>(std::distance(slots.begin(), slotIt));
					meshAsset.Primitives.push_back(
						{ static_cast<std::uint32_t>(firstIndex), static_cast<std::uint32_t>(primitive.Indices.size()),
							static_cast<std::int32_t>(baseVertex), slot, primitive.Bounds });
					ExpandBounds(meshAsset.Bounds, primitive.Bounds);
				}
				meshAsset.VertexStreams.front().DataSizeBytes = meshAsset.VertexBytes.size();
				if (skinned)
				{
					meshAsset.VertexStreams.push_back(
						{ static_cast<std::uint32_t>(sizeof(PackedSkinVertex)), meshAsset.VertexBytes.size(), skinBytes.size() });
					meshAsset.VertexAttributes.push_back({ Swim::Assets::VertexSemantic::Joints0,
						Swim::Assets::VertexElementFormat::UInt16x4, 1, static_cast<std::uint32_t>(offsetof(PackedSkinVertex, Joints)) });
					meshAsset.VertexAttributes.push_back({ Swim::Assets::VertexSemantic::Weights0,
						Swim::Assets::VertexElementFormat::Float32x4, 1, static_cast<std::uint32_t>(offsetof(PackedSkinVertex, Weights)) });
					meshAsset.VertexBytes.insert(meshAsset.VertexBytes.end(), skinBytes.begin(), skinBytes.end());
					++result.Stats.SkinnedMeshes;
				}
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
				node.MorphWeights = sourceNode.Weights;
				if (!sourceNode.MeshIndex.has_value())
				{
					continue;
				}
				if (sourceNode.SkinIndex.has_value())
				{
					node.Skin = skeletonHandles[*sourceNode.SkinIndex];
					modelDependencies.push_back(node.Skin.GetId());
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
						return MakeError(StaticModelCompileErrorCode::InvalidSourceData,
							"mesh primitive references a material outside the material table");
					}
					node.Materials.push_back(materialHandles[*sourceMaterialIndex]);
					modelDependencies.push_back(materialHandles[*sourceMaterialIndex].GetId());
				}
			}

			for (std::size_t animationIndex = 0; animationIndex < model.Animations.size(); ++animationIndex)
			{
				const SourceAnimation& source = model.Animations[animationIndex];
				Swim::Assets::AnimationClipAsset clip;
				clip.Name = source.Name.empty() ? "animation" + std::to_string(animationIndex) : source.Name;
				for (const SourceAnimationChannel& channel : source.Channels)
				{
					if (channel.Node >= nodeNames.size() || channel.Times.empty())
					{
						continue;
					}
					Swim::Assets::AnimationTrack track;
					track.Target = nodeNames[channel.Node];
					track.Path = channel.Path;
					track.Interpolation = channel.Interpolation;
					track.Components = channel.Components;
					track.Times = channel.Times;
					track.Values = channel.Values;
					for (std::size_t key = 1; key < track.Times.size(); ++key)
					{
						if (!(track.Times[key] > track.Times[key - 1]))
						{
							return MakeError(
								StaticModelCompileErrorCode::InvalidSourceData, "animation sampler times are not strictly increasing");
						}
					}
					clip.Duration = std::max(clip.Duration, track.Times.back());
					clip.Tracks.push_back(std::move(track));
				}
				const std::string path = ChildPath(result.RootLogicalPath, "animation", animationIndex);
				const auto handle = ids.Declare<Swim::Assets::AnimationClipAsset>(path);
				modelAsset.Animations.push_back(handle);
				modelDependencies.push_back(handle.GetId());
				emit(Swim::Assets::SassetAssetType::AnimationClip, handle.GetId(), path, {}, SerializeAssetPayload(clip), false);
				++result.Stats.Animations;
			}
			modelAsset.Skeletons = skeletonHandles;
			for (const auto& skeleton : skeletonHandles)
			{
				modelDependencies.push_back(skeleton.GetId());
			}

			const auto rootHandle = ids.Declare<Swim::Assets::ModelAsset>(result.RootLogicalPath);
			result.RootId = rootHandle.GetId();
			emit(Swim::Assets::SassetAssetType::Model, rootHandle.GetId(), result.RootLogicalPath, std::move(modelDependencies),
				SerializeAssetPayload(modelAsset), true);
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

} // namespace Swim::AssetCompiler
