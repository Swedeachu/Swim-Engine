#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace Swim::Replay
{

	// Audio analysis for editing to music: waveform peaks for drawing, onsets, a beat
	// grid and section boundaries. Works on mono float samples (MixToMono first);
	// decoding files is the engine's job (Runtime/ReplayMusic).

	std::vector<float> MixToMono(std::span<const float> interleaved, std::uint32_t channels);

	struct AudioPeaks
	{
		double BucketsPerSecond = 0.0;
		std::vector<float> Min; // Per bucket, -1..1.
		std::vector<float> Max;
	};

	AudioPeaks ComputePeaks(std::span<const float> mono, double sampleRate, double bucketsPerSecond);

	struct OnsetAnalysis
	{
		double FrameRate = 0.0;		 // Envelope frames per second.
		std::vector<float> Envelope; // Spectral flux, normalized to 0..1.
		std::vector<double> Onsets;	 // Seconds.
		std::vector<float> Strength; // Envelope value at each onset.
		double Duration = 0.0;
	};

	// Spectral-flux onsets. Higher sensitivity finds more (weaker) onsets.
	OnsetAnalysis DetectOnsets(std::span<const float> mono, double sampleRate, float sensitivity = 1.0f);

	struct BeatGrid
	{
		double Bpm = 0.0;
		double Phase = 0.0;			   // Time of the first beat.
		std::vector<double> Beats;	   // Seconds, every beat over the duration.
		std::vector<double> Downbeats; // Every BeatsPerBar-th beat, phase chosen from the envelope.
		float Confidence = 0.0f;	   // 0..1: how periodic the envelope is at Bpm.
	};

	// A constant-tempo grid from the onset envelope (autocorrelation with a preference for
	// tempos near 120 BPM against octave errors, then the best phase).
	BeatGrid EstimateBeats(const OnsetAnalysis& onsets, double minBpm = 70.0, double maxBpm = 180.0, std::uint32_t beatsPerBar = 4);

	// Section boundaries (verse, chorus, drop...) from novelty in beat-synchronous spectral
	// features. Boundaries are at least minSectionSeconds apart and land on beats when a
	// grid is given.
	std::vector<double> DetectSections(
		std::span<const float> mono, double sampleRate, const BeatGrid& beats, double minSectionSeconds = 8.0);

	// The target nearest to time within tolerance (time itself when none is).
	double Snap(double time, std::span<const double> targets, double tolerance);

} // namespace Swim::Replay
