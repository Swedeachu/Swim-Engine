#include "Tools/AssetCompiler/GltfImporter.h"

#include <fastgltf/core.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/types.hpp>

#include <draco/compression/decode.h>
#include <draco/core/decoder_buffer.h>
#include <draco/mesh/mesh.h>

#include <algorithm>
#include <cstddef>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

namespace Swim::AssetCompiler
{

	namespace
	{
		class UnsupportedSourceFeature final : public std::runtime_error
		{
		public:
			explicit UnsupportedSourceFeature(const std::string& message)
				: std::runtime_error(message)
			{}
		};

		std::uint32_t ToIndex(std::size_t value)
		{
			if (value > std::numeric_limits<std::uint32_t>::max())
			{
				throw std::overflow_error("glTF index exceeds Swim's 32-bit intermediate model limit");
			}
			return static_cast<std::uint32_t>(value);
		}

		std::optional<std::uint32_t> ToOptionalIndex(const fastgltf::Optional<std::size_t>& value)
		{
			if (!value.has_value())
			{
				return std::nullopt;
			}
			return ToIndex(*value);
		}

		SourcePrimitiveTopology ConvertTopology(fastgltf::PrimitiveType topology)
		{
			switch (topology)
			{
			case fastgltf::PrimitiveType::Points:
				return SourcePrimitiveTopology::Points;
			case fastgltf::PrimitiveType::Lines:
				return SourcePrimitiveTopology::Lines;
			case fastgltf::PrimitiveType::LineLoop:
				return SourcePrimitiveTopology::LineLoop;
			case fastgltf::PrimitiveType::LineStrip:
				return SourcePrimitiveTopology::LineStrip;
			case fastgltf::PrimitiveType::Triangles:
				return SourcePrimitiveTopology::Triangles;
			case fastgltf::PrimitiveType::TriangleStrip:
				return SourcePrimitiveTopology::TriangleStrip;
			case fastgltf::PrimitiveType::TriangleFan:
				return SourcePrimitiveTopology::TriangleFan;
			}
			return SourcePrimitiveTopology::Triangles;
		}

		SourceAlphaMode ConvertAlphaMode(fastgltf::AlphaMode mode)
		{
			switch (mode)
			{
			case fastgltf::AlphaMode::Opaque:
				return SourceAlphaMode::Opaque;
			case fastgltf::AlphaMode::Mask:
				return SourceAlphaMode::Mask;
			case fastgltf::AlphaMode::Blend:
				return SourceAlphaMode::Blend;
			}
			return SourceAlphaMode::Opaque;
		}

		SourceImageMimeType ConvertMimeType(fastgltf::MimeType type)
		{
			switch (type)
			{
			case fastgltf::MimeType::JPEG:
				return SourceImageMimeType::Jpeg;
			case fastgltf::MimeType::PNG:
				return SourceImageMimeType::Png;
			case fastgltf::MimeType::KTX2:
				return SourceImageMimeType::Ktx2;
			case fastgltf::MimeType::DDS:
				return SourceImageMimeType::Dds;
			case fastgltf::MimeType::WEBP:
				return SourceImageMimeType::WebP;
			default:
				return SourceImageMimeType::Unknown;
			}
		}

		SourceFilter ConvertFilter(const fastgltf::Optional<fastgltf::Filter>& filter)
		{
			if (!filter.has_value())
			{
				return SourceFilter::Unspecified;
			}

			switch (*filter)
			{
			case fastgltf::Filter::Nearest:
				return SourceFilter::Nearest;
			case fastgltf::Filter::Linear:
				return SourceFilter::Linear;
			case fastgltf::Filter::NearestMipMapNearest:
				return SourceFilter::NearestMipmapNearest;
			case fastgltf::Filter::LinearMipMapNearest:
				return SourceFilter::LinearMipmapNearest;
			case fastgltf::Filter::NearestMipMapLinear:
				return SourceFilter::NearestMipmapLinear;
			case fastgltf::Filter::LinearMipMapLinear:
				return SourceFilter::LinearMipmapLinear;
			}
			return SourceFilter::Unspecified;
		}

