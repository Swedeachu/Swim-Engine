#include "Engine/Systems/Renderer/Residency/MeshGeometryPayload.h"
#include "Engine/Systems/Renderer/Geometry/GpuMeshMetadata.h"
#include "Engine/Systems/Renderer/Residency/GpuMeshletPayloadHeader.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <type_traits>

namespace Swim::Render
{
	namespace
	{
		using Assets::VertexElementFormat;

		std::uint32_t FormatBytes(VertexElementFormat format)
		{
			switch (format)
			{
			case VertexElementFormat::Float32x2:
				return 8;
			case VertexElementFormat::Float32x3:
				return 12;
			case VertexElementFormat::Float32x4:
				return 16;
			case VertexElementFormat::Float16x2:
			case VertexElementFormat::UNorm8x4:
				return 4;
			case VertexElementFormat::Float16x4:
			case VertexElementFormat::SNorm16x4:
			case VertexElementFormat::UInt16x4:
				return 8;
			default:
				throw std::invalid_argument("MeshAsset uses an unknown vertex element format");
			}
		}

		void Hash(std::uint32_t& hash, std::uint32_t value)
		{
			for (int shift = 0; shift < 32; shift += 8)
			{
				hash ^= (value >> shift) & 0xffu;
				hash *= 16777619u;
			}
		}

		std::uint64_t Align16(std::uint64_t value)
		{
			return (value + 15) & ~std::uint64_t(15);
		}

		static_assert(sizeof(Assets::MeshletDesc) == 16 && std::is_trivially_copyable_v<Assets::MeshletDesc>);
	} // namespace

	GeometryMeshDesc MeshGeometryPayload::Describe(std::string_view debugName) const
	{
		GeometryMeshDesc desc;
		desc.VertexLayout = VertexLayout;
		desc.VertexStride = VertexStride;
		desc.Vertices = Vertices;
		desc.IndexFormat = IndexFormat;
		desc.Indices = Indices;
		desc.Submeshes = Submeshes;
		desc.Lods = Lods;
		desc.Meshlets = Meshlets;
		desc.MeshletCount = MeshletCount;
		desc.DebugName = debugName;
		return desc;
	}

