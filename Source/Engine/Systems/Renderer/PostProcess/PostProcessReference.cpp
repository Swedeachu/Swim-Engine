#include "Engine/Systems/Renderer/PostProcess/PostProcessReference.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace Swim::Render
{
	void ValidatePostProcessSettings(const PostProcessSettings& settings)
	{
		const auto finite = [](std::initializer_list<float> values)
		{
			return std::all_of(values.begin(), values.end(),
				[](float v)
				{
					return std::isfinite(v);
				});
		};
		const auto& e = settings.Exposure;
		const auto& b = settings.Bloom;
		const auto& g = settings.Grading;
		const auto& t = settings.ToneMap;
		const auto& o = settings.Output;
		const bool exposure = finite({ e.ManualEv100, e.Compensation, e.MinEv100, e.MaxEv100, e.MinLog2Luminance, e.MaxLog2Luminance,
								  e.LowPercentile, e.HighPercentile, e.SpeedUp, e.SpeedDown }) &&
			(e.Mode == ExposureMode::Manual || e.Mode == ExposureMode::Automatic) && e.MinEv100 <= e.MaxEv100 &&
			e.MinLog2Luminance < e.MaxLog2Luminance && e.LowPercentile >= 0.0f && e.LowPercentile < e.HighPercentile &&
			e.HighPercentile <= 1.0f && e.SpeedUp > 0.0f && e.SpeedDown > 0.0f;
		const bool bloom = finite({ b.Threshold, b.Knee, b.Intensity }) && b.MipCount >= 1 && b.MipCount <= MaxBloomMips &&
			b.Threshold >= 0.0f && b.Knee > 0.0f && b.Intensity >= 0.0f;
		const bool grading = finite({ g.Temperature, g.Tint, g.Contrast, g.Saturation, g.Slope[0], g.Slope[1], g.Slope[2], g.Offset[0],
								 g.Offset[1], g.Offset[2], g.Power[0], g.Power[1], g.Power[2] }) &&
			std::abs(g.Temperature) <= 100.0f && std::abs(g.Tint) <= 100.0f && g.Contrast > 0.0f && g.Saturation >= 0.0f &&
			g.Power[0] > 0.0f && g.Power[1] > 0.0f && g.Power[2] > 0.0f;
		const bool tone = std::isfinite(t.WhitePoint) && t.WhitePoint >= 1.0f && static_cast<std::uint32_t>(t.Operator) <= 3u;
		const bool output = finite({ o.PaperWhiteNits, o.PeakNits }) && static_cast<std::uint32_t>(o.Encoding) <= 2u &&
			o.PaperWhiteNits > 0.0f && o.PeakNits >= o.PaperWhiteNits && o.PeakNits <= 10000.0f;
		if (!exposure || !bloom || !grading || !tone || !output)
		{
			throw std::invalid_argument("Post-process settings are out of range (see PostProcessSettings.h)");
		}
	}
} // namespace Swim::Render

namespace Swim::Render::Post
{
	namespace
	{
		constexpr Float3 LuminanceWeights{ 0.2126f, 0.7152f, 0.0722f };

		float Saturate(float v)
		{
			return std::clamp(v, 0.0f, 1.0f);
		}

		Float3 Multiply(const Matrix3& m, const Float3& v)
		{
			return { m[0] * v[0] + m[1] * v[1] + m[2] * v[2], m[3] * v[0] + m[4] * v[1] + m[5] * v[2],
				m[6] * v[0] + m[7] * v[1] + m[8] * v[2] };
		}

		Matrix3 Multiply(const Matrix3& a, const Matrix3& b)
		{
			Matrix3 r{};
			for (int i = 0; i < 3; ++i)
			{
				for (int j = 0; j < 3; ++j)
				{
					r[i * 3 + j] = a[i * 3] * b[j] + a[i * 3 + 1] * b[3 + j] + a[i * 3 + 2] * b[6 + j];
				}
			}
			return r;
		}

		float Fract(float v)
		{
			return v - std::floor(v);
		}

		Float3 Rgb(const Float4& v)
		{
			return { v[0], v[1], v[2] };
		}