		SourceWrap ConvertWrap(fastgltf::Wrap wrap)
		{
			switch (wrap)
			{
			case fastgltf::Wrap::Repeat:
				return SourceWrap::Repeat;
			case fastgltf::Wrap::MirroredRepeat:
				return SourceWrap::MirroredRepeat;
			case fastgltf::Wrap::ClampToEdge:
				return SourceWrap::ClampToEdge;
			}
			return SourceWrap::Repeat;
		}

		void CopyBytes(std::vector<std::byte>& destination, const std::byte* data, std::size_t size)
		{
			if (!data || size == 0)
			{
				destination.clear();
				return;
			}
			destination.assign(data, data + size);
		}

		bool CopyDataSourceBytes(const fastgltf::DataSource& source, std::vector<std::byte>& bytes)
		{
			bool copied = false;
			std::visit(fastgltf::visitor{
				[&](const fastgltf::sources::Array& data)
				{
					CopyBytes(bytes, data.bytes.data(), data.bytes.size());
					copied = true;
				},
				[&](const fastgltf::sources::Vector& data)
				{
					CopyBytes(bytes, data.bytes.data(), data.bytes.size());
					copied = true;
				},
				[&](const fastgltf::sources::ByteView& data)
				{
					CopyBytes(bytes, data.bytes.data(), data.bytes.size());
					copied = true;
				},
				[](const auto&)
				{
				}
			}, source);
			return copied;
		}


		bool CopyBufferViewBytes(const fastgltf::Asset& asset, std::size_t bufferViewIndex, std::vector<std::byte>& bytes)
		{
			if (bufferViewIndex >= asset.bufferViews.size())
			{
				return false;
			}
			const fastgltf::BufferView& view = asset.bufferViews[bufferViewIndex];
			if (view.bufferIndex >= asset.buffers.size())
			{
				return false;
			}

			std::vector<std::byte> bufferBytes;
			if (!CopyDataSourceBytes(asset.buffers[view.bufferIndex].data, bufferBytes))
			{
				return false;
			}
			if (view.byteOffset > bufferBytes.size() || view.byteLength > bufferBytes.size() - view.byteOffset)
			{
				return false;
			}
			bytes.assign(
				bufferBytes.begin() + static_cast<std::ptrdiff_t>(view.byteOffset),
				bufferBytes.begin() + static_cast<std::ptrdiff_t>(view.byteOffset + view.byteLength)
			);
			return true;
		}

		fastgltf::MimeType GetDataSourceMimeType(const fastgltf::DataSource& source)
		{
			fastgltf::MimeType result = fastgltf::MimeType::None;
			std::visit(fastgltf::visitor{
				[&](const fastgltf::sources::BufferView& data) { result = data.mimeType; },
				[&](const fastgltf::sources::URI& data) { result = data.mimeType; },
				[&](const fastgltf::sources::Array& data) { result = data.mimeType; },
				[&](const fastgltf::sources::Vector& data) { result = data.mimeType; },
				[&](const fastgltf::sources::CustomBuffer& data) { result = data.mimeType; },
				[&](const fastgltf::sources::ByteView& data) { result = data.mimeType; },
				[](const auto&) {}
			}, source);
			return result;
		}

		SourceImage ImportImage(const fastgltf::Asset& asset, const fastgltf::Image& image)
		{
			SourceImage result{};
			result.Name.assign(image.name.begin(), image.name.end());
			result.MimeType = ConvertMimeType(GetDataSourceMimeType(image.data));

			if (CopyDataSourceBytes(image.data, result.EncodedBytes))
			{
				return result;
			}

			std::visit(fastgltf::visitor{
				[&](const fastgltf::sources::URI& data)
				{
					const std::filesystem::path path = data.uri.fspath();
					result.ExternalPath = path.generic_string();
				},
				[&](const fastgltf::sources::BufferView& data)
				{
					if (data.bufferViewIndex >= asset.bufferViews.size())
					{
						return;
					}
					const auto& view = asset.bufferViews[data.bufferViewIndex];
					if (view.bufferIndex >= asset.buffers.size())
					{
						return;
					}

					std::vector<std::byte> bufferBytes;
					if (!CopyDataSourceBytes(asset.buffers[view.bufferIndex].data, bufferBytes))
					{
						return;
					}
					if (view.byteOffset > bufferBytes.size() || view.byteLength > bufferBytes.size() - view.byteOffset)
					{
						return;
					}
					result.EncodedBytes.assign(
						bufferBytes.begin() + static_cast<std::ptrdiff_t>(view.byteOffset),
						bufferBytes.begin() + static_cast<std::ptrdiff_t>(view.byteOffset + view.byteLength)
					);
				},
				[](const auto&)
				{
				}
			}, image.data);
			return result;
		}


