#include "Engine/Assets/SassetPayloadReaders.h"

#include <cmath>
#include <limits>
#include <optional>
#include <type_traits>
#include <utility>

namespace Swim::Assets::SassetDetail
{

	static bool ReadBounds(BinaryReader& reader, AssetBounds& bounds)
	{
		for (float& value : bounds.Min)
		{
			value = reader.F32();
		}

		for (float& value : bounds.Max)
		{
			value = reader.F32();
		}

		return reader.IsValid();
	}

	static bool ReadTransform(BinaryReader& reader, AssetTransform& transform)
	{
		for (float& value : transform.Translation)
		{
			value = reader.F32();
		}

		for (float& value : transform.Rotation)
		{
			value = reader.F32();
		}

		for (float& value : transform.Scale)
		{
			value = reader.F32();
		}

		return reader.IsValid();
	}

	static bool ReadFloats(BinaryReader& reader, std::vector<float>& values)
	{
		std::uint32_t count = 0;

		if (!reader.Count(count))
		{
			return false;
		}

		values.resize(count);

		for (float& value : values)
		{
			value = reader.F32();
		}

		return reader.IsValid();
	}

	static bool ReadMatrix(BinaryReader& reader, std::array<float, 16>& matrix)
	{
		for (float& value : matrix)
		{
			value = reader.F32();
		}

		return reader.IsValid();
	}

	template <typename T> AssetHandle<T> DeclareReference(AssetSystem& assets, AssetId id)
	{
		if (!id.IsValid())
		{
			return {};
		}

		return assets.Declare<T>(id);
	}

	bool ReadMesh(BinaryReader& reader, MeshAsset& asset)
	{
		const std::uint32_t version = reader.U32();

		if (version != SassetPayloadVersion && version != SassetMeshPayloadVersion)
		{
			return false;
		}

		asset.IndexFormat = static_cast<IndexElementFormat>(reader.U8());

		if (!ReadBounds(reader, asset.Bounds))
		{
			return false;
		}

		std::uint32_t count = 0;

		if (!reader.Count(count))
		{
			return false;
		}

		asset.VertexStreams.resize(count);

		for (VertexStreamDesc& stream : asset.VertexStreams)
		{
			stream.StrideBytes = reader.U32();
			stream.DataOffsetBytes = reader.U64();
			stream.DataSizeBytes = reader.U64();
		}

		if (!reader.Count(count))
		{
			return false;
		}

		asset.VertexAttributes.resize(count);

		for (VertexAttributeDesc& attribute : asset.VertexAttributes)
		{
			attribute.Semantic = static_cast<VertexSemantic>(reader.U8());
			attribute.Format = static_cast<VertexElementFormat>(reader.U8());
			attribute.StreamIndex = reader.U16();
			attribute.OffsetBytes = reader.U32();
		}

		if (!reader.Count(count))
		{
			return false;
		}

		asset.Primitives.resize(count);

		for (MeshPrimitive& primitive : asset.Primitives)
		{
			primitive.FirstIndex = reader.U32();
			primitive.IndexCount = reader.U32();
			primitive.VertexOffset = reader.I32();
			primitive.MaterialSlot = reader.U32();
			ReadBounds(reader, primitive.Bounds);
		}

		if (!reader.Count(count))
		{
			return false;
		}

		asset.Lods.resize(count);

		for (MeshLod& lod : asset.Lods)
		{
			lod.FirstPrimitive = reader.U32();
			lod.PrimitiveCount = reader.U32();
			lod.ScreenCoverage = reader.F32();
		}

		if (!reader.Count(count))
		{
			return false;
		}

		asset.Meshlets.resize(count);

		for (MeshletDesc& meshlet : asset.Meshlets)
		{
			meshlet.VertexOffset = reader.U32();
			meshlet.VertexCount = reader.U32();
			meshlet.TriangleOffset = reader.U32();
			meshlet.TriangleCount = reader.U32();
		}

		asset.VertexBytes = reader.ByteVector();
		asset.IndexBytes = reader.ByteVector();
		asset.MeshletVertexBytes = reader.ByteVector();
		asset.MeshletTriangleBytes = reader.ByteVector();

		if (version == SassetMeshPayloadVersion)
		{
			if (!reader.Count(count))
			{
				return false;
			}

			asset.MorphTargets.resize(count);

			for (MeshMorphTarget& target : asset.MorphTargets)
			{
				target.Name = reader.String();

				if (!ReadFloats(reader, target.PositionDeltas) || !ReadFloats(reader, target.NormalDeltas) ||
					!ReadFloats(reader, target.TangentDeltas))
				{
					return false;
				}
			}

			if (!ReadFloats(reader, asset.DefaultMorphWeights))
			{
				return false;
			}

			// Every non-empty delta array covers the whole vertex payload (stream 0).
			const std::uint64_t vertices = asset.VertexStreams.empty() || asset.VertexStreams[0].StrideBytes == 0
				? 0
				: asset.VertexStreams[0].DataSizeBytes / asset.VertexStreams[0].StrideBytes;

			for (const MeshMorphTarget& target : asset.MorphTargets)
			{
				for (const std::vector<float>* deltas : { &target.PositionDeltas, &target.NormalDeltas, &target.TangentDeltas })
				{
					if (!deltas->empty() && deltas->size() != vertices * 3)
					{
						return false;
					}
				}
			}

			if (!asset.DefaultMorphWeights.empty() && asset.DefaultMorphWeights.size() != asset.MorphTargets.size())
			{
				return false;
			}
		}

		return reader.IsAtEnd();
	}