		// Average of the 2x2 texels around the corner (cx, cy) (texels cx - 1 .. cx).
		Float3 Box(const Image& image, int cx, int cy)
		{
			const auto& a = image.Clamped(cx - 1, cy - 1);
			const auto& b = image.Clamped(cx, cy - 1);
			const auto& c = image.Clamped(cx - 1, cy);
			const auto& d = image.Clamped(cx, cy);
			return { (a[0] + b[0] + c[0] + d[0]) * 0.25f, (a[1] + b[1] + c[1] + d[1]) * 0.25f, (a[2] + b[2] + c[2] + d[2]) * 0.25f };
		}

		Float3 Average4(const Float3& a, const Float3& b, const Float3& c, const Float3& d)
		{
			return { (a[0] + b[0] + c[0] + d[0]) * 0.25f, (a[1] + b[1] + c[1] + d[1]) * 0.25f, (a[2] + b[2] + c[2] + d[2]) * 0.25f };
		}
	} // namespace

	const Float4& Image::Clamped(int x, int y) const
	{
		const int cx = std::clamp(x, 0, int(Width) - 1);
		const int cy = std::clamp(y, 0, int(Height) - 1);
		return Texels[std::size_t(cy) * Width + std::size_t(cx)];
	}

	float RoundToHalf(float value)
	{
		if (std::isnan(value))
		{
			return value;
		}
		const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
		const std::uint32_t sign = bits & 0x80000000u;
		std::uint32_t magnitude = bits & 0x7fffffffu;
		std::uint32_t half = 0;
		if (magnitude >= 0x477ff000u) // >= 65520 rounds to infinity.
		{
			half = 0x7c00u;
		}
		else if (magnitude < 0x38800000u) // Below the smallest normal half: subnormal or zero.
		{
			const float absolute = std::bit_cast<float>(magnitude);
			// Subnormal halves are multiples of 2^-24; nearbyint rounds to even.
			half = static_cast<std::uint32_t>(std::nearbyint(absolute * 16777216.0f));
		}
		else
		{
			const std::uint32_t exponent = (magnitude >> 23) - 127u + 15u;
			std::uint32_t mantissa = magnitude & 0x7fffffu;
			half = (exponent << 10) | (mantissa >> 13);
			const std::uint32_t rest = mantissa & 0x1fffu;
			if (rest > 0x1000u || (rest == 0x1000u && (half & 1u)))
			{
				++half; // May carry into the exponent (and up to infinity), which is correct.
			}
		}
		// Back to float.
		const std::uint32_t e = (half >> 10) & 0x1fu;
		const std::uint32_t m = half & 0x3ffu;
		float result = 0.0f;
		if (e == 0)
		{
			result = float(m) / 16777216.0f;
		}
		else if (e == 31)
		{
			result = std::numeric_limits<float>::infinity();
		}
		else
		{
			result = std::bit_cast<float>(((e - 15u + 127u) << 23) | (m << 13));
		}
		return sign ? -result : result;
	}

	Float4 RoundToHalf(const Float4& value)
	{
		return { RoundToHalf(value[0]), RoundToHalf(value[1]), RoundToHalf(value[2]), RoundToHalf(value[3]) };
	}

	float Luminance(const Float3& rgb)
	{
		return rgb[0] * LuminanceWeights[0] + rgb[1] * LuminanceWeights[1] + rgb[2] * LuminanceWeights[2];
	}

	std::uint32_t HistogramBin(float luminance, float minLog2, float inverseLog2Range)
	{
		const float l = std::log2(luminance);
		if (!(l >= minLog2))
		{
			return 0;
		}
		const float t = Saturate((l - minLog2) * inverseLog2Range);
		return std::min(static_cast<std::uint32_t>(t * 254.0f) + 1u, PostHistogramBins - 1);
	}

	Histogram BuildHistogram(const Image& source, const ExposureSettings& settings)
	{
		Histogram bins{};
		const float inverse = 1.0f / (settings.MaxLog2Luminance - settings.MinLog2Luminance);
		for (const auto& texel : source.Texels)
		{
			++bins[HistogramBin(Luminance(Rgb(texel)), settings.MinLog2Luminance, inverse)];
		}
		return bins;
	}

	float BinLog2Luminance(std::uint32_t bin, float minLog2, float log2Range)
	{
		return minLog2 + (float(bin - 1) + 0.5f) / 254.0f * log2Range;
	}

