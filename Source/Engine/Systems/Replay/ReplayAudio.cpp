#include "Engine/Systems/Replay/ReplayAudio.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <numeric>

namespace Swim::Replay
{

	namespace
	{

		constexpr std::size_t FrameSize = 1024;
		constexpr std::size_t HopSize = 512;
		constexpr std::size_t BandCount = 12;
		constexpr double Pi = 3.14159265358979323846;

		void Fft(std::vector<std::complex<float>>& data)
		{
			const std::size_t n = data.size();

			for (std::size_t i = 1, j = 0; i < n; ++i)
			{
				std::size_t bit = n >> 1;

				for (; j & bit; bit >>= 1)
				{
					j ^= bit;
				}

				j ^= bit;

				if (i < j)
				{
					std::swap(data[i], data[j]);
				}
			}

			for (std::size_t length = 2; length <= n; length <<= 1)
			{
				const double angle = -2.0 * Pi / static_cast<double>(length);
				const std::complex<float> step(static_cast<float>(std::cos(angle)), static_cast<float>(std::sin(angle)));

				for (std::size_t i = 0; i < n; i += length)
				{
					std::complex<float> w(1.0f, 0.0f);

					for (std::size_t k = 0; k < length / 2; ++k)
					{
						const auto u = data[i + k];
						const auto v = data[i + k + length / 2] * w;
						data[i + k] = u + v;
						data[i + k + length / 2] = u - v;
						w *= step;
					}
				}
			}
		}

		// Log-compressed magnitude spectra, one per hop.
		std::vector<std::vector<float>> Spectrogram(std::span<const float> mono)
		{
			std::vector<std::vector<float>> frames;

			if (mono.size() < FrameSize)
			{
				return frames;
			}

			std::vector<float> window(FrameSize);

			for (std::size_t i = 0; i < FrameSize; ++i)
			{
				window[i] = static_cast<float>(0.5 - 0.5 * std::cos(2.0 * Pi * static_cast<double>(i) / (FrameSize - 1)));
			}

			std::vector<std::complex<float>> buffer(FrameSize);

			for (std::size_t start = 0; start + FrameSize <= mono.size(); start += HopSize)
			{
				for (std::size_t i = 0; i < FrameSize; ++i)
				{
					buffer[i] = { mono[start + i] * window[i], 0.0f };
				}

				Fft(buffer);
				std::vector<float> magnitude(FrameSize / 2);

				for (std::size_t i = 0; i < magnitude.size(); ++i)
				{
					magnitude[i] = std::log1p(10.0f * std::abs(buffer[i]));
				}

				frames.push_back(std::move(magnitude));
			}

			return frames;
		}

		float Mean(std::span<const float> values)
		{
			return values.empty() ? 0.0f : std::accumulate(values.begin(), values.end(), 0.0f) / static_cast<float>(values.size());
		}

	} // namespace

	std::vector<float> MixToMono(std::span<const float> interleaved, std::uint32_t channels)
	{
		channels = std::max(channels, 1u);
		std::vector<float> mono(interleaved.size() / channels);

		for (std::size_t i = 0; i < mono.size(); ++i)
		{
			float sum = 0.0f;

			for (std::uint32_t c = 0; c < channels; ++c)
			{
				sum += interleaved[i * channels + c];
			}

			mono[i] = sum / static_cast<float>(channels);
		}

		return mono;
	}

	AudioPeaks ComputePeaks(std::span<const float> mono, double sampleRate, double bucketsPerSecond)
	{
		AudioPeaks peaks;
		peaks.BucketsPerSecond = bucketsPerSecond;

		if (mono.empty() || sampleRate <= 0.0 || bucketsPerSecond <= 0.0)
		{
			return peaks;
		}

		const double samplesPerBucket = std::max(sampleRate / bucketsPerSecond, 1.0);
		const auto buckets = static_cast<std::size_t>(std::ceil(static_cast<double>(mono.size()) / samplesPerBucket));
		peaks.Min.assign(buckets, 0.0f);
		peaks.Max.assign(buckets, 0.0f);

		for (std::size_t b = 0; b < buckets; ++b)
		{
			const auto from = static_cast<std::size_t>(static_cast<double>(b) * samplesPerBucket);
			const auto to = std::min(mono.size(), static_cast<std::size_t>(static_cast<double>(b + 1) * samplesPerBucket));
			float low = 0.0f;
			float high = 0.0f;

			for (std::size_t i = from; i < to; ++i)
			{
				low = std::min(low, mono[i]);
				high = std::max(high, mono[i]);
			}

			peaks.Min[b] = std::clamp(low, -1.0f, 1.0f);
			peaks.Max[b] = std::clamp(high, -1.0f, 1.0f);
		}

		return peaks;
	}

