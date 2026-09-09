#pragma once

#include "Engine/Systems/Renderer/RHI/RhiContracts.h"

#include <array>
#include <bit>

namespace Swim::Testing
{

	struct SampledDimensionData
	{
		Rhi::TextureDesc Texture;
		Rhi::TextureViewDesc View;
		Rhi::Extent3D CopyExtent;
	};

	inline SampledDimensionData MakeSampledDimensionData(std::uint32_t index)
	{
		using D = Rhi::TextureViewDimension;
		const std::array dimensions{ D::Texture1D, D::Texture1DArray, D::Texture2DArray,
			D::Texture3D, D::TextureCube, D::Texture2D, D::TextureCubeArray };
		SampledDimensionData data{};
		const bool cube = index == 4 || index == 6;
		data.Texture.Dimension = index < 2 ? Rhi::TextureDimension::Texture1D : index == 3 ?
			Rhi::TextureDimension::Texture3D : cube ? Rhi::TextureDimension::TextureCube : Rhi::TextureDimension::Texture2D;
		data.Texture.PixelFormat = cube ? Rhi::Format::R32Float : Rhi::Format::R32Uint;
		data.Texture.Extent = { 8, index < 2 ? 1u : 8u, index == 3 ? 8u : 1u };
		data.Texture.MipLevels = 2;
		data.Texture.ArrayLayers = index == 3 ? 1 : cube ? 18 : 3;
		data.Texture.Usage = Rhi::TextureUsage::Sampled | Rhi::TextureUsage::TransferDestination;
		data.View.Dimension = dimensions[index];
		data.View.PixelFormat = data.Texture.PixelFormat;
		data.View.BaseMipLevel = 1;
		data.View.BaseArrayLayer = index == 3 ? 0 : cube ? 6 : 1;
		data.View.ArrayLayerCount = index == 6 ? 12 : index == 4 ? 6 : index == 1 || index == 2 ? 2 : 1;
		data.CopyExtent = { 4, index < 2 ? 1u : 4u, index == 3 ? 4u : 1u };
		return data;
	}

	inline std::uint32_t SampledDimensionValue(std::uint32_t frame, std::uint32_t image,
		std::uint32_t layer, std::uint32_t x, std::uint32_t y, std::uint32_t z)
	{
		// Cubes contain a distinct constant per face, independent of face orientation.
		const auto texel = image == 4 || image == 6 ? 0 : z * 16 + y * 4 + x;
		return frame * 10000 + image * 1000 + layer * 100 + texel;
	}

	inline std::array<std::uint32_t, 256> SampledDimensionPixels(std::uint32_t frame, std::uint32_t image)
	{
		std::array<std::uint32_t, 256> pixels{};
		const auto data = MakeSampledDimensionData(image);
		std::size_t offset = 0;
		for (std::uint32_t layer = 0; layer < data.View.ArrayLayerCount; ++layer)
		{
			for (std::uint32_t z = 0; z < data.CopyExtent.Depth; ++z)
			{
				for (std::uint32_t y = 0; y < data.CopyExtent.Height; ++y)
				{
					for (std::uint32_t x = 0; x < data.CopyExtent.Width; ++x)
					{
						const auto value = SampledDimensionValue(frame, image, layer, x, y, z);
						pixels[offset++] = image == 4 || image == 6 ? std::bit_cast<std::uint32_t>(static_cast<float>(value)) : value;
					}
				}
			}
		}
		return pixels;
	}

} // namespace Swim::Testing