	bool AverageLog2Luminance(const Histogram& histogram, const ExposureSettings& settings, float& average)
	{
		const float range = settings.MaxLog2Luminance - settings.MinLog2Luminance;
		float total = 0.0f;
		for (std::uint32_t b = 1; b < PostHistogramBins; ++b)
		{
			total += float(histogram[b]);
		}
		const float low = total * settings.LowPercentile;
		const float high = total * settings.HighPercentile;
		float cumulative = 0.0f;
		float weight = 0.0f;
		float sum = 0.0f;
		for (std::uint32_t b = 1; b < PostHistogramBins; ++b)
		{
			const float count = float(histogram[b]);
			const float w = std::max(std::min(cumulative + count, high) - std::max(cumulative, low), 0.0f);
			weight += w;
			sum += w * BinLog2Luminance(b, settings.MinLog2Luminance, range);
			cumulative += count;
		}
		if (!(weight > 0.0f))
		{
			return false;
		}
		average = sum / weight;
		return true;
	}

	float Ev100FromAverageLog2(float averageLog2)
	{
		return averageLog2 + 3.0f; // log2(L * 100 / 12.5) = log2(L) + log2(8).
	}

	float ExposureFromEv100(float ev100)
	{
		return 1.0f / (1.2f * std::exp2(ev100));
	}

	GpuExposureState UpdateExposure(
		const Histogram& histogram, const ExposureSettings& settings, const GpuExposureState& previous, float deltaTime, bool reset)
	{
		const bool snap = reset || previous.Valid == 0;
		GpuExposureState state;
		float target = 0.0f;
		if (settings.Mode == ExposureMode::Manual)
		{
			state.AverageLog2Luminance = snap ? 0.0f : previous.AverageLog2Luminance;
			target = std::clamp(settings.ManualEv100 - settings.Compensation, settings.MinEv100, settings.MaxEv100);
			state.Ev100 = target;
		}
		else
		{
			float average = 0.0f;
			if (!AverageLog2Luminance(histogram, settings, average))
			{
				average = snap ? std::log2(0.18f) : previous.AverageLog2Luminance;
			}
			state.AverageLog2Luminance = average;
			target = std::clamp(Ev100FromAverageLog2(average) - settings.Compensation, settings.MinEv100, settings.MaxEv100);
			if (snap)
			{
				state.Ev100 = target;
			}
			else
			{
				const float speed = target > previous.Ev100 ? settings.SpeedUp : settings.SpeedDown;
				state.Ev100 = previous.Ev100 + (target - previous.Ev100) * (1.0f - std::exp(-deltaTime * speed));
			}
		}
		state.Exposure = ExposureFromEv100(state.Ev100);
		state.Valid = 1;
		return state;
	}

	std::uint32_t BloomLevelCount(std::uint32_t width, std::uint32_t height, std::uint32_t requested)
	{
		std::uint32_t levels = 0;
		while (levels < requested && levels < 31 && (width >> (levels + 1)) >= 1 && (height >> (levels + 1)) >= 1)
		{
			++levels;
		}
		return levels;
	}

	Float3 BloomThreshold(const Float3& color, float threshold, float knee)
	{
		const float brightness = std::max(color[0], std::max(color[1], color[2]));
		float soft = std::clamp(brightness - threshold + knee, 0.0f, 2.0f * knee);
		soft = soft * soft / (4.0f * knee + 1.0e-5f);
		const float weight = std::max(soft, brightness - threshold) / std::max(brightness, 1.0e-5f);
		return { color[0] * weight, color[1] * weight, color[2] * weight };
	}