	bool ReadSkeleton(BinaryReader& reader, SkeletonAsset& asset)
	{
		if (reader.U32() != SassetPayloadVersion || !ReadMatrix(reader, asset.RootTransform))
		{
			return false;
		}

		std::uint32_t count = 0;

		if (!reader.Count(count))
		{
			return false;
		}

		asset.Joints.resize(count);

		for (std::uint32_t index = 0; index < count; ++index)
		{
			SkeletonJoint& joint = asset.Joints[index];
			joint.Name = reader.String();
			joint.Parent = reader.U32();
			joint.SourceNode = reader.U32();

			if (!ReadTransform(reader, joint.RestTransform) || !ReadMatrix(reader, joint.InverseBind))
			{
				return false;
			}

			// Parents first: one forward pass computes model-space poses.
			if (joint.Parent != SkeletonJoint::InvalidJoint && joint.Parent >= index)
			{
				return false;
			}
		}

		return reader.IsAtEnd();
	}

	static bool IsValidTrack(const AnimationTrack& track)
	{
		const std::uint32_t expected = track.Path == AnimationPath::Rotation ? 4u
			: track.Path == AnimationPath::MorphWeights						 ? track.Components
																			 : 3u;

		if (static_cast<std::uint8_t>(track.Path) > static_cast<std::uint8_t>(AnimationPath::MorphWeights) ||
			static_cast<std::uint8_t>(track.Interpolation) > static_cast<std::uint8_t>(AnimationInterpolation::CubicSpline) ||
			track.Components == 0 || track.Components != expected || track.Times.empty())
		{
			return false;
		}

		const std::uint64_t perKey =
			std::uint64_t(track.Components) * (track.Interpolation == AnimationInterpolation::CubicSpline ? 3u : 1u);

		if (track.Values.size() != track.Times.size() * perKey)
		{
			return false;
		}

		for (std::size_t key = 0; key < track.Times.size(); ++key)
		{
			if (!std::isfinite(track.Times[key]) || (key > 0 && !(track.Times[key] > track.Times[key - 1])))
			{
				return false;
			}
		}

		return true;
	}

