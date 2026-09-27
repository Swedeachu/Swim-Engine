#include "Engine/Runtime/FrameProfiler.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>

namespace Engine
{
	namespace
	{
		constexpr char Separator = '\x1f';

		double Percentile(const std::vector<double>& sorted, double p)
		{
			if (sorted.empty())
			{
				return 0.0;
			}
			const double index = p * double(sorted.size() - 1);
			const auto low = static_cast<std::size_t>(std::floor(index));
			const auto high = std::min(low + 1, sorted.size() - 1);
			return sorted[low] + (sorted[high] - sorted[low]) * (index - double(low));
		}

		std::string Csv(std::string_view text)
		{
			if (text.find_first_of(",\"\n") == std::string_view::npos)
			{
				return std::string(text);
			}
			std::string quoted = "\"";
			for (const char c : text)
			{
				if (c == '"')
				{
					quoted += '"';
				}
				quoted += c;
			}
			return quoted + '"';
		}
	} // namespace

	void FrameProfiler::Add(std::string_view category, std::string_view name, double milliseconds)
	{
		if (!std::isfinite(milliseconds))
		{
			return;
		}
		std::string key;
		key.reserve(category.size() + name.size() + 1);
		key.append(category).push_back(Separator);
		key.append(name);
		frame[key] += milliseconds;
	}

	void FrameProfiler::Begin(std::uint32_t warmup, std::uint32_t frames, std::filesystem::path csv, std::string labelValue)
	{
		samples.clear();
		warmupLeft = warmup;
		framesLeft = std::max(frames, 1u);
		measured = 0;
		capturing = true;
		csvPath = std::move(csv);
		label = std::move(labelValue);
	}

	bool FrameProfiler::EndFrame()
	{
		bool completed = false;
		if (capturing)
		{
			if (warmupLeft > 0)
			{
				--warmupLeft;
			}
			else
			{
				// A zone missing this frame counts as absent (its Frames share shows it).
				for (const auto& [key, ms] : frame)
				{
					samples[key].push_back(ms);
				}
				++measured;
				if (--framesLeft == 0)
				{
					capturing = false;
					completed = true;
					report = {};
					report.Label = label;
					report.Frames = measured;
					for (auto& [key, values] : samples)
					{
						std::sort(values.begin(), values.end());
						ZoneStats zone;
						const auto split = key.find(Separator);
						zone.Category = key.substr(0, split);
						zone.Name = split == std::string::npos ? std::string() : key.substr(split + 1);
						zone.Frames = static_cast<std::uint32_t>(values.size());
						double total = 0.0;
						for (const double v : values)
						{
							total += v;
						}
						zone.Mean = total / double(values.size());
						zone.PerFrame = total / double(measured);
						zone.Min = values.front();
						zone.Max = values.back();
						zone.P50 = Percentile(values, 0.5);
						zone.P95 = Percentile(values, 0.95);
						zone.P99 = Percentile(values, 0.99);
						report.Zones.push_back(std::move(zone));
					}
					std::sort(report.Zones.begin(), report.Zones.end(),
						[](const ZoneStats& a, const ZoneStats& b)
						{
							if (a.Category != b.Category)
							{
								return a.Category < b.Category;
							}
							return a.PerFrame > b.PerFrame;
						});
					if (!csvPath.empty())
					{
						std::error_code error;
						if (csvPath.has_parent_path())
						{
							std::filesystem::create_directories(csvPath.parent_path(), error);
						}
						const bool exists = std::filesystem::exists(csvPath, error);
						std::ofstream out(csvPath, std::ios::app);
						out << ToCsv(report, !exists);
					}
				}
			}
		}
		frame.clear();
		return completed;
	}

	const FrameProfiler::ZoneStats* FrameProfiler::Report::Find(std::string_view category, std::string_view name) const
	{
		for (const auto& zone : Zones)
		{
			if (zone.Category == category && zone.Name == name)
			{
				return &zone;
			}
		}
		return nullptr;
	}

	std::string FrameProfiler::ToCsv(const Report& report, bool header)
	{
		std::ostringstream out;
		if (header)
		{
			out << "label,category,name,frames,per_frame_ms,mean_ms,min_ms,p50_ms,p95_ms,p99_ms,max_ms\n";
		}
		char buffer[256];
		for (const auto& zone : report.Zones)
		{
			std::snprintf(buffer, sizeof(buffer), ",%u,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f\n", zone.Frames, zone.PerFrame, zone.Mean, zone.Min,
				zone.P50, zone.P95, zone.P99, zone.Max);
			out << Csv(report.Label) << ',' << Csv(zone.Category) << ',' << Csv(zone.Name) << buffer;
		}
		return out.str();
	}

	std::string FrameProfiler::Summary(const Report& report, std::size_t top)
	{
		std::ostringstream out;
		out << "[Profile] " << (report.Label.empty() ? std::string("capture") : report.Label) << ": " << report.Frames << " frames\n";
		std::map<std::string, std::size_t> shown;
		char buffer[256];
		for (const auto& zone : report.Zones)
		{
			if (shown[zone.Category]++ >= top)
			{
				continue;
			}
			std::snprintf(buffer, sizeof(buffer), "[Profile]   %-6s %-44.44s %8.3f ms  (p95 %7.3f, max %7.3f, %u frames)\n", zone.Category.c_str(),
				zone.Name.c_str(), zone.PerFrame, zone.P95, zone.Max, zone.Frames);
			out << buffer;
		}
		return out.str();
	}
} // namespace Engine