	Image BloomDownsample(
		const Image& source, std::uint32_t width, std::uint32_t height, bool first, float exposure, float threshold, float knee)
	{
		Image result(width, height);
		for (std::uint32_t y = 0; y < height; ++y)
		{
			for (std::uint32_t x = 0; x < width; ++x)
			{
				const int cx = int(2 * x + 1);
				const int cy = int(2 * y + 1);
				const auto a = Box(source, cx - 2, cy - 2);
				const auto b = Box(source, cx, cy - 2);
				const auto c = Box(source, cx + 2, cy - 2);
				const auto d = Box(source, cx - 1, cy - 1);
				const auto e = Box(source, cx + 1, cy - 1);
				const auto f = Box(source, cx - 2, cy);
				const auto g = Box(source, cx, cy);
				const auto h = Box(source, cx + 2, cy);
				const auto i = Box(source, cx - 1, cy + 1);
				const auto j = Box(source, cx + 1, cy + 1);
				const auto k = Box(source, cx - 2, cy + 2);
				const auto l = Box(source, cx, cy + 2);
				const auto m = Box(source, cx + 2, cy + 2);
				std::array<Float3, 5> groups{ Average4(d, e, i, j), Average4(a, b, f, g), Average4(b, c, g, h), Average4(f, g, k, l),
					Average4(g, h, l, m) };
				constexpr std::array<float, 5> weights{ 0.5f, 0.125f, 0.125f, 0.125f, 0.125f };
				Float3 sum{ 0, 0, 0 };
				if (first)
				{
					float total = 0.0f;
					for (int n = 0; n < 5; ++n)
					{
						for (auto& v : groups[n])
						{
							v *= exposure;
						}
						const float w = weights[n] / (1.0f + Luminance(groups[n]));
						total += w;
						for (int ch = 0; ch < 3; ++ch)
						{
							sum[ch] += groups[n][ch] * w;
						}
					}
					for (auto& v : sum)
					{
						v /= total;
					}
					sum = BloomThreshold(sum, threshold, knee);
				}
				else
				{
					for (int n = 0; n < 5; ++n)
					{
						for (int ch = 0; ch < 3; ++ch)
						{
							sum[ch] += groups[n][ch] * weights[n];
						}
					}
				}
				result.At(x, y) = RoundToHalf(Float4{ sum[0], sum[1], sum[2], 1.0f });
			}
		}
		return result;
	}

	Float3 TentUpsample(const Image& low, std::uint32_t x, std::uint32_t y, std::uint32_t width, std::uint32_t height)
	{
		const float px = (float(x) + 0.5f) * float(low.Width) / float(width) - 0.5f;
		const float py = (float(y) + 0.5f) * float(low.Height) / float(height) - 0.5f;
		const float fx0 = std::floor(px);
		const float fy0 = std::floor(py);
		const float fx = px - fx0;
		const float fy = py - fy0;
		const std::array<float, 4> wx{ (1.0f - fx) * 0.25f, (2.0f - fx) * 0.25f, (1.0f + fx) * 0.25f, fx * 0.25f };
		const std::array<float, 4> wy{ (1.0f - fy) * 0.25f, (2.0f - fy) * 0.25f, (1.0f + fy) * 0.25f, fy * 0.25f };
		const int x0 = int(fx0) - 1;
		const int y0 = int(fy0) - 1;
		Float3 sum{ 0, 0, 0 };
		for (int j = 0; j < 4; ++j)
		{
			Float3 row{ 0, 0, 0 };
			for (int i = 0; i < 4; ++i)
			{
				const auto& t = low.Clamped(x0 + i, y0 + j);
				for (int c = 0; c < 3; ++c)
				{
					row[c] += t[c] * wx[i];
				}
			}
			for (int c = 0; c < 3; ++c)
			{
				sum[c] += row[c] * wy[j];
			}
		}
		return sum;
	}

	Image BloomUpsample(const Image& low, const Image& high)
	{
		Image result(high.Width, high.Height);
		for (std::uint32_t y = 0; y < high.Height; ++y)
		{
			for (std::uint32_t x = 0; x < high.Width; ++x)
			{
				const auto tent = TentUpsample(low, x, y, high.Width, high.Height);
				const auto& h = high.At(x, y);
				result.At(x, y) = RoundToHalf(Float4{ h[0] + tent[0], h[1] + tent[1], h[2] + tent[2], 1.0f });
			}
		}
		return result;
	}