		void CollectExternalDependencies(const fastgltf::Asset& asset, IntermediateModel& model)
		{
			auto collect = [&](const fastgltf::DataSource& source)
			{
				std::visit(fastgltf::visitor{
					[&](const fastgltf::sources::URI& data)
					{
						if (!data.uri.isLocalPath())
						{
							return;
						}
						const std::filesystem::path path = data.uri.fspath();
						if (!path.empty())
						{
							model.ExternalDependencies.push_back(path.generic_string());
						}
					},
					[](const auto&)
					{
					}
				}, source);
			};

			for (const fastgltf::Buffer& buffer : asset.buffers)
			{
				collect(buffer.data);
			}
			for (const fastgltf::Image& image : asset.images)
			{
				collect(image.data);
			}

			std::sort(model.ExternalDependencies.begin(), model.ExternalDependencies.end());
			model.ExternalDependencies.erase(
				std::unique(model.ExternalDependencies.begin(), model.ExternalDependencies.end()),
				model.ExternalDependencies.end());
		}

		Swim::Assets::AssetTransform ConvertTransform(const fastgltf::Node& node)
		{
			fastgltf::TRS trs{};
			if (const auto* storedTrs = std::get_if<fastgltf::TRS>(&node.transform))
			{
				trs = *storedTrs;
			}
			else
			{
				const auto& matrix = std::get<fastgltf::math::fmat4x4>(node.transform);
				fastgltf::math::decomposeTransformMatrix(matrix, trs.scale, trs.rotation, trs.translation);
			}

			Swim::Assets::AssetTransform result{};
			result.Translation = { trs.translation.x(), trs.translation.y(), trs.translation.z() };
			result.Rotation = { trs.rotation.x(), trs.rotation.y(), trs.rotation.z(), trs.rotation.w() };
			result.Scale = { trs.scale.x(), trs.scale.y(), trs.scale.z() };
			return result;
		}

		void ExpandBounds(Swim::Assets::AssetBounds& bounds, const std::array<float, 3>& position)
		{
			for (std::size_t axis = 0; axis < position.size(); ++axis)
			{
				bounds.Min[axis] = std::min(bounds.Min[axis], position[axis]);
				bounds.Max[axis] = std::max(bounds.Max[axis], position[axis]);
			}
		}

		std::optional<std::uint32_t> FindDracoAttributeUniqueId(
			const fastgltf::DracoCompressedPrimitive& compression,
			std::string_view semantic)
		{
			for (const fastgltf::Attribute& attribute : compression.attributes)
			{
				if (attribute.name == semantic)
				{
					return ToIndex(attribute.accessorIndex);
				}
			}
			return std::nullopt;
		}