	bool ReadAnimationClip(BinaryReader& reader, AnimationClipAsset& asset)
	{
		if (reader.U32() != SassetPayloadVersion)
		{
			return false;
		}

		asset.Name = reader.String();
		asset.Duration = reader.F32();
		std::uint32_t count = 0;

		if (!reader.Count(count) || !std::isfinite(asset.Duration) || asset.Duration < 0.0f)
		{
			return false;
		}

		asset.Tracks.resize(count);

		for (AnimationTrack& track : asset.Tracks)
		{
			track.Target = reader.String();
			track.Path = static_cast<AnimationPath>(reader.U8());
			track.Interpolation = static_cast<AnimationInterpolation>(reader.U8());
			track.Components = reader.U32();

			if (!ReadFloats(reader, track.Times) || !ReadFloats(reader, track.Values) || !IsValidTrack(track))
			{
				return false;
			}
		}

		if (!reader.Count(count))
		{
			return false;
		}

		asset.Events.resize(count);

		for (std::size_t index = 0; index < asset.Events.size(); ++index)
		{
			asset.Events[index].Time = reader.F32();
			asset.Events[index].Name = reader.String();

			if (!std::isfinite(asset.Events[index].Time) || (index > 0 && asset.Events[index].Time < asset.Events[index - 1].Time))
			{
				return false;
			}
		}

		return reader.IsAtEnd();
	}

	bool ReadTexture(BinaryReader& reader, TextureAsset& asset)
	{
		if (reader.U32() != SassetPayloadVersion)
		{
			return false;
		}

		asset.Dimension = static_cast<TextureDimension>(reader.U8());
		asset.ColorSpace = static_cast<TextureColorSpace>(reader.U8());
		asset.Semantic = static_cast<TextureSemantic>(reader.U8());
		asset.Width = reader.U32();
		asset.Height = reader.U32();
		asset.Depth = reader.U32();
		asset.ArrayLayers = reader.U32();

		std::uint32_t count = 0;

		if (!reader.Count(count))
		{
			return false;
		}

		asset.Payloads.resize(count);

		for (TexturePayloadVariant& payload : asset.Payloads)
		{
			payload.Container = static_cast<TextureContainerFormat>(reader.U8());
			payload.Format = static_cast<TexturePayloadFormat>(reader.U8());
			payload.Supercompression = static_cast<TextureSupercompression>(reader.U8());
			payload.ContainerFormatCode = reader.U32();

			std::uint32_t mipCount = 0;

			if (!reader.Count(mipCount))
			{
				return false;
			}

			payload.Mips.resize(mipCount);

			for (TextureMipDesc& mip : payload.Mips)
			{
				mip.Width = reader.U32();
				mip.Height = reader.U32();
				mip.Depth = reader.U32();
				mip.OffsetBytes = reader.U64();
				mip.SizeBytes = reader.U64();
				mip.UncompressedSizeBytes = reader.U64();
			}

			payload.Bytes = reader.ByteVector();
		}

		return reader.IsAtEnd();
	}

	bool ReadSampler(BinaryReader& reader, SamplerAsset& asset)
	{
		if (reader.U32() != SassetPayloadVersion)
		{
			return false;
		}

		asset.MinFilter = static_cast<SamplerFilter>(reader.U8());
		asset.MagFilter = static_cast<SamplerFilter>(reader.U8());
		asset.MipFilter = static_cast<SamplerFilter>(reader.U8());
		asset.AddressU = static_cast<SamplerAddressMode>(reader.U8());
		asset.AddressV = static_cast<SamplerAddressMode>(reader.U8());
		asset.AddressW = static_cast<SamplerAddressMode>(reader.U8());
		asset.MaxAnisotropy = reader.F32();
		return reader.IsAtEnd();
	}

