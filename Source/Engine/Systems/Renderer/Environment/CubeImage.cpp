#include "Engine/Systems/Renderer/Environment/CubeImage.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>

namespace Swim::Render::Environment
{
	bool IsPowerOfTwo(std::uint32_t value)
	{
		return value != 0 && (value & (value - 1)) == 0;
	}

	std::uint32_t FullCubeMipCount(std::uint32_t size)
	{
		return static_cast<std::uint32_t>(std::bit_width(size));
	}

	std::uint32_t EnvironmentSourceMipCount(std::uint32_t size)
	{
		return FullCubeMipCount(size) > 2 ? FullCubeMipCount(size) - 2 : 1;
	}

	CubeImage::CubeImage(std::uint32_t sizeInput, std::uint32_t mipCountInput) : size(sizeInput)
	{
		if (!IsPowerOfTwo(size))
		{
			throw std::invalid_argument("CubeImage size must be a power of two");
		}
		mipCount = mipCountInput == 0 ? FullCubeMipCount(size) : mipCountInput;
		if (mipCount > FullCubeMipCount(size))
		{
			throw std::invalid_argument("CubeImage mip count exceeds the chain");
		}
		std::size_t offset = 0;
		for (std::uint32_t mip = 0; mip < mipCount; ++mip)
		{
			mipOffsets.push_back(offset);
			const std::size_t mipSize = GetMipSize(mip);
			offset += mipSize * mipSize * CubeFaceCount;
		}
		texels.assign(offset, Float4{ 0, 0, 0, 0 });
	}

	std::uint32_t CubeImage::GetMipSize(std::uint32_t mip) const
	{
		return std::max(size >> mip, 1u);
	}

	Float4& CubeImage::Texel(std::uint32_t mip, std::uint32_t face, std::uint32_t x, std::uint32_t y)
	{
		const std::size_t mipSize = GetMipSize(mip);
		return texels.at(mipOffsets.at(mip) + (face * mipSize + y) * mipSize + x);
	}

	const Float4& CubeImage::Texel(std::uint32_t mip, std::uint32_t face, std::uint32_t x, std::uint32_t y) const
	{
		const std::size_t mipSize = GetMipSize(mip);
		return texels.at(mipOffsets.at(mip) + (face * mipSize + y) * mipSize + x);
	}

	std::span<Float4> CubeImage::Face(std::uint32_t mip, std::uint32_t face)
	{
		const std::size_t mipSize = GetMipSize(mip);
		return std::span(texels).subspan(mipOffsets.at(mip) + face * mipSize * mipSize, mipSize * mipSize);
	}

	std::span<const Float4> CubeImage::Face(std::uint32_t mip, std::uint32_t face) const
	{
		const std::size_t mipSize = GetMipSize(mip);
		return std::span(texels).subspan(mipOffsets.at(mip) + face * mipSize * mipSize, mipSize * mipSize);
	}

	Float4 CubeImage::SampleNearest(const Float3& direction, float lod) const
	{
		const auto mip = static_cast<std::uint32_t>(std::clamp(std::ceil(std::max(lod, 0.0f) + 0.5f) - 1.0f, 0.0f, float(mipCount - 1)));
		const auto coordinate = DirectionToCube(direction);
		const std::uint32_t mipSize = GetMipSize(mip);
		const auto texel = [&](float value)
		{
			return static_cast<std::uint32_t>(std::clamp(std::floor((value + 1.0f) * 0.5f * float(mipSize)), 0.0f, float(mipSize - 1)));
		};
		return Texel(mip, coordinate.Face, texel(coordinate.S), texel(coordinate.T));
	}

	Float4 CubeImage::FetchAcrossEdge(std::uint32_t mip, std::uint32_t face, int x, int y) const
	{
		// The texel center just beyond the face edge, projected onto the adjacent face.
		const std::uint32_t mipSize = GetMipSize(mip);
		const float s = 2.0f * (float(x) + 0.5f) / float(mipSize) - 1.0f;
		const float t = 2.0f * (float(y) + 0.5f) / float(mipSize) - 1.0f;
		const auto coordinate = DirectionToCube(CubeFaceDirection(face, s, t));
		const auto texel = [&](float value)
		{
			return static_cast<std::uint32_t>(std::clamp(std::floor((value + 1.0f) * 0.5f * float(mipSize)), 0.0f, float(mipSize - 1)));
		};
		return Texel(mip, coordinate.Face, texel(coordinate.S), texel(coordinate.T));
	}