		template <std::size_t Components, typename Store>
		void DecodeDracoAttribute(
			const draco::Mesh& mesh,
			const fastgltf::DracoCompressedPrimitive& compression,
			std::string_view semantic,
			bool required,
			Store&& store)
		{
			const std::optional<std::uint32_t> uniqueId = FindDracoAttributeUniqueId(compression, semantic);
			if (!uniqueId.has_value())
			{
				if (required)
				{
					throw std::runtime_error("Draco primitive is missing required attribute " + std::string(semantic));
				}
				return;
			}

			const draco::PointAttribute* attribute = mesh.GetAttributeByUniqueId(*uniqueId);
			if (!attribute)
			{
				throw std::runtime_error(
					"Draco payload does not contain glTF attribute " + std::string(semantic) +
					" with unique id " + std::to_string(*uniqueId)
				);
			}
			if (attribute->num_components() != static_cast<std::int8_t>(Components))
			{
				throw std::runtime_error("Draco attribute " + std::string(semantic) + " has an unexpected component count");
			}

			for (draco::PointIndex point(0); point < mesh.num_points(); ++point)
			{
				std::array<float, Components> value{};
				if (!attribute->ConvertValue<float>(
					attribute->mapped_index(point),
					static_cast<std::int8_t>(Components),
					value.data()))
				{
					throw std::runtime_error("Could not convert Draco attribute " + std::string(semantic) + " to float data");
				}
				store(static_cast<std::size_t>(point.value()), value);
			}
		}

		SourcePrimitive ImportDracoPrimitive(const fastgltf::Asset& asset, const fastgltf::Primitive& primitive)
		{
			const fastgltf::DracoCompressedPrimitive& compression = *primitive.dracoCompression;
			if (primitive.type != fastgltf::PrimitiveType::Triangles)
			{
				throw std::runtime_error("KHR_draco_mesh_compression primitive is not TRIANGLES");
			}

			std::vector<std::byte> compressedBytes;
			if (!CopyBufferViewBytes(asset, compression.bufferView, compressedBytes) || compressedBytes.empty())
			{
				throw std::runtime_error("Could not read KHR_draco_mesh_compression bufferView bytes");
			}

			draco::DecoderBuffer buffer;
			buffer.Init(reinterpret_cast<const char*>(compressedBytes.data()), compressedBytes.size());
			draco::Decoder decoder;
			auto decoded = decoder.DecodeMeshFromBuffer(&buffer);
			if (!decoded.ok() || decoded.value() == nullptr)
			{
				const std::string detail = decoded.ok()
					? "decoder returned a null mesh"
					: decoded.status().error_msg_string();
				throw std::runtime_error("Draco mesh decode failed: " + detail);
			}
			std::unique_ptr<draco::Mesh> mesh = std::move(decoded).value();

			const std::size_t pointCount = static_cast<std::size_t>(mesh->num_points());
			const std::size_t faceCount = static_cast<std::size_t>(mesh->num_faces());
			if (pointCount > std::numeric_limits<std::uint32_t>::max() ||
				faceCount > std::numeric_limits<std::uint32_t>::max() / 3u)
			{
				throw std::overflow_error("Draco mesh exceeds Swim's 32-bit intermediate model limits");
			}

			SourcePrimitive result{};
			result.Topology = SourcePrimitiveTopology::Triangles;
			result.MaterialIndex = ToOptionalIndex(primitive.materialIndex);
			result.Vertices.resize(pointCount);

			DecodeDracoAttribute<3>(*mesh, compression, "POSITION", true,
				[&](std::size_t index, const std::array<float, 3>& value)
				{
					result.Vertices[index].Position = value;
					ExpandBounds(result.Bounds, value);
				});
			DecodeDracoAttribute<3>(*mesh, compression, "NORMAL", false,
				[&](std::size_t index, const std::array<float, 3>& value)
				{
					result.Vertices[index].Normal = value;
					result.Vertices[index].HasNormal = true;
				});
			DecodeDracoAttribute<4>(*mesh, compression, "TANGENT", false,
				[&](std::size_t index, const std::array<float, 4>& value)
				{
					result.Vertices[index].Tangent = value;
					result.Vertices[index].HasTangent = true;
				});
			DecodeDracoAttribute<2>(*mesh, compression, "TEXCOORD_0", false,
				[&](std::size_t index, const std::array<float, 2>& value)
				{
					result.Vertices[index].TexCoord0 = value;
					result.Vertices[index].HasTexCoord0 = true;
				});

			result.Indices.reserve(faceCount * 3u);
			for (draco::FaceIndex faceIndex(0); faceIndex < mesh->num_faces(); ++faceIndex)
			{
				const draco::Mesh::Face& face = mesh->face(faceIndex);
				for (std::size_t corner = 0; corner < 3; ++corner)
				{
					const std::uint32_t point = static_cast<std::uint32_t>(face[corner].value());
					if (point >= pointCount)
					{
						throw std::runtime_error("Draco mesh face references an invalid point index");
					}
					result.Indices.push_back(point);
				}
			}
			return result;
		}

