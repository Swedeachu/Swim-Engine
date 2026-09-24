#include "Engine/Systems/Renderer/Temporal/TemporalReference.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Swim::Render
{
	void ValidateTemporalSettings(const TemporalSettings& settings)
	{
		if (!std::isfinite(settings.Feedback) || settings.Feedback <= 0.0f || settings.Feedback > 1.0f)
		{
			throw std::invalid_argument("TAA feedback must be in (0, 1]");
		}
		if (!std::isfinite(settings.ClipGamma) || settings.ClipGamma < 0.25f || settings.ClipGamma > 8.0f)
		{
			throw std::invalid_argument("TAA clip gamma must be in [0.25, 8]");
		}
		if (settings.JitterPhases > MaxJitterPhases)
		{
			throw std::invalid_argument("TAA jitter phases must be 0 .. MaxJitterPhases");
		}
	}
} // namespace Swim::Render

namespace Swim::Render::Temporal
{
	float Halton(std::uint32_t index, std::uint32_t base)
	{
		if (index == 0 || base < 2)
		{
			throw std::invalid_argument("Halton needs index >= 1 and base >= 2");
		}
		float result = 0.0f;
		float fraction = 1.0f;
		while (index > 0)
		{
			fraction /= float(base);
			result += fraction * float(index % base);
			index /= base;
		}
		return result;
	}

	Float2 JitterPixels(std::uint64_t frameIndex, std::uint32_t phases)
	{
		if (phases == 0)
		{
			return { 0.0f, 0.0f };
		}
		const auto index = static_cast<std::uint32_t>(frameIndex % phases) + 1u;
		return { Halton(index, 2) - 0.5f, Halton(index, 3) - 0.5f };
	}

	Float2 JitterNdc(std::uint64_t frameIndex, std::uint32_t phases, std::uint32_t width, std::uint32_t height)
	{
		if (!width || !height)
		{
			throw std::invalid_argument("TAA jitter needs a non-empty viewport");
		}
		const auto pixels = JitterPixels(frameIndex, phases);
		return { pixels[0] * 2.0f / float(width), -pixels[1] * 2.0f / float(height) };
	}

	float Luminance(const Float3& rgb)
	{
		return 0.2126f * rgb[0] + 0.7152f * rgb[1] + 0.0722f * rgb[2];
	}

	Float3 RgbToYCoCg(const Float3& rgb)
	{
		return { 0.25f * rgb[0] + 0.5f * rgb[1] + 0.25f * rgb[2], 0.5f * rgb[0] - 0.5f * rgb[2],
			-0.25f * rgb[0] + 0.5f * rgb[1] - 0.25f * rgb[2] };
	}

	Float3 YCoCgToRgb(const Float3& ycocg)
	{
		return { ycocg[0] + ycocg[1] - ycocg[2], ycocg[0] + ycocg[2], ycocg[0] - ycocg[1] - ycocg[2] };
	}

	Float3 Sanitize(const Float4& color)
	{
		Float3 result{};
		for (int c = 0; c < 3; ++c)
		{
			result[c] = std::isfinite(color[c]) ? std::max(color[c], 0.0f) : 0.0f;
		}
		return result;
	}

	Float3 ClipToBox(const Float3& value, const Float3& minimum, const Float3& maximum)
	{
		Float3 center{}, offset{};
		float scale = 0.0f;
		for (int c = 0; c < 3; ++c)
		{
			center[c] = 0.5f * (maximum[c] + minimum[c]);
			const float extent = std::max(0.5f * (maximum[c] - minimum[c]), 1.0e-6f);
			offset[c] = value[c] - center[c];
			scale = std::max(scale, std::abs(offset[c]) / extent);
		}
		if (scale <= 1.0f)
		{
			return value;
		}
		return { center[0] + offset[0] / scale, center[1] + offset[1] / scale, center[2] + offset[2] / scale };
	}

	Float4 SampleBilinear(const ColorImage& image, const Float2& uv)
	{
		const float px = uv[0] * float(image.Width) - 0.5f;
		const float py = uv[1] * float(image.Height) - 0.5f;
		const float fx0 = std::floor(px);
		const float fy0 = std::floor(py);
		const float fx = px - fx0;
		const float fy = py - fy0;
		const auto clampX = [&](float v)
		{
			return static_cast<std::uint32_t>(std::clamp(v, 0.0f, float(image.Width - 1)));
		};
		const auto clampY = [&](float v)
		{
			return static_cast<std::uint32_t>(std::clamp(v, 0.0f, float(image.Height - 1)));
		};
		const std::uint32_t x0 = clampX(fx0), x1 = clampX(fx0 + 1.0f);
		const std::uint32_t y0 = clampY(fy0), y1 = clampY(fy0 + 1.0f);
		Float4 result{};
		for (int c = 0; c < 4; ++c)
		{
			const float top = image.At(x0, y0)[c] + (image.At(x1, y0)[c] - image.At(x0, y0)[c]) * fx;
			const float bottom = image.At(x0, y1)[c] + (image.At(x1, y1)[c] - image.At(x0, y1)[c]) * fx;
			result[c] = top + (bottom - top) * fy;
		}
		return result;
	}