	Matrix3 WhiteBalanceMatrix(float temperature, float tint)
	{
		if (temperature == 0.0f && tint == 0.0f)
		{
			return { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
		}
		// Linear Rec.709 <-> LMS (CAT02-based) and the CIE daylight locus, as in Unity's post stack.
		constexpr Matrix3 linearToLms{ 3.90405e-1f, 5.49941e-1f, 8.92632e-3f, 7.08416e-2f, 9.63172e-1f, 1.35775e-3f, 2.31082e-2f,
			1.28021e-1f, 9.36245e-1f };
		constexpr Matrix3 lmsToLinear{ 2.85847e+0f, -1.62879e+0f, -2.48910e-2f, -2.10182e-1f, 1.15820e+0f, 3.24281e-4f, -4.18120e-2f,
			-1.18169e-1f, 1.06867e+0f };
		const auto xyToLms = [](float x, float y)
		{
			const float bigX = x / y;
			const float bigZ = (1.0f - x - y) / y;
			return Float3{ 0.7328f * bigX + 0.4296f - 0.1624f * bigZ, -0.7036f * bigX + 1.6975f + 0.0061f * bigZ,
				0.0030f * bigX + 0.0136f + 0.9834f * bigZ };
		};
		const float t1 = temperature / 65.0f;
		const float t2 = tint / 65.0f;
		const float x = 0.31271f - t1 * (t1 < 0.0f ? 0.1f : 0.05f);
		const float y = 2.87f * x - 3.0f * x * x - 0.27509507f + t2 * 0.05f;
		const auto reference = xyToLms(0.31271f, 0.32902f); // D65.
		const auto target = xyToLms(x, y);
		const Matrix3 scale{ reference[0] / target[0], 0, 0, 0, reference[1] / target[1], 0, 0, 0, reference[2] / target[2] };
		return Multiply(lmsToLinear, Multiply(scale, linearToLms));
	}

	GpuPostParams BuildPostParams(const PostProcessSettings& settings, std::uint32_t bloomLevels)
	{
		GpuPostParams params;
		const auto& g = settings.Grading;
		const auto wb = WhiteBalanceMatrix(g.Temperature, g.Tint);
		for (int r = 0; r < 3; ++r)
		{
			for (int c = 0; c < 3; ++c)
			{
				params.WhiteBalance[r * 4 + c] = wb[r * 3 + c];
			}
			params.WhiteBalance[r * 4 + 3] = 0.0f;
			params.SlopeContrast[r] = g.Slope[r];
			params.OffsetSaturation[r] = g.Offset[r];
			params.PowerBloom[r] = g.Power[r];
		}
		params.SlopeContrast[3] = g.Contrast;
		params.OffsetSaturation[3] = g.Saturation;
		const bool bloom = settings.Bloom.Enabled && bloomLevels > 0;
		params.BloomEnabled = bloom ? 1u : 0u;
		params.PowerBloom[3] = bloom ? settings.Bloom.Intensity / float(bloomLevels) : 0.0f;
		params.ToneMapper = static_cast<std::uint32_t>(settings.ToneMap.Operator);
		params.Encoding = static_cast<std::uint32_t>(settings.Output.Encoding);
		params.Dither = settings.Output.Dither && settings.Output.Encoding == OutputEncoding::Srgb ? 1u : 0u;
		params.WhitePoint = settings.ToneMap.WhitePoint;
		params.PaperWhiteNits = settings.Output.PaperWhiteNits;
		params.PeakNits = settings.Output.PeakNits;
		const bool identity = g.Temperature == 0.0f && g.Tint == 0.0f && g.Contrast == 1.0f && g.Saturation == 1.0f &&
			g.Slope == std::array<float, 3>{ 1, 1, 1 } && g.Offset == std::array<float, 3>{ 0, 0, 0 } &&
			g.Power == std::array<float, 3>{ 1, 1, 1 };
		params.GradingEnabled = identity ? 0u : 1u;
		return params;
	}

	Float3 Grade(const GpuPostParams& p, const Float3& color)
	{
		if (p.GradingEnabled == 0)
		{
			return color;
		}
		Float3 c{};
		for (int r = 0; r < 3; ++r)
		{
			c[r] = p.WhiteBalance[r * 4] * color[0] + p.WhiteBalance[r * 4 + 1] * color[1] + p.WhiteBalance[r * 4 + 2] * color[2];
		}
		for (int i = 0; i < 3; ++i)
		{
			c[i] = 0.18f * std::pow(std::max(c[i], 0.0f) / 0.18f, p.SlopeContrast[3]);
			c[i] = std::pow(std::max(c[i] * p.SlopeContrast[i] + p.OffsetSaturation[i], 0.0f), p.PowerBloom[i]);
		}
		const float l = Luminance(c);
		for (int i = 0; i < 3; ++i)
		{
			c[i] = std::max(l + (c[i] - l) * p.OffsetSaturation[3], 0.0f);
		}
		return c;
	}

	Float3 ToneMapReinhard(const Float3& color, float whitePoint)
	{
		Float3 r{};
		const float w2 = whitePoint * whitePoint;
		for (int i = 0; i < 3; ++i)
		{
			const float x = std::max(color[i], 0.0f);
			r[i] = Saturate(x * (1.0f + x / w2) / (1.0f + x));
		}
		return r;
	}

	Float3 ToneMapAces(const Float3& color)
	{
		constexpr Matrix3 input{ 0.59719f, 0.35458f, 0.04823f, 0.07600f, 0.90834f, 0.01566f, 0.02840f, 0.13383f, 0.83777f };
		constexpr Matrix3 output{ 1.60475f, -0.53108f, -0.07367f, -0.10208f, 1.10813f, -0.00605f, -0.00327f, -0.07276f, 1.07602f };
		auto v = Multiply(input, Float3{ std::max(color[0], 0.0f), std::max(color[1], 0.0f), std::max(color[2], 0.0f) });
		for (auto& x : v)
		{
			const float a = x * (x + 0.0245786f) - 0.000090537f;
			const float b = x * (0.983729f * x + 0.4329510f) + 0.238081f;
			x = a / b;
		}
		v = Multiply(output, v);
		return { Saturate(v[0]), Saturate(v[1]), Saturate(v[2]) };
	}

	Float3 ToneMapPbrNeutral(const Float3& color)
	{
		constexpr float startCompression = 0.8f - 0.04f;
		constexpr float desaturation = 0.15f;
		Float3 c{ std::max(color[0], 0.0f), std::max(color[1], 0.0f), std::max(color[2], 0.0f) };
		const float x = std::min(c[0], std::min(c[1], c[2]));
		const float offset = x < 0.08f ? x - 6.25f * x * x : 0.04f;
		for (auto& v : c)
		{
			v -= offset;
		}
		const float peak = std::max(c[0], std::max(c[1], c[2]));
		if (peak < startCompression)
		{
			return c;
		}
		constexpr float d = 1.0f - startCompression;
		const float newPeak = 1.0f - d * d / (peak + d - startCompression);
		for (auto& v : c)
		{
			v *= newPeak / peak;
		}
		const float g = 1.0f - 1.0f / (desaturation * (peak - newPeak) + 1.0f);
		for (auto& v : c)
		{
			v = v * (1.0f - g) + newPeak * g;
		}
		return c;
	}

	Float3 ToneMap(ToneMapper op, const Float3& color, float whitePoint)
	{
		switch (op)
		{
		case ToneMapper::Reinhard:
			return ToneMapReinhard(color, whitePoint);
		case ToneMapper::Aces:
			return ToneMapAces(color);
		case ToneMapper::PbrNeutral:
			return ToneMapPbrNeutral(color);
		default:
			return { Saturate(color[0]), Saturate(color[1]), Saturate(color[2]) };
		}
	}

	float SrgbOetf(float linear)
	{
		return linear <= 0.0031308f ? 12.92f * linear : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
	}

	float SrgbEotf(float encoded)
	{
		return encoded <= 0.04045f ? encoded / 12.92f : std::pow((encoded + 0.055f) / 1.055f, 2.4f);
	}

	float PqOetf(float nits)
	{
		constexpr float m1 = 0.1593017578125f, m2 = 78.84375f, c1 = 0.8359375f, c2 = 18.8515625f, c3 = 18.6875f;
		const float y = std::pow(std::clamp(nits / 10000.0f, 0.0f, 1.0f), m1);
		return std::pow((c1 + c2 * y) / (1.0f + c3 * y), m2);
	}

	float PqEotf(float encoded)
	{
		constexpr float m1 = 0.1593017578125f, m2 = 78.84375f, c1 = 0.8359375f, c2 = 18.8515625f, c3 = 18.6875f;
		const float e = std::pow(std::clamp(encoded, 0.0f, 1.0f), 1.0f / m2);
		return 10000.0f * std::pow(std::max(e - c1, 0.0f) / (c2 - c3 * e), 1.0f / m1);
	}

	Float3 Rec709ToRec2020(const Float3& color)
	{
		constexpr Matrix3 m{ 0.6274040f, 0.3292820f, 0.0433136f, 0.0690970f, 0.9195400f, 0.0113612f, 0.0163916f, 0.0880132f, 0.8955950f };
		return Multiply(m, color);
	}

	float InterleavedGradientNoise(std::uint32_t x, std::uint32_t y)
	{
		return Fract(52.9829189f * Fract(0.06711056f * float(x) + 0.00583715f * float(y)));
	}

	Float4 CompositeTexel(
		const GpuPostParams& p, float exposure, const Float4& source, const Float3& bloom, std::uint32_t x, std::uint32_t y)
	{
		Float3 c{ source[0] * exposure, source[1] * exposure, source[2] * exposure };
		if (p.BloomEnabled != 0)
		{
			for (int i = 0; i < 3; ++i)
			{
				c[i] += bloom[i] * p.PowerBloom[3];
			}
		}
		c = Grade(p, c);
		const auto op = static_cast<ToneMapper>(p.ToneMapper);
		if (p.Encoding == static_cast<std::uint32_t>(OutputEncoding::Srgb))
		{
			const auto t = ToneMap(op, c, p.WhitePoint);
			const float noise = p.Dither != 0 ? (InterleavedGradientNoise(x, y) - 0.5f) / 255.0f : 0.0f;
			return { Saturate(SrgbOetf(Saturate(t[0])) + noise), Saturate(SrgbOetf(Saturate(t[1])) + noise),
				Saturate(SrgbOetf(Saturate(t[2])) + noise), Saturate(source[3]) };
		}
		// HDR: tone map relative to the display peak, in units of paper white.
		const float headroom = p.PeakNits / p.PaperWhiteNits;
		auto t = ToneMap(op, Float3{ c[0] / headroom, c[1] / headroom, c[2] / headroom }, p.WhitePoint);
		const Float3 nits{ t[0] * headroom * p.PaperWhiteNits, t[1] * headroom * p.PaperWhiteNits, t[2] * headroom * p.PaperWhiteNits };
		if (p.Encoding == static_cast<std::uint32_t>(OutputEncoding::Hdr10))
		{
			const auto wide = Rec709ToRec2020(nits);
			return { PqOetf(wide[0]), PqOetf(wide[1]), PqOetf(wide[2]), source[3] };
		}
		return { nits[0] / 80.0f, nits[1] / 80.0f, nits[2] / 80.0f, source[3] };
	}

	PostResult RunPostProcess(
		const Image& source, const PostProcessSettings& settings, const GpuExposureState& previous, float deltaTime, bool reset)
	{
		ValidatePostProcessSettings(settings);
		PostResult result;
		if (settings.Exposure.Mode == ExposureMode::Automatic)
		{
			result.Bins = BuildHistogram(source, settings.Exposure);
		}
		result.State = UpdateExposure(result.Bins, settings.Exposure, previous, deltaTime, reset);
		const std::uint32_t levels = settings.Bloom.Enabled ? BloomLevelCount(source.Width, source.Height, settings.Bloom.MipCount) : 0u;
		for (std::uint32_t i = 0; i < levels; ++i)
		{
			const auto& from = i == 0 ? source : result.Down.back();
			result.Down.push_back(BloomDownsample(from, source.Width >> (i + 1), source.Height >> (i + 1), i == 0, result.State.Exposure,
				settings.Bloom.Threshold, settings.Bloom.Knee));
		}
		result.Up.resize(levels);
		if (levels > 0)
		{
			result.Up[levels - 1] = result.Down[levels - 1];
			for (std::uint32_t i = levels - 1; i-- > 0;)
			{
				result.Up[i] = BloomUpsample(result.Up[i + 1], result.Down[i]);
			}
		}
		result.Params = BuildPostParams(settings, levels);
		result.Output = Image(source.Width, source.Height);
		const bool sdr = settings.Output.Encoding == OutputEncoding::Srgb;
		for (std::uint32_t y = 0; y < source.Height; ++y)
		{
			for (std::uint32_t x = 0; x < source.Width; ++x)
			{
				const Float3 bloom = levels > 0 ? TentUpsample(result.Up[0], x, y, source.Width, source.Height) : Float3{ 0, 0, 0 };
				auto texel = CompositeTexel(result.Params, result.State.Exposure, source.At(x, y), bloom, x, y);
				for (auto& v : texel)
				{
					v = sdr ? std::round(v * 255.0f) / 255.0f : RoundToHalf(v);
				}
				result.Output.At(x, y) = texel;
			}
		}
		return result;
	}
} // namespace Swim::Render::Post