	MeshGeometryPayload BuildMeshGeometryPayload(const Assets::MeshAsset& mesh)
	{
		if (mesh.VertexStreams.empty())
		{
			throw std::invalid_argument("MeshAsset has no vertex streams");
		}

		MeshGeometryPayload payload;
		std::uint64_t vertexCount = 0;
		std::vector<std::uint32_t> streamBase;
		for (std::size_t s = 0; s < mesh.VertexStreams.size(); ++s)
		{
			const auto& stream = mesh.VertexStreams[s];
			if (!stream.StrideBytes || stream.DataSizeBytes % stream.StrideBytes != 0 || stream.DataOffsetBytes > mesh.VertexBytes.size() ||
				stream.DataSizeBytes > mesh.VertexBytes.size() - stream.DataOffsetBytes)
			{
				throw std::invalid_argument("MeshAsset vertex stream exceeds its vertex bytes or has an invalid stride");
			}
			const auto count = stream.DataSizeBytes / stream.StrideBytes;
			if (s == 0)
			{
				vertexCount = count;
			}
			else if (count != vertexCount)
			{
				throw std::invalid_argument("MeshAsset vertex streams disagree on the vertex count");
			}
			streamBase.push_back(payload.VertexStride);
			if (payload.VertexStride > UINT32_MAX - stream.StrideBytes)
			{
				throw std::invalid_argument("MeshAsset packed vertex stride overflows");
			}
			payload.VertexStride += stream.StrideBytes;
		}
		if (!vertexCount)
		{
			throw std::invalid_argument("MeshAsset has no vertices");
		}

		struct PackedAttribute
		{
			std::uint32_t Offset;
			std::uint32_t Semantic;
			std::uint32_t Format;
		};

		std::vector<PackedAttribute> attributes;
		for (const auto& attribute : mesh.VertexAttributes)
		{
			if (attribute.StreamIndex >= mesh.VertexStreams.size() ||
				std::uint64_t(attribute.OffsetBytes) + FormatBytes(attribute.Format) >
					mesh.VertexStreams[attribute.StreamIndex].StrideBytes)
			{
				throw std::invalid_argument("MeshAsset vertex attribute lies outside its stream");
			}
			attributes.push_back({ streamBase[attribute.StreamIndex] + attribute.OffsetBytes, std::uint32_t(attribute.Semantic),
				std::uint32_t(attribute.Format) });
		}
		std::sort(attributes.begin(), attributes.end(),
			[](const PackedAttribute& a, const PackedAttribute& b)
			{
				return a.Offset != b.Offset ? a.Offset < b.Offset : a.Semantic < b.Semantic;
			});
		std::uint32_t layout = 2166136261u;
		Hash(layout, payload.VertexStride);
		for (const auto& attribute : attributes)
		{
			Hash(layout, attribute.Offset);
			Hash(layout, attribute.Semantic);
			Hash(layout, attribute.Format);
		}
		payload.VertexLayout = layout ? layout : 1;

		// Interleave: one stream is a straight copy; several are packed per vertex.
		payload.Vertices.resize(static_cast<std::size_t>(vertexCount * payload.VertexStride));
		for (std::size_t s = 0; s < mesh.VertexStreams.size(); ++s)
		{
			const auto& stream = mesh.VertexStreams[s];
			const auto* source = mesh.VertexBytes.data() + stream.DataOffsetBytes;
			if (mesh.VertexStreams.size() == 1)
			{
				std::memcpy(payload.Vertices.data(), source, static_cast<std::size_t>(stream.DataSizeBytes));
				break;
			}
			for (std::uint64_t v = 0; v < vertexCount; ++v)
			{
				std::memcpy(payload.Vertices.data() + v * payload.VertexStride + streamBase[s], source + v * stream.StrideBytes,
					stream.StrideBytes);
			}
		}

		payload.IndexFormat = mesh.IndexFormat == Assets::IndexElementFormat::UInt16 ? Rhi::IndexType::Uint16 : Rhi::IndexType::Uint32;
		payload.Indices = mesh.IndexBytes;

		for (const auto& primitive : mesh.Primitives)
		{
			payload.Submeshes.push_back({ primitive.FirstIndex, primitive.IndexCount, primitive.VertexOffset, primitive.MaterialSlot });
		}
		if (!mesh.Lods.empty() && mesh.Primitives.empty())
		{
			throw std::invalid_argument("MeshAsset LODs need primitives");
		}
		// The asset's screen-coverage threshold becomes the LOD selection metric.
		const auto lodCount = std::min<std::size_t>(mesh.Lods.size(), GpuMeshMetadata::MaxLods);
		for (std::size_t i = 0; i < lodCount; ++i)
		{
			payload.Lods.push_back({ mesh.Lods[i].FirstPrimitive, mesh.Lods[i].PrimitiveCount, mesh.Lods[i].ScreenCoverage });
		}

		if (!mesh.Meshlets.empty())
		{
			const std::uint64_t descriptorBytes = mesh.Meshlets.size() * sizeof(Assets::MeshletDesc);
			GpuMeshletPayloadHeader header;
			header.MeshletCount = static_cast<std::uint32_t>(mesh.Meshlets.size());
			const std::uint64_t descriptorOffset = Align16(sizeof(header));
			const std::uint64_t vertexOffset = Align16(descriptorOffset + descriptorBytes);
			const std::uint64_t triangleOffset = Align16(vertexOffset + mesh.MeshletVertexBytes.size());
			const std::uint64_t total = triangleOffset + mesh.MeshletTriangleBytes.size();
			if (total > UINT32_MAX)
			{
				throw std::invalid_argument("MeshAsset meshlet payload exceeds 4 GiB");
			}
			header.DescriptorOffset = static_cast<std::uint32_t>(descriptorOffset);
			header.VertexIndexOffset = static_cast<std::uint32_t>(vertexOffset);
			header.VertexIndexBytes = static_cast<std::uint32_t>(mesh.MeshletVertexBytes.size());
			header.TriangleOffset = static_cast<std::uint32_t>(triangleOffset);
			header.TriangleBytes = static_cast<std::uint32_t>(mesh.MeshletTriangleBytes.size());
			payload.Meshlets.resize(static_cast<std::size_t>(total));
			std::memcpy(payload.Meshlets.data(), &header, sizeof(header));
			std::memcpy(payload.Meshlets.data() + descriptorOffset, mesh.Meshlets.data(), static_cast<std::size_t>(descriptorBytes));
			if (!mesh.MeshletVertexBytes.empty())
			{
				std::memcpy(payload.Meshlets.data() + vertexOffset, mesh.MeshletVertexBytes.data(), mesh.MeshletVertexBytes.size());
			}
			if (!mesh.MeshletTriangleBytes.empty())
			{
				std::memcpy(payload.Meshlets.data() + triangleOffset, mesh.MeshletTriangleBytes.data(), mesh.MeshletTriangleBytes.size());
			}
			payload.MeshletCount = header.MeshletCount;
		}
		return payload;
	}
} // namespace Swim::Render