	OnsetAnalysis DetectOnsets(std::span<const float> mono, double sampleRate, float sensitivity)
	{
		OnsetAnalysis result;
		result.FrameRate = sampleRate / HopSize;
		result.Duration = sampleRate > 0.0 ? static_cast<double>(mono.size()) / sampleRate : 0.0;
		const auto spectra = Spectrogram(mono);

		if (spectra.size() < 3)
		{
			return result;
		}

		result.Envelope.assign(spectra.size(), 0.0f);

		for (std::size_t f = 1; f < spectra.size(); ++f)
		{
			float flux = 0.0f;

			for (std::size_t i = 0; i < spectra[f].size(); ++i)
			{
				flux += std::max(0.0f, spectra[f][i] - spectra[f - 1][i]);
			}

			result.Envelope[f] = flux;
		}

		const float peak = *std::max_element(result.Envelope.begin(), result.Envelope.end());

		if (peak <= 0.0f)
		{
			return result;
		}

		for (float& v : result.Envelope)
		{
			v /= peak;
		}

		// Local maxima above a moving average plus a margin; at least 50 ms apart.
		const float delta = 0.07f / std::max(sensitivity, 0.05f);
		const auto minGap = static_cast<std::ptrdiff_t>(std::max(1.0, 0.05 * result.FrameRate));
		std::ptrdiff_t last = -minGap;
		const auto count = static_cast<std::ptrdiff_t>(result.Envelope.size());

		for (std::ptrdiff_t f = 1; f < count - 1; ++f)
		{
			const float v = result.Envelope[static_cast<std::size_t>(f)];
			bool isMax = true;

			for (std::ptrdiff_t k = std::max<std::ptrdiff_t>(0, f - 3); k <= std::min(count - 1, f + 3) && isMax; ++k)
			{
				isMax = k == f || result.Envelope[static_cast<std::size_t>(k)] <= v;
			}

			if (!isMax || f - last < minGap)
			{
				continue;
			}

			const auto from = static_cast<std::size_t>(std::max<std::ptrdiff_t>(0, f - 16));
			const auto to = static_cast<std::size_t>(std::min(count, f + 9));
			const float average = Mean(std::span<const float>(result.Envelope).subspan(from, to - from));

			if (v > average + delta)
			{
				result.Onsets.push_back(static_cast<double>(f) / result.FrameRate);
				result.Strength.push_back(v);
				last = f;
			}
		}

		return result;
	}