	Float4 CubeImage::FetchSeamless(std::uint32_t mip, std::uint32_t face, int x, int y) const
	{
		const int mipSize = static_cast<int>(GetMipSize(mip));
		const bool insideX = x >= 0 && x < mipSize;
		const bool insideY = y >= 0 && y < mipSize;
		if (insideX && insideY)
		{
			return Texel(mip, face, std::uint32_t(x), std::uint32_t(y));
		}
		if (insideX || insideY)
		{
			return FetchAcrossEdge(mip, face, x, y);
		}
		// Corner: the average of the three texels that meet there.
		const int cx = std::clamp(x, 0, mipSize - 1);
		const int cy = std::clamp(y, 0, mipSize - 1);
		const auto a = Texel(mip, face, std::uint32_t(cx), std::uint32_t(cy));
		const auto b = FetchAcrossEdge(mip, face, x, cy);
		const auto c = FetchAcrossEdge(mip, face, cx, y);
		Float4 result{};
		for (int channel = 0; channel < 4; ++channel)
		{
			result[channel] = (a[channel] + b[channel] + c[channel]) / 3.0f;
		}
		return result;
	}

	Float4 CubeImage::SampleBilinear(std::uint32_t mip, const CubeCoordinate& coordinate) const
	{
		const std::uint32_t mipSize = GetMipSize(mip);
		const float u = (coordinate.S + 1.0f) * 0.5f * float(mipSize) - 0.5f;
		const float v = (coordinate.T + 1.0f) * 0.5f * float(mipSize) - 0.5f;
		const float fu = std::floor(u);
		const float fv = std::floor(v);
		const float wu = u - fu;
		const float wv = v - fv;
		const int x0 = static_cast<int>(fu);
		const int y0 = static_cast<int>(fv);
		const auto t00 = FetchSeamless(mip, coordinate.Face, x0, y0);
		const auto t10 = FetchSeamless(mip, coordinate.Face, x0 + 1, y0);
		const auto t01 = FetchSeamless(mip, coordinate.Face, x0, y0 + 1);
		const auto t11 = FetchSeamless(mip, coordinate.Face, x0 + 1, y0 + 1);
		Float4 result{};
		for (int c = 0; c < 4; ++c)
		{
			const float top = t00[c] * (1.0f - wu) + t10[c] * wu;
			const float bottom = t01[c] * (1.0f - wu) + t11[c] * wu;
			result[c] = top * (1.0f - wv) + bottom * wv;
		}
		return result;
	}

	Float4 CubeImage::SampleTrilinear(const Float3& direction, float lod) const
	{
		const float clamped = std::clamp(lod, 0.0f, float(mipCount - 1));
		const auto coordinate = DirectionToCube(direction);
		const auto low = static_cast<std::uint32_t>(std::floor(clamped));
		const auto high = std::min(low + 1, mipCount - 1);
		const float weight = clamped - float(low);
		const auto a = SampleBilinear(low, coordinate);
		if (weight == 0.0f || high == low)
		{
			return a;
		}
		const auto b = SampleBilinear(high, coordinate);
		Float4 result{};
		for (int c = 0; c < 4; ++c)
		{
			result[c] = a[c] * (1.0f - weight) + b[c] * weight;
		}
		return result;
	}

	void CubeImage::GenerateMips()
	{
		for (std::uint32_t mip = 1; mip < mipCount; ++mip)
		{
			const std::uint32_t mipSize = GetMipSize(mip);
			for (std::uint32_t face = 0; face < CubeFaceCount; ++face)
			{
				for (std::uint32_t y = 0; y < mipSize; ++y)
				{
					for (std::uint32_t x = 0; x < mipSize; ++x)
					{
						Float4 sum{};
						for (std::uint32_t dy = 0; dy < 2; ++dy)
						{
							for (std::uint32_t dx = 0; dx < 2; ++dx)
							{
								const auto& source = Texel(mip - 1, face, x * 2 + dx, y * 2 + dy);
								for (int c = 0; c < 4; ++c)
								{
									sum[c] += source[c];
								}
							}
						}
						for (auto& value : sum)
						{
							value *= 0.25f;
						}
						Texel(mip, face, x, y) = sum;
					}
				}
			}
		}
	}

	Float4 Image2D::SampleBilinear(float uInput, float vInput) const
	{
		const float u = uInput * float(Width) - 0.5f;
		const float v = vInput * float(Height) - 0.5f;
		const float fu = std::floor(u);
		const float fv = std::floor(v);
		const float wu = u - fu;
		const float wv = v - fv;
		const auto clampX = [&](float value)
		{
			return static_cast<std::uint32_t>(std::clamp(value, 0.0f, float(Width - 1)));
		};
		const auto clampY = [&](float value)
		{
			return static_cast<std::uint32_t>(std::clamp(value, 0.0f, float(Height - 1)));
		};
		const auto x0 = clampX(fu);
		const auto x1 = clampX(fu + 1.0f);
		const auto y0 = clampY(fv);
		const auto y1 = clampY(fv + 1.0f);
		Float4 result{};
		for (int c = 0; c < 4; ++c)
		{
			const float top = At(x0, y0)[c] * (1.0f - wu) + At(x1, y0)[c] * wu;
			const float bottom = At(x0, y1)[c] * (1.0f - wu) + At(x1, y1)[c] * wu;
			result[c] = top * (1.0f - wv) + bottom * wv;
		}
		return result;
	}
} // namespace Swim::Render::Environment