		SourcePrimitive ImportPrimitive(const fastgltf::Asset& asset, const fastgltf::Primitive& primitive)
		{
			if (primitive.dracoCompression)
			{
				return ImportDracoPrimitive(asset, primitive);
			}

			SourcePrimitive result{};
			result.Topology = ConvertTopology(primitive.type);
			result.MaterialIndex = ToOptionalIndex(primitive.materialIndex);

			const auto* positions = primitive.findAttribute("POSITION");
			if (positions == primitive.attributes.end() || positions->accessorIndex >= asset.accessors.size())
			{
				throw std::runtime_error("glTF primitive is missing a valid POSITION accessor");
			}

			const auto& positionAccessor = asset.accessors[positions->accessorIndex];
			result.Vertices.resize(positionAccessor.count);
			fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(asset, positionAccessor,
				[&](fastgltf::math::fvec3 position, std::size_t index)
				{
					auto& vertex = result.Vertices[index];
					vertex.Position = { position.x(), position.y(), position.z() };
					ExpandBounds(result.Bounds, vertex.Position);
				});

			if (const auto* normal = primitive.findAttribute("NORMAL"); normal != primitive.attributes.end())
			{
				const auto& accessor = asset.accessors.at(normal->accessorIndex);
				if (accessor.count != result.Vertices.size())
				{
					throw std::runtime_error("glTF NORMAL accessor count does not match POSITION count");
				}
				fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(asset, accessor,
					[&](fastgltf::math::fvec3 value, std::size_t index)
					{
						result.Vertices[index].Normal = { value.x(), value.y(), value.z() };
						result.Vertices[index].HasNormal = true;
					});
			}

			if (const auto* tangent = primitive.findAttribute("TANGENT"); tangent != primitive.attributes.end())
			{
				const auto& accessor = asset.accessors.at(tangent->accessorIndex);
				if (accessor.count != result.Vertices.size())
				{
					throw std::runtime_error("glTF TANGENT accessor count does not match POSITION count");
				}
				fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec4>(asset, accessor,
					[&](fastgltf::math::fvec4 value, std::size_t index)
					{
						result.Vertices[index].Tangent = { value.x(), value.y(), value.z(), value.w() };
						result.Vertices[index].HasTangent = true;
					});
			}

			if (const auto* texCoord = primitive.findAttribute("TEXCOORD_0"); texCoord != primitive.attributes.end())
			{
				const auto& accessor = asset.accessors.at(texCoord->accessorIndex);
				if (accessor.count != result.Vertices.size())
				{
					throw std::runtime_error("glTF TEXCOORD_0 accessor count does not match POSITION count");
				}
				fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec2>(asset, accessor,
					[&](fastgltf::math::fvec2 value, std::size_t index)
					{
						result.Vertices[index].TexCoord0 = { value.x(), value.y() };
						result.Vertices[index].HasTexCoord0 = true;
					});
			}

			if (!primitive.indicesAccessor.has_value() || *primitive.indicesAccessor >= asset.accessors.size())
			{
				throw std::runtime_error("glTF primitive did not provide generated/valid indices");
			}
			const auto& indexAccessor = asset.accessors[*primitive.indicesAccessor];
			result.Indices.resize(indexAccessor.count);
			fastgltf::copyFromAccessor<std::uint32_t>(asset, indexAccessor, result.Indices.data());
			return result;
		}
	}