	BeatGrid EstimateBeats(const OnsetAnalysis& onsets, double minBpm, double maxBpm, std::uint32_t beatsPerBar)
	{
		BeatGrid grid;
		const auto& env = onsets.Envelope;

		if (env.size() < 16 || onsets.FrameRate <= 0.0)
		{
			return grid;
		}

		const float mean = Mean(env);
		std::vector<float> centred(env.size());
		std::transform(env.begin(), env.end(), centred.begin(),
			[mean](float v)
			{
				return v - mean;
			});
		const auto minLag = static_cast<std::size_t>(std::max(1.0, std::floor(60.0 * onsets.FrameRate / maxBpm)));
		const auto maxLag = std::min(centred.size() / 2, static_cast<std::size_t>(std::ceil(60.0 * onsets.FrameRate / minBpm)));

		if (minLag >= maxLag)
		{
			return grid;
		}

		const auto correlate = [&](std::size_t lag)
		{
			double sum = 0.0;

			for (std::size_t i = lag; i < centred.size(); ++i)
			{
				sum += static_cast<double>(centred[i]) * centred[i - lag];
			}

			return sum / static_cast<double>(centred.size() - lag);
		};

		const double zero = correlate(0);
		std::vector<double> scores(maxLag + 2, 0.0);
		std::size_t best = minLag;

		for (std::size_t lag = minLag; lag <= maxLag; ++lag)
		{
			const double bpm = 60.0 * onsets.FrameRate / static_cast<double>(lag);
			const double octave = std::log2(bpm / 120.0);
			const double weight = std::exp(-0.5 * octave * octave / (0.9 * 0.9));
			scores[lag] = correlate(lag) * weight;

			if (scores[lag] > scores[best])
			{
				best = lag;
			}
		}

		// Parabolic refinement of the lag.
		double lag = static_cast<double>(best);

		if (best > minLag && best < maxLag)
		{
			const double a = scores[best - 1];
			const double b = scores[best];
			const double c = scores[best + 1];
			const double denominator = a - 2.0 * b + c;

			if (std::abs(denominator) > 1.0e-12)
			{
				lag += std::clamp(0.5 * (a - c) / denominator, -0.5, 0.5);
			}
		}

		grid.Bpm = 60.0 * onsets.FrameRate / lag;
		grid.Confidence = zero > 0.0 ? static_cast<float>(std::clamp(correlate(best) / zero, 0.0, 1.0)) : 0.0f;

		// Phase: the offset whose comb of beats collects the most envelope.
		const auto sumAt = [&](double offset, double period, std::uint32_t every, std::uint32_t which)
		{
			double sum = 0.0;
			std::uint32_t index = 0;

			for (double f = offset; f < static_cast<double>(env.size()); f += period, ++index)
			{
				if (every > 1 && index % every != which)
				{
					continue;
				}

				sum += env[static_cast<std::size_t>(f)];
			}

			return sum;
		};

		double bestOffset = 0.0;
		double bestSum = -1.0;

		for (double offset = 0.0; offset < lag; offset += 1.0)
		{
			const double sum = sumAt(offset, lag, 1, 0);

			if (sum > bestSum)
			{
				bestSum = sum;
				bestOffset = offset;
			}
		}

		grid.Phase = bestOffset / onsets.FrameRate;
		const double period = 60.0 / grid.Bpm;

		for (double t = grid.Phase; t < onsets.Duration; t += period)
		{
			grid.Beats.push_back(t);
		}

		beatsPerBar = std::max(beatsPerBar, 1u);
		std::uint32_t bestBar = 0;
		double bestBarSum = -1.0;

		for (std::uint32_t which = 0; which < beatsPerBar; ++which)
		{
			const double sum = sumAt(bestOffset, lag, beatsPerBar, which);

			if (sum > bestBarSum)
			{
				bestBarSum = sum;
				bestBar = which;
			}
		}

		for (std::size_t i = bestBar; i < grid.Beats.size(); i += beatsPerBar)
		{
			grid.Downbeats.push_back(grid.Beats[i]);
		}

		return grid;
	}