	Neighborhood Gather(const ColorImage& current, const DepthImage& depth, const VelocityImage& velocity, std::uint32_t x, std::uint32_t y,
		float clipGamma)
	{
		Neighborhood result{};
		Float3 sum{ 0, 0, 0 }, sumSquares{ 0, 0, 0 };
		Float3 minimum{ INFINITY, INFINITY, INFINITY }, maximum{ -INFINITY, -INFINITY, -INFINITY };
		float nearest = -1.0f;
		for (int dy = -1; dy <= 1; ++dy)
		{
			for (int dx = -1; dx <= 1; ++dx)
			{
				const auto sx = static_cast<std::uint32_t>(std::clamp(int(x) + dx, 0, int(current.Width) - 1));
				const auto sy = static_cast<std::uint32_t>(std::clamp(int(y) + dy, 0, int(current.Height) - 1));
				const auto rgb = Sanitize(current.At(sx, sy));
				const auto ycocg = RgbToYCoCg(rgb);
				for (int c = 0; c < 3; ++c)
				{
					sum[c] += ycocg[c];
					sumSquares[c] += ycocg[c] * ycocg[c];
					minimum[c] = std::min(minimum[c], ycocg[c]);
					maximum[c] = std::max(maximum[c], ycocg[c]);
				}
				const float d = depth.At(sx, sy);
				if (d > nearest) // Reverse-Z: larger is nearer.
				{
					nearest = d;
					result.Velocity = velocity.At(sx, sy);
				}
				if (dx == 0 && dy == 0)
				{
					result.Center = rgb;
				}
			}
		}
		for (int c = 0; c < 3; ++c)
		{
			const float mean = sum[c] / 9.0f;
			const float sigma = std::sqrt(std::max(sumSquares[c] / 9.0f - mean * mean, 0.0f));
			result.Minimum[c] = std::max(mean - clipGamma * sigma, minimum[c]);
			result.Maximum[c] = std::min(mean + clipGamma * sigma, maximum[c]);
		}
		return result;
	}

	Float4 ResolveTexel(const ColorImage& current, const DepthImage& depth, const VelocityImage& velocity, const ColorImage* history,
		const TemporalSettings& settings, std::uint32_t x, std::uint32_t y)
	{
		const auto n = Gather(current, depth, velocity, x, y, settings.ClipGamma);
		const Float2 uv{ (float(x) + 0.5f) / float(current.Width), (float(y) + 0.5f) / float(current.Height) };
		const Float2 previous{ uv[0] - n.Velocity[0], uv[1] - n.Velocity[1] };
		const bool onScreen = previous[0] >= 0.0f && previous[0] <= 1.0f && previous[1] >= 0.0f && previous[1] <= 1.0f;
		if (!history || !onScreen)
		{
			return { n.Center[0], n.Center[1], n.Center[2], 1.0f };
		}
		const auto clipped = YCoCgToRgb(ClipToBox(RgbToYCoCg(Sanitize(SampleBilinear(*history, previous))), n.Minimum, n.Maximum));
		const Float3 past{ std::max(clipped[0], 0.0f), std::max(clipped[1], 0.0f), std::max(clipped[2], 0.0f) };
		const float currentWeight = settings.Feedback / (1.0f + Luminance(n.Center));
		const float pastWeight = (1.0f - settings.Feedback) / (1.0f + Luminance(past));
		const float inverseTotal = 1.0f / (currentWeight + pastWeight);
		Float4 result{ 0, 0, 0, 1.0f };
		for (int c = 0; c < 3; ++c)
		{
			result[c] = (n.Center[c] * currentWeight + past[c] * pastWeight) * inverseTotal;
		}
		return result;
	}

	ColorImage Resolve(const ColorImage& current, const DepthImage& depth, const VelocityImage& velocity, const ColorImage* history,
		const TemporalSettings& settings)
	{
		ValidateTemporalSettings(settings);
		const auto same = [&](std::uint32_t width, std::uint32_t height)
		{
			return width == current.Width && height == current.Height;
		};
		if (!current.Width || !current.Height || !same(depth.Width, depth.Height) || !same(velocity.Width, velocity.Height) ||
			(history && !same(history->Width, history->Height)))
		{
			throw std::invalid_argument("TAA inputs must be non-empty and the same size");
		}
		ColorImage output(current.Width, current.Height);
		for (std::uint32_t y = 0; y < current.Height; ++y)
		{
			for (std::uint32_t x = 0; x < current.Width; ++x)
			{
				output.At(x, y) = ResolveTexel(current, depth, velocity, history, settings, x, y);
			}
		}
		return output;
	}
} // namespace Swim::Render::Temporal