	bool ReadMaterialTemplate(BinaryReader& reader, MaterialTemplateAsset& asset)
	{
		if (reader.U32() != SassetPayloadVersion)
		{
			return false;
		}

		asset.ShaderFamily = reader.String();
		asset.FeatureMask = reader.U64();
		std::uint32_t count = 0;

		if (!reader.Count(count))
		{
			return false;
		}

		asset.Parameters.resize(count);

		for (MaterialParameterDesc& parameter : asset.Parameters)
		{
			parameter.Name = reader.String();
			parameter.Type = static_cast<MaterialParameterType>(reader.U8());

			for (float& value : parameter.DefaultValue)
			{
				value = reader.F32();
			}
		}

		return reader.IsAtEnd();
	}

	bool ReadMaterialInstance(BinaryReader& reader, AssetSystem& assets, MaterialInstanceAsset& asset)
	{
		if (reader.U32() != SassetPayloadVersion)
		{
			return false;
		}

		asset.Template = DeclareReference<MaterialTemplateAsset>(assets, AssetId{ reader.U64() });

		std::uint32_t count = 0;

		if (!reader.Count(count))
		{
			return false;
		}

		asset.Parameters.resize(count);

		for (MaterialParameterValue& parameter : asset.Parameters)
		{
			parameter.Name = reader.String();

			for (float& value : parameter.Value)
			{
				value = reader.F32();
			}
		}

		if (!reader.Count(count))
		{
			return false;
		}

		asset.Textures.resize(count);

		for (MaterialTextureBinding& texture : asset.Textures)
		{
			texture.Name = reader.String();
			texture.Texture = DeclareReference<TextureAsset>(assets, AssetId{ reader.U64() });
			texture.Sampler = DeclareReference<SamplerAsset>(assets, AssetId{ reader.U64() });
		}

		return reader.IsAtEnd();
	}

	bool ReadModel(BinaryReader& reader, AssetSystem& assets, ModelAsset& asset)
	{
		const std::uint32_t version = reader.U32();

		if (version != SassetPayloadVersion && version != SassetModelPayloadVersion)
		{
			return false;
		}

		const bool animated = version == SassetModelPayloadVersion;
		std::uint32_t count = 0;

		if (!reader.Count(count))
		{
			return false;
		}

		asset.Nodes.resize(count);

		for (ModelNode& node : asset.Nodes)
		{
			node.Name = reader.String();
			node.Parent = reader.U32();
			ReadTransform(reader, node.LocalTransform);
			node.Mesh = DeclareReference<MeshAsset>(assets, AssetId{ reader.U64() });

			std::uint32_t materialCount = 0;

			if (!reader.Count(materialCount))
			{
				return false;
			}

			node.Materials.resize(materialCount);

			for (AssetHandle<MaterialInstanceAsset>& material : node.Materials)
			{
				material = DeclareReference<MaterialInstanceAsset>(assets, AssetId{ reader.U64() });
			}

			if (animated)
			{
				node.Skin = DeclareReference<SkeletonAsset>(assets, AssetId{ reader.U64() });

				if (!ReadFloats(reader, node.MorphWeights))
				{
					return false;
				}
			}
		}

		if (!reader.Count(count))
		{
			return false;
		}

		asset.Roots.resize(count);

		for (std::uint32_t& root : asset.Roots)
		{
			root = reader.U32();
		}

		if (animated)
		{
			if (!reader.Count(count))
			{
				return false;
			}

			asset.Skeletons.resize(count);

			for (AssetHandle<SkeletonAsset>& skeleton : asset.Skeletons)
			{
				skeleton = DeclareReference<SkeletonAsset>(assets, AssetId{ reader.U64() });
			}

			if (!reader.Count(count))
			{
				return false;
			}

			asset.Animations.resize(count);

			for (AssetHandle<AnimationClipAsset>& animation : asset.Animations)
			{
				animation = DeclareReference<AnimationClipAsset>(assets, AssetId{ reader.U64() });
			}
		}

		return reader.IsAtEnd();
	}

} // namespace Swim::Assets::SassetDetail
