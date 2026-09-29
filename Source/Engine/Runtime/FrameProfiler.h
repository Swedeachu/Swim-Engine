#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Engine
{

	// Per-frame timing zones and a capture over many frames (the `profile` console command).
	//
	// Each frame, code records named durations in milliseconds: CPU zones the engine times
	// itself (Scope / Add), the renderer's CPU phases and every GPU pass (AddSample with a
	// category). Durations with the same name within one frame are summed (a pass that runs
	// once per probe face adds up). While a capture runs, EndFrame folds the frame into
	// per-zone statistics; when it has seen its frame count it finishes: mean, min, max,
	// p50, p95 and p99 per zone, and the share of frames each zone appeared in. Results go to
	// a CSV and a text summary. UI-free and renderer-free, so it is testable headless.
	class FrameProfiler
	{

	  public:

		using Clock = std::chrono::steady_clock;

		// Times its lifetime into zone `name` (category "cpu").
		class Scope
		{

		  public:

			Scope(FrameProfiler& profiler, std::string_view name) : profiler(&profiler), name(name), start(Clock::now()) {}

			~Scope()
			{
				profiler->Add("cpu", name, std::chrono::duration<double, std::milli>(Clock::now() - start).count());
			}

			Scope(const Scope&) = delete;

			Scope& operator=(const Scope&) = delete;

		  private:

			FrameProfiler* profiler;
			std::string_view name;
			Clock::time_point start;

		};

		void Add(std::string_view category, std::string_view name, double milliseconds);

		// Starts a capture: `warmup` frames are skipped, then `frames` are measured.
		void Begin(std::uint32_t warmup, std::uint32_t frames, std::filesystem::path csv = {}, std::string label = {});

		bool IsCapturing() const { return capturing; }

		// Ends the current frame. Returns true on the frame the capture completed.
		bool EndFrame();

		struct ZoneStats
		{
			std::string Category;
			std::string Name;
			double Mean = 0.0; // Over the frames it appeared in.
			double Min = 0.0;
			double Max = 0.0;
			double P50 = 0.0;
			double P95 = 0.0;
			double P99 = 0.0;
			double PerFrame = 0.0; // Total / measured frames (what it costs a frame on average).
			std::uint32_t Frames = 0;
		};

		struct Report
		{
			std::string Label;
			std::uint32_t Frames = 0;
			std::vector<ZoneStats> Zones; // Sorted by category, then PerFrame descending.
			const ZoneStats* Find(std::string_view category, std::string_view name) const;
		};

		// The last completed capture.
		const Report& GetReport() const { return report; }

		// CSV: label,category,name,frames,per_frame_ms,mean_ms,min_ms,p50_ms,p95_ms,p99_ms,max_ms
		static std::string ToCsv(const Report& report, bool header = true);

		// The costliest `top` zones of each category.
		static std::string Summary(const Report& report, std::size_t top = 12);

		// The current frame's zones (for overlays).
		const std::unordered_map<std::string, double>& GetFrame() const { return frame; }

	  private:

		std::unordered_map<std::string, double> frame; // "category\x1fname" -> ms.
		std::unordered_map<std::string, std::vector<double>> samples;
		std::uint32_t warmupLeft = 0;
		std::uint32_t framesLeft = 0;
		std::uint32_t measured = 0;
		bool capturing = false;
		std::filesystem::path csvPath;
		std::string label;
		Report report;

	};

} // namespace Engine