	std::vector<double> DetectSections(std::span<const float> mono, double sampleRate, const BeatGrid& beats, double minSectionSeconds)
	{
		std::vector<double> boundaries;
		const auto spectra = Spectrogram(mono);

		if (spectra.size() < 8 || sampleRate <= 0.0)
		{
			return boundaries;
		}

		const double frameRate = sampleRate / HopSize;
		const double duration = static_cast<double>(mono.size()) / sampleRate;

		// Segment times: beats, or half seconds without a grid.
		std::vector<double> times = beats.Beats;

		if (times.size() < 8)
		{
			times.clear();

			for (double t = 0.0; t < duration; t += 0.5)
			{
				times.push_back(t);
			}
		}

		// Log-spaced band energies averaged per segment, then normalized.
		const std::size_t bins = spectra.front().size();
		std::vector<std::array<float, BandCount + 1>> features;

		for (std::size_t s = 0; s + 1 < times.size(); ++s)
		{
			const auto from = static_cast<std::size_t>(times[s] * frameRate);
			const auto to = std::min(spectra.size(), static_cast<std::size_t>(times[s + 1] * frameRate) + 1);
			std::array<float, BandCount + 1> feature{};

			for (std::size_t f = from; f < to; ++f)
			{
				for (std::size_t band = 0; band < BandCount; ++band)
				{
					const auto lo = static_cast<std::size_t>(std::pow(static_cast<double>(bins), static_cast<double>(band) / BandCount));
					const auto hi = std::max(
						lo + 1, static_cast<std::size_t>(std::pow(static_cast<double>(bins), static_cast<double>(band + 1) / BandCount)));

					for (std::size_t i = lo; i < std::min(hi, bins); ++i)
					{
						feature[band] += spectra[f][i];
					}
				}
			}

			float norm = 0.0f;

			for (std::size_t band = 0; band < BandCount; ++band)
			{
				norm += feature[band] * feature[band];
			}

			norm = std::sqrt(norm);
			feature[BandCount] = norm / static_cast<float>(std::max<std::size_t>(to - from, 1));

			if (norm > 0.0f)
			{
				for (std::size_t band = 0; band < BandCount; ++band)
				{
					feature[band] /= norm;
				}
			}

			features.push_back(feature);
		}

		if (features.size() < 8)
		{
			return boundaries;
		}

		// Loudness as a feature on the same scale as the shape.
		float loudest = 0.0f;

		for (const auto& f : features)
		{
			loudest = std::max(loudest, f[BandCount]);
		}

		for (auto& f : features)
		{
			f[BandCount] = loudest > 0.0f ? f[BandCount] / loudest : 0.0f;
		}

		const auto similarity = [&](std::size_t a, std::size_t b)
		{
			float dot = 0.0f;

			for (std::size_t i = 0; i <= BandCount; ++i)
			{
				dot += features[a][i] * features[b][i];
			}

			return dot;
		};

		// Checkerboard-kernel novelty along the diagonal of the self-similarity matrix.
		const auto n = static_cast<std::ptrdiff_t>(features.size());
		const std::ptrdiff_t half = std::clamp<std::ptrdiff_t>(n / 8, 2, 8);
		std::vector<float> novelty(features.size(), 0.0f);

		for (std::ptrdiff_t c = half; c < n - half; ++c)
		{
			float sum = 0.0f;

			for (std::ptrdiff_t i = -half; i < half; ++i)
			{
				for (std::ptrdiff_t j = -half; j < half; ++j)
				{
					const float sign = ((i < 0) == (j < 0)) ? 1.0f : -1.0f;
					const float taper = std::exp(-0.5f * static_cast<float>(i * i + j * j) / static_cast<float>(half * half));
					sum += sign * taper * similarity(static_cast<std::size_t>(c + i), static_cast<std::size_t>(c + j));
				}
			}

			novelty[static_cast<std::size_t>(c)] = std::max(sum, 0.0f);
		}

		const float mean = Mean(novelty);
		float variance = 0.0f;

		for (const float v : novelty)
		{
			variance += (v - mean) * (v - mean);
		}

		const float threshold = mean + 0.5f * std::sqrt(variance / static_cast<float>(novelty.size()));
		std::vector<std::pair<float, double>> candidates;

		for (std::ptrdiff_t c = 1; c < n - 1; ++c)
		{
			const float v = novelty[static_cast<std::size_t>(c)];

			if (v > threshold && v >= novelty[static_cast<std::size_t>(c - 1)] && v >= novelty[static_cast<std::size_t>(c + 1)])
			{
				candidates.emplace_back(v, times[static_cast<std::size_t>(c)]);
			}
		}

		// Strongest first, keeping the minimum spacing.
		std::sort(candidates.begin(), candidates.end(),
			[](const auto& a, const auto& b)
			{
				return a.first > b.first;
			});

		for (const auto& [strength, time] : candidates)
		{
			const bool clear = std::none_of(boundaries.begin(), boundaries.end(),
				[&](double other)
				{
					return std::abs(other - time) < minSectionSeconds;
				});

			if (clear && time >= minSectionSeconds * 0.5 && time <= duration - minSectionSeconds * 0.5)
			{
				boundaries.push_back(time);
			}
		}

		std::sort(boundaries.begin(), boundaries.end());
		return boundaries;
	}

	double Snap(double time, std::span<const double> targets, double tolerance)
	{
		double best = time;
		double bestDistance = tolerance;

		for (const double target : targets)
		{
			const double distance = std::abs(target - time);

			if (distance <= bestDistance)
			{
				bestDistance = distance;
				best = target;
			}
		}

		return best;
	}

} // namespace Swim::Replay
