#pragma once

#include "Engine/Assets/AssetMath.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace Swim::AssetCompiler
{

	enum class SourcePrimitiveTopology : std::uint8_t
	{
		Points,
		Lines,
		LineLoop,
		LineStrip,
		Triangles,
		TriangleStrip,
		TriangleFan
	};

	enum class SourceAlphaMode : std::uint8_t
	{
		Opaque,
		Mask,
		Blend
	};

	enum class SourceImageMimeType : std::uint8_t
	{
		Unknown,
		Jpeg,
		Png,
		Ktx2,
		Dds,
		WebP
	};

	enum class SourceFilter : std::uint8_t
	{
		Unspecified,
		Nearest,
		Linear,
		NearestMipmapNearest,
		LinearMipmapNearest,
		NearestMipmapLinear,
		LinearMipmapLinear
	};

	enum class SourceWrap : std::uint8_t
	{
		Repeat,
		MirroredRepeat,
		ClampToEdge
	};

	struct SourceVertex
	{
		std::array<float, 3> Position{};
		std::array<float, 3> Normal{};
		std::array<float, 4> Tangent{};
		std::array<float, 2> TexCoord0{};
		bool HasNormal = false;
		bool HasTangent = false;
		bool HasTexCoord0 = false;
	};

	struct SourcePrimitive
	{
		SourcePrimitiveTopology Topology = SourcePrimitiveTopology::Triangles;
		std::vector<SourceVertex> Vertices;
		std::vector<std::uint32_t> Indices;
		std::optional<std::uint32_t> MaterialIndex;
		Swim::Assets::AssetBounds Bounds{};
	};

	struct SourceMesh
	{
		std::string Name;
		std::vector<SourcePrimitive> Primitives;
	};

	struct SourceMaterial
	{
		std::string Name;
		std::array<float, 4> BaseColorFactor{ 1.0f, 1.0f, 1.0f, 1.0f };
		std::array<float, 3> EmissiveFactor{};
		float MetallicFactor = 1.0f;
		float RoughnessFactor = 1.0f;
		float AlphaCutoff = 0.5f;
		SourceAlphaMode AlphaMode = SourceAlphaMode::Opaque;
		bool DoubleSided = false;
		bool Unlit = false;
		std::optional<std::uint32_t> BaseColorTexture;
		std::optional<std::uint32_t> MetallicRoughnessTexture;
		std::optional<std::uint32_t> NormalTexture;
		std::optional<std::uint32_t> OcclusionTexture;
		std::optional<std::uint32_t> EmissiveTexture;
	};

	struct SourceImage
	{
		std::string Name;
		SourceImageMimeType MimeType = SourceImageMimeType::Unknown;
		std::string ExternalPath;
		std::vector<std::byte> EncodedBytes;
	};

	struct SourceSampler
	{
		std::string Name;
		SourceFilter MagFilter = SourceFilter::Unspecified;
		SourceFilter MinFilter = SourceFilter::Unspecified;
		SourceWrap WrapU = SourceWrap::Repeat;
		SourceWrap WrapV = SourceWrap::Repeat;
	};

	struct SourceTexture
	{
		std::string Name;
		std::optional<std::uint32_t> ImageIndex;
		std::optional<std::uint32_t> SamplerIndex;
	};

	struct SourceNode
	{
		static constexpr std::uint32_t InvalidNode = std::numeric_limits<std::uint32_t>::max();

		std::string Name;
		std::uint32_t Parent = InvalidNode;
		Swim::Assets::AssetTransform LocalTransform{};
		std::optional<std::uint32_t> MeshIndex;
	};

	struct IntermediateModel
	{
		// Relative loose-source files referenced by a .gltf. The development cooker
		// hashes these alongside the root source so external .bin/image edits invalidate
		// the matching cooked .sasset without making runtime Assets know about glTF.
		std::vector<std::string> ExternalDependencies;
		std::vector<SourceMesh> Meshes;
		std::vector<SourceMaterial> Materials;
		std::vector<SourceImage> Images;
		std::vector<SourceSampler> Samplers;
		std::vector<SourceTexture> Textures;
		std::vector<SourceNode> Nodes;
		std::vector<std::uint32_t> Roots;
	};

}