	GltfImportResult GltfImporter::Import(const std::filesystem::path& path) const
	{
		GltfImportResult result{};

		try
		{
			static constexpr auto SupportedExtensions =
				fastgltf::Extensions::KHR_mesh_quantization |
				fastgltf::Extensions::KHR_texture_basisu |
				fastgltf::Extensions::KHR_texture_transform |
				fastgltf::Extensions::KHR_draco_mesh_compression |
				fastgltf::Extensions::EXT_texture_webp |
				fastgltf::Extensions::MSFT_texture_dds |
				fastgltf::Extensions::KHR_materials_unlit;

			constexpr auto Options =
				fastgltf::Options::LoadExternalBuffers |
				fastgltf::Options::LoadExternalImages |
				fastgltf::Options::DecomposeNodeMatrices |
				fastgltf::Options::GenerateMeshIndices;

			auto dependencyFile = fastgltf::MappedGltfFile::FromPath(path);
			if (!dependencyFile)
			{
				result.Error = { GltfImportErrorCode::FileOpenFailed, std::string(fastgltf::getErrorMessage(dependencyFile.error())) };
				return result;
			}

			// Parse once without loose-source loading so URI-backed buffer/image paths
			// remain visible for the cooker provenance manifest. The dependency scan and
			// real import use independent parser instances and file mappings.
			fastgltf::Parser dependencyParser(SupportedExtensions);
			auto dependencyParsed = dependencyParser.loadGltf(dependencyFile.get(), path.parent_path(), static_cast<fastgltf::Options>(0));
			if (dependencyParsed.error() == fastgltf::Error::None)
			{
				CollectExternalDependencies(dependencyParsed.get(), result.Model);
			}

			fastgltf::Parser parser(SupportedExtensions);
			auto gltfFile = fastgltf::MappedGltfFile::FromPath(path);
			if (!gltfFile)
			{
				result.Error = { GltfImportErrorCode::FileOpenFailed, std::string(fastgltf::getErrorMessage(gltfFile.error())) };
				return result;
			}
			auto parsed = parser.loadGltf(gltfFile.get(), path.parent_path(), Options);
			if (parsed.error() != fastgltf::Error::None)
			{
				result.Error = { GltfImportErrorCode::ParseFailed, std::string(fastgltf::getErrorMessage(parsed.error())) };
				return result;
			}
			const fastgltf::Asset& asset = parsed.get();

			result.Model.Images.reserve(asset.images.size());
			for (const fastgltf::Image& image : asset.images)
			{
				result.Model.Images.push_back(ImportImage(asset, image));
			}

			result.Model.Samplers.reserve(asset.samplers.size());
			for (const fastgltf::Sampler& sampler : asset.samplers)
			{
				SourceSampler out{};
				out.Name.assign(sampler.name.begin(), sampler.name.end());
				out.MagFilter = ConvertFilter(sampler.magFilter);
				out.MinFilter = ConvertFilter(sampler.minFilter);
				out.WrapU = ConvertWrap(sampler.wrapS);
				out.WrapV = ConvertWrap(sampler.wrapT);
				result.Model.Samplers.push_back(std::move(out));
			}

			result.Model.Textures.reserve(asset.textures.size());
			for (const fastgltf::Texture& texture : asset.textures)
			{
				SourceTexture out{};
				out.Name.assign(texture.name.begin(), texture.name.end());
				out.SamplerIndex = ToOptionalIndex(texture.samplerIndex);
				if (texture.imageIndex.has_value())
				{
					out.ImageIndex = ToIndex(*texture.imageIndex);
				}
				else if (texture.basisuImageIndex.has_value())
				{
					out.ImageIndex = ToIndex(*texture.basisuImageIndex);
				}
				else if (texture.webpImageIndex.has_value())
				{
					out.ImageIndex = ToIndex(*texture.webpImageIndex);
				}
				else if (texture.ddsImageIndex.has_value())
				{
					out.ImageIndex = ToIndex(*texture.ddsImageIndex);
				}
				result.Model.Textures.push_back(std::move(out));
			}

			result.Model.Materials.reserve(asset.materials.size());
			for (const fastgltf::Material& material : asset.materials)
			{
				SourceMaterial out{};
				out.Name.assign(material.name.begin(), material.name.end());
				out.BaseColorFactor = {
					material.pbrData.baseColorFactor.x(), material.pbrData.baseColorFactor.y(),
					material.pbrData.baseColorFactor.z(), material.pbrData.baseColorFactor.w()
				};
				out.EmissiveFactor = { material.emissiveFactor.x(), material.emissiveFactor.y(), material.emissiveFactor.z() };
				out.MetallicFactor = static_cast<float>(material.pbrData.metallicFactor);
				out.RoughnessFactor = static_cast<float>(material.pbrData.roughnessFactor);
				out.AlphaCutoff = static_cast<float>(material.alphaCutoff);
				out.AlphaMode = ConvertAlphaMode(material.alphaMode);
				out.DoubleSided = material.doubleSided;
				out.Unlit = material.unlit;
				if (material.pbrData.baseColorTexture)
				{
					out.BaseColorTexture = ToIndex(material.pbrData.baseColorTexture->textureIndex);
				}
				if (material.pbrData.metallicRoughnessTexture)
				{
					out.MetallicRoughnessTexture = ToIndex(material.pbrData.metallicRoughnessTexture->textureIndex);
				}
				if (material.normalTexture)
				{
					out.NormalTexture = ToIndex(material.normalTexture->textureIndex);
				}
				if (material.occlusionTexture)
				{
					out.OcclusionTexture = ToIndex(material.occlusionTexture->textureIndex);
				}
				if (material.emissiveTexture)
				{
					out.EmissiveTexture = ToIndex(material.emissiveTexture->textureIndex);
				}
				result.Model.Materials.push_back(std::move(out));
			}

			result.Model.Meshes.reserve(asset.meshes.size());
			for (const fastgltf::Mesh& mesh : asset.meshes)
			{
				SourceMesh out{};
				out.Name.assign(mesh.name.begin(), mesh.name.end());
				out.Primitives.reserve(mesh.primitives.size());
				for (const fastgltf::Primitive& primitive : mesh.primitives)
				{
					out.Primitives.push_back(ImportPrimitive(asset, primitive));
				}
				result.Model.Meshes.push_back(std::move(out));
			}

			result.Model.Nodes.resize(asset.nodes.size());
			for (std::size_t nodeIndex = 0; nodeIndex < asset.nodes.size(); ++nodeIndex)
			{
				const fastgltf::Node& node = asset.nodes[nodeIndex];
				SourceNode& out = result.Model.Nodes[nodeIndex];
				out.Name.assign(node.name.begin(), node.name.end());
				out.LocalTransform = ConvertTransform(node);
				out.MeshIndex = ToOptionalIndex(node.meshIndex);

				for (std::size_t child : node.children)
				{
					if (child >= result.Model.Nodes.size())
					{
						throw std::runtime_error("glTF node contains an invalid child index");
					}
					SourceNode& childNode = result.Model.Nodes[child];
					if (childNode.Parent != SourceNode::InvalidNode)
					{
						throw std::runtime_error("glTF node has more than one parent");
					}
					childNode.Parent = ToIndex(nodeIndex);
				}
			}

			if (asset.defaultScene.has_value() && *asset.defaultScene < asset.scenes.size())
			{
				for (std::size_t root : asset.scenes[*asset.defaultScene].nodeIndices)
				{
					result.Model.Roots.push_back(ToIndex(root));
				}
			}
			else
			{
				for (std::size_t nodeIndex = 0; nodeIndex < result.Model.Nodes.size(); ++nodeIndex)
				{
					if (result.Model.Nodes[nodeIndex].Parent == SourceNode::InvalidNode)
					{
						result.Model.Roots.push_back(ToIndex(nodeIndex));
					}
				}
			}
		}
		catch (const UnsupportedSourceFeature& error)
		{
			result.Model = {};
			result.Error = { GltfImportErrorCode::UnsupportedFeature, error.what() };
		}
		catch (const std::overflow_error& error)
		{
			result.Model = {};
			result.Error = { GltfImportErrorCode::IndexOverflow, error.what() };
		}
		catch (const std::exception& error)
		{
			result.Model = {};
			result.Error = { GltfImportErrorCode::InvalidData, error.what() };
		}

		return result;
	}

}
