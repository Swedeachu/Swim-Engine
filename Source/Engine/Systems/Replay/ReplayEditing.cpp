#include "Engine/Systems/Replay/ReplayEditing.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>
#include <string>

namespace Swim::Replay
{

	namespace
	{

		constexpr float DegreesToRadians = 3.14159265358979f / 180.0f;

		Vec3 AroundY(float degrees, float radius, float height)
		{
			const float a = degrees * DegreesToRadians;
			return { std::sin(a) * radius, height, std::cos(a) * radius };
		}

		void AimAtTarget(CameraRig& rig, const ShotDesc& desc)
		{
			if (desc.Target != NoNode)
			{
				rig.Aim = CameraAim::Node;
				rig.LookAtNode = desc.Target;
				rig.LookAtOffset = desc.TargetOffset;
			}
			else
			{
				rig.Aim = CameraAim::Point;
				rig.LookAt.Set(0.0, Animation::Add(desc.Point, desc.TargetOffset), Ease::Step);
			}
		}

		struct Preset
		{
			float Exposure;
			float Temperature;
			float Tint;
			float Contrast;
			float Saturation;
			float Bloom;
			Vec3 Slope;
		};

		const std::map<std::string, Preset, std::less<>>& PresetTable()
		{
			static const std::map<std::string, Preset, std::less<>> table{
				{ "neutral", { 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.04f, { 1.0f, 1.0f, 1.0f } } },
				{ "golden_hour", { 0.3f, 35.0f, 5.0f, 1.05f, 1.15f, 0.08f, { 1.06f, 1.0f, 0.9f } } },
				{ "night", { -1.2f, -30.0f, 0.0f, 1.1f, 0.75f, 0.06f, { 0.9f, 0.95f, 1.1f } } },
				{ "noir", { -0.2f, 0.0f, 0.0f, 1.35f, 0.0f, 0.03f, { 1.0f, 1.0f, 1.0f } } },
				{ "studio", { 0.2f, 0.0f, 0.0f, 1.1f, 1.05f, 0.02f, { 1.0f, 1.0f, 1.0f } } },
				{ "neon", { 0.0f, -15.0f, 25.0f, 1.2f, 1.4f, 0.15f, { 1.05f, 0.95f, 1.1f } } },
			};
			return table;
		}

	} // namespace

	const char* ToString(ShotPreset preset)
	{
		switch (preset)
		{
		case ShotPreset::Static:
			return "static";
		case ShotPreset::Dolly:
			return "dolly";
		case ShotPreset::Follow:
			return "follow";
		case ShotPreset::Flythrough:
			return "flythrough";
		default:
			return "orbit";
		}
	}

	bool ParseShotPreset(std::string_view text, ShotPreset& preset)
	{
		for (ShotPreset p : { ShotPreset::Static, ShotPreset::Orbit, ShotPreset::Dolly, ShotPreset::Follow, ShotPreset::Flythrough })
		{
			if (text == ToString(p))
			{
				preset = p;
				return true;
			}
		}

		return false;
	}

	const Shot& CreateShot(ReplayProject& project, const ShotDesc& desc)
	{
		const std::string name = desc.Name.empty() ? std::string("shot ") + std::to_string(project.Shots.size() + 1) : desc.Name;
		const std::string rigName = "shot:" + name;
		project.Cameras.erase(rigName);
		project.Paths.erase(rigName);
		CameraRig& rig = project.EditCamera(rigName);
		Shot shot;
		shot.Name = name;
		shot.Camera = rigName;
		shot.Start = desc.Start;
		shot.End = std::max(desc.End, desc.Start + 1.0e-3);
		shot.ProgressEase = desc.Motion;

		if (desc.Fov > 0.0f)
		{
			rig.Fov.Set(desc.Start, desc.Fov, Ease::Step);
		}

		// Moving presets around a node ride in its frame (position only, so a spinning
		// subject does not spin the camera); around a point they are in world space.
		const Vec3 centre = desc.Target != NoNode ? Vec3{ 0.0f, 0.0f, 0.0f } : desc.Point;
		const auto followTarget = [&]
		{
			if (desc.Target != NoNode)
			{
				rig.RelativeTo = desc.Target;
				rig.FollowRotation = false;
			}
		};

		switch (desc.Preset)
		{
		case ShotPreset::Static:
		{
			followTarget();
			rig.Position.Set(desc.Start, Animation::Add(centre, AroundY(desc.StartAngle, desc.Radius, desc.Height)), Ease::Step);
			AimAtTarget(rig, desc);
			break;
		}
		case ShotPreset::Orbit:
		{
			followTarget();
			CameraPath& path = project.EditPath(rigName);
			path.Kind = PathKind::Centripetal;
			const int steps = std::max(2, static_cast<int>(std::ceil(std::abs(desc.Degrees) / 30.0f)));

			for (int i = 0; i <= steps; ++i)
			{
				const float angle = desc.StartAngle + desc.Degrees * static_cast<float>(i) / static_cast<float>(steps);
				path.Points.push_back({ Animation::Add(centre, AroundY(angle, desc.Radius, desc.Height)) });
			}

			path.Invalidate();
			shot.Path = rigName;
			AimAtTarget(rig, desc);
			break;
		}
		case ShotPreset::Dolly:
		{
			followTarget();
			CameraPath& path = project.EditPath(rigName);
			path.Kind = PathKind::Linear;
			path.Points.push_back({ Animation::Add(centre, AroundY(desc.StartAngle, desc.Radius, desc.Height)) });
			path.Points.push_back({ Animation::Add(
				centre, AroundY(desc.StartAngle, desc.EndRadius, desc.Height * desc.EndRadius / std::max(desc.Radius, 1.0e-3f))) });
			path.Invalidate();
			shot.Path = rigName;
			AimAtTarget(rig, desc);
			break;
		}
		case ShotPreset::Follow:
		{
			if (desc.Target != NoNode)
			{
				rig.RelativeTo = desc.Target;
				rig.FollowRotation = true;
			}

			rig.Position.Set(desc.Start, Animation::Add(centre, desc.Offset), Ease::Step);
			AimAtTarget(rig, desc);
			break;
		}
		case ShotPreset::Flythrough:
		{
			CameraPath& path = project.EditPath(rigName);
			path.Kind = PathKind::Centripetal;

			for (const Vec3& point : desc.Points)
			{
				path.Points.push_back({ point });
			}

			path.Invalidate();
			shot.Path = rigName;
			rig.Aim = CameraAim::Direction; // Along the path of travel.
			break;
		}
		}

		project.SetShot(shot);
		return *project.FindShot(name);
	}

	std::size_t CutsToBeats(ReplayProject& project, const BeatCutDesc& desc)
	{
		std::vector<double> times;

		for (const double t : desc.Times)
		{
			if (t >= desc.From && t <= desc.To)
			{
				times.push_back(t);
			}
		}

		std::sort(times.begin(), times.end());
		const std::uint32_t every = std::max(desc.Every, 1u);
		std::vector<double> cuts;

		for (std::size_t i = 0; i < times.size(); i += every)
		{
			cuts.push_back(times[i]);
		}

		if (cuts.size() < 2 || desc.Sources.empty())
		{
			return 0;
		}

		// Templates are copied before the range is cleared (they may live inside it).
		std::vector<Shot> templates;

		for (const std::string& source : desc.Sources)
		{
			if (const Shot* shot = project.FindShot(source))
			{
				templates.push_back(*shot);
			}
			else
			{
				Shot rig;
				rig.Camera = source;
				templates.push_back(rig);
			}
		}

		const double from = cuts.front();
		const double to = cuts.back();
		std::erase_if(project.Shots,
			[&](const Shot& s)
			{
				return s.Start >= from && s.End <= to;
			});

		std::size_t created = 0;

		for (std::size_t i = 0; i + 1 < cuts.size(); ++i)
		{
			Shot shot = templates[i % templates.size()];
			shot.Name = desc.Prefix + " " + std::to_string(i + 1);
			shot.Start = cuts[i];
			shot.End = cuts[i + 1];
			project.SetShot(std::move(shot));
			++created;
		}

		return created;
	}

	std::vector<double> CollectSnapTargets(const ReplayProject& project)
	{
		std::vector<double> targets;

		for (const auto& marker : project.Markers)
		{
			targets.push_back(marker.Time);
		}

		for (const auto& shot : project.Shots)
		{
			targets.push_back(shot.Start);
			targets.push_back(shot.End);
		}

		for (const auto& cut : project.Cuts)
		{
			targets.push_back(cut.Time);
		}

		for (const auto& jump : project.Jumps)
		{
			targets.push_back(jump.From);
			targets.push_back(jump.To);
		}

		std::sort(targets.begin(), targets.end());
		targets.erase(std::unique(targets.begin(), targets.end()), targets.end());
		return targets;
	}

	NodeEdit& AddLight(ReplayProject& project, const LightDesc& desc)
	{
		NodeEdit& node = project.AddNode(std::string(Names::Light));
		node.Label = desc.Label;
		node.AttachTo = desc.AttachTo;
		node.Position.Set(0.0, desc.Position, Ease::Linear);
		node.Rotation.Set(0.0, desc.Rotation, Ease::Linear);
		node.Visible.Set(0.0, true, Ease::Step);
		node.Props["type"].Set(0.0, desc.Type, Ease::Step);
		node.Props["color"].Set(0.0, desc.Color);
		node.Props["intensity"].Set(0.0, desc.Intensity);
		node.Props["range"].Set(0.0, desc.Range);
		node.Props["innerCone"].Set(0.0, desc.InnerCone);
		node.Props["outerCone"].Set(0.0, desc.OuterCone);
		node.Props["castShadows"].Set(0.0, desc.CastShadows, Ease::Step);
		return node;
	}

	const std::vector<std::string>& LightingPresets()
	{
		static const std::vector<std::string> names = []
		{
			std::vector<std::string> out;

			for (const auto& [name, preset] : PresetTable())
			{
				out.push_back(name);
			}

			return out;
		}();
		return names;
	}

	bool ApplyLightingPreset(ReplayProject& project, std::string_view preset, double time, Ease ease)
	{
		const auto found = PresetTable().find(preset);

		if (found == PresetTable().end())
		{
			return false;
		}

		const Preset& p = found->second;
		project.Environment["post.exposure"].Set(time, p.Exposure, ease);
		project.Environment["grade.temperature"].Set(time, p.Temperature, ease);
		project.Environment["grade.tint"].Set(time, p.Tint, ease);
		project.Environment["grade.contrast"].Set(time, p.Contrast, ease);
		project.Environment["grade.saturation"].Set(time, p.Saturation, ease);
		project.Environment["grade.slope"].Set(time, p.Slope, ease);
		project.Environment["bloom.intensity"].Set(time, p.Bloom, ease);
		return true;
	}

	namespace
	{

		struct Anchor
		{
			double T = 0.0; // Timeline.
			double R = 0.0; // Recording.
		};

		// Knots (0, a), (f, m), (1, b) of a linear speed curve over a segment whose mean is
		// mean: m at the middle when that stays above minScale, else minScale with the
		// middle knot moved. False when no such curve exists.
		bool SolveSegment(double a, double b, double mean, double minScale, double& f, double& m)
		{
			f = 0.5;
			m = 2.0 * mean - 0.5 * (a + b);

			if (m >= minScale)
			{
				return true;
			}

			m = minScale;

			if (std::abs(a - b) < 1.0e-9)
			{
				return false;
			}

			f = (2.0 * mean - m - b) / (a - b);
			return f > 0.01 && f < 0.99;
		}

		// The timeline time showing a recording time within [low, high] (the remap is monotonic).
		double TimelineAt(const ReplayProject& project, double recording, double low, double high)
		{
			for (int i = 0; i < 60; ++i)
			{
				const double mid = 0.5 * (low + high);
				(project.ToRecordingTime(mid) < recording ? low : high) = mid;
			}

			return 0.5 * (low + high);
		}

		void EraseKeys(Track<float>& track, double from, double to)
		{
			std::erase_if(track.Keys,
				[&](const TrackKey<float>& k)
				{
					return k.Time >= from - Track<float>::Epsilon && k.Time <= to + Track<float>::Epsilon;
				});
		}

	} // namespace

	RegionSyncResult SyncRegion(ReplayProject& project, const RegionSyncDesc& d, double recordingLength)
	{
		if (!std::isfinite(d.From) || !std::isfinite(d.To) || !(d.To - d.From > 0.05))
		{
			throw std::invalid_argument("the region is empty (to must be after from)");
		}

		const double minScale = std::max(static_cast<double>(d.MinScale), 0.001);
		const double maxScale = std::max(static_cast<double>(d.MaxScale), minScale);
		ReplayProject old;
		old.TimeRemap = project.TimeRemap;
		const double contentEnd = std::isfinite(d.ContentTo) && d.ContentTo > d.From ? d.ContentTo : d.To;
		const double r1 = old.ToRecordingTime(contentEnd);
		const bool wasEmpty = project.TimeScale.Empty();
		const double speedIn = wasEmpty ? 1.0 : *project.TimeScale.Evaluate(d.From);
		const double speedAfter = wasEmpty ? 1.0 : *project.TimeScale.Evaluate(d.To);
		const bool keysAfter = std::any_of(project.TimeScale.Keys.begin(), project.TimeScale.Keys.end(),
			[&](const TrackKey<float>& k)
			{
				return k.Time > d.To + 0.5 + Track<float>::Epsilon;
			});

		std::vector<Anchor> wanted;

		for (const auto& [beat, event] : d.Pairs)
		{
			if (std::isfinite(beat) && std::isfinite(event))
			{
				wanted.push_back({ beat, old.ToRecordingTime(event) });
			}
		}

		std::sort(wanted.begin(), wanted.end(),
			[](const Anchor& a, const Anchor& b)
			{
				return a.T < b.T;
			});
		std::vector<double> events;

		for (const double e : d.Events)
		{
			if (std::isfinite(e))
			{
				events.push_back(old.ToRecordingTime(e));
			}
		}

		std::sort(events.begin(), events.end());
		std::vector<double> beats(d.Beats.begin(), d.Beats.end());
		std::sort(beats.begin(), beats.end());

		if (wasEmpty && d.From > Track<float>::Epsilon)
		{
			project.TimeScale.Set(0.0, 1.0f);
		}

		// A key just before From holds the old curve there (exact for step and linear keys),
		// so the region's first speed does not bend what plays before it.
		const double holdTime = d.From - 1.0e-5;

		if (holdTime > Track<float>::Epsilon)
		{
			project.TimeScale.Set(holdTime, *project.TimeScale.Evaluate(holdTime), Ease::Linear);
		}

		RegionSyncResult result;
		double r0 = old.ToRecordingTime(d.From);

		// The speed before From depends on the key written at From, so refit until the
		// recording time at From settles.
		for (int iteration = 0; iteration < 4; ++iteration)
		{
			if (!(r1 > r0 + 1.0e-6))
			{
				throw std::invalid_argument("the content must move forward over the region (contentTo after from)");
			}

			std::vector<Anchor> anchors{ { d.From, r0 } };
			std::size_t dropped = 0;
			const auto fits = [&](double t, double r)
			{
				return t > anchors.back().T + 0.01 && t < d.To - 0.01 && r > anchors.back().R + 1.0e-6 && r < r1 - 1.0e-6;
			};

			if (!wanted.empty())
			{
				for (const Anchor& w : wanted)
				{
					if (fits(w.T, w.R))
					{
						anchors.push_back(w);
					}
					else
					{
						++dropped;
					}
				}
			}
			else
			{
				for (const double r : events)
				{
					if (!(r > r0 && r < r1))
					{
						continue;
					}

					const double even = d.From + (r - r0) / (r1 - r0) * (d.To - d.From);
					double best = 0.0;
					double bestDistance = std::max(d.MaxShift, 0.0);
					bool found = false;

					for (const double b : beats)
					{
						if (fits(b, r) && std::abs(b - even) <= bestDistance)
						{
							best = b;
							bestDistance = std::abs(b - even);
							found = true;
						}
					}

					if (found)
					{
						anchors.push_back({ best, r });
					}
					else
					{
						++dropped;
					}
				}
			}

			anchors.push_back({ d.To, r1 });

			// Anchors that would need a speed outside the range are dropped.
			for (;;)
			{
				std::size_t bad = anchors.size();

				for (std::size_t k = 0; k + 1 < anchors.size(); ++k)
				{
					const double mean = (anchors[k + 1].R - anchors[k].R) / (anchors[k + 1].T - anchors[k].T);

					if (mean < minScale || mean > maxScale)
					{
						bad = k;
						break;
					}
				}

				if (bad == anchors.size())
				{
					break;
				}

				if (anchors.size() == 2)
				{
					throw std::invalid_argument("the region needs speed " + std::to_string((r1 - r0) / (d.To - d.From)) + ", outside " +
						std::to_string(minScale) + ".." + std::to_string(maxScale));
				}

				anchors.erase(anchors.begin() + static_cast<std::ptrdiff_t>(bad + 2 < anchors.size() ? bad + 1 : bad));
				++dropped;
			}

			const std::size_t n = anchors.size() - 1;
			std::vector<double> means(n);

			for (std::size_t k = 0; k < n; ++k)
			{
				means[k] = (anchors[k + 1].R - anchors[k].R) / (anchors[k + 1].T - anchors[k].T);
			}

			EraseKeys(project.TimeScale, d.From, keysAfter ? d.To : d.To + 0.5);

			if (d.Smooth)
			{
				std::vector<double> v(n + 1);
				v[0] = speedIn;
				v[n] = speedAfter;

				for (std::size_t k = 1; k < n; ++k)
				{
					v[k] = std::min(means[k - 1], means[k]);
				}

				double f = 0.5;
				double m = 0.0;

				// Interior knots never exceed their neighbours' means; the ends give way when
				// keeping the outside speed cannot fit.
				if (!SolveSegment(v[0], v[1], means[0], minScale, f, m))
				{
					v[0] = std::min(v[0], means[0]);
				}

				if (!SolveSegment(v[n - 1], v[n], means[n - 1], minScale, f, m))
				{
					v[n] = std::min(v[n], means[n - 1]);
				}

				for (std::size_t k = 0; k < n; ++k)
				{
					if (!SolveSegment(v[k], v[k + 1], means[k], minScale, f, m))
					{
						f = 0.5;
						m = 2.0 * means[k] - 0.5 * (v[k] + v[k + 1]);
					}

					const double span = anchors[k + 1].T - anchors[k].T;
					project.TimeScale.Set(anchors[k].T, static_cast<float>(v[k]), Ease::Linear);
					project.TimeScale.Set(anchors[k].T + f * span, static_cast<float>(m), Ease::Linear);
				}

				project.TimeScale.Set(d.To, static_cast<float>(v[n]), Ease::Linear);

				if (!keysAfter && std::abs(v[n] - speedAfter) > 1.0e-6)
				{
					project.TimeScale.Set(d.To + 0.5, static_cast<float>(speedAfter), Ease::Linear);
				}
			}
			else
			{
				for (std::size_t k = 0; k < n; ++k)
				{
					project.TimeScale.Set(anchors[k].T, static_cast<float>(means[k]), Ease::Step);
				}

				project.TimeScale.Set(d.To, static_cast<float>(speedAfter), keysAfter ? Ease::Linear : Ease::Step);
			}

			project.BakeTimeScale(recordingLength);
			result.Anchors.clear();

			for (const Anchor& a : anchors)
			{
				result.Anchors.emplace_back(a.T, a.R);
			}

			result.Dropped = dropped;
			result.MinSpeed = static_cast<float>(*std::min_element(means.begin(), means.end()));
			result.MaxSpeed = static_cast<float>(*std::max_element(means.begin(), means.end()));
			const double measured = project.ToRecordingTime(d.From);

			if (std::abs(measured - r0) < 1.0e-7)
			{
				break;
			}

			r0 = measured;
		}

		result.MaxError = 0.0;

		for (const auto& [t, r] : result.Anchors)
		{
			result.MaxError = std::max(result.MaxError, std::abs(project.ToRecordingTime(t) - r));
		}

		if (!d.RetimeMarkerKind.empty())
		{
			for (ReplayMarker& marker : project.Markers)
			{
				if (marker.Kind == d.RetimeMarkerKind && marker.Time >= d.From && marker.Time <= contentEnd)
				{
					marker.Time = TimelineAt(project, old.ToRecordingTime(marker.Time), d.From, d.To);
				}
			}

			std::stable_sort(project.Markers.begin(), project.Markers.end(),
				[](const ReplayMarker& a, const ReplayMarker& b)
				{
					return a.Time < b.Time;
				});
		}

		return result;
	}

	const char* ToString(TimeScaleStyle style)
	{
		return style == TimeScaleStyle::Punchy ? "punchy" : "calm";
	}

	bool ParseTimeScaleStyle(std::string_view text, TimeScaleStyle& style)
	{
		if (text != "calm" && text != "punchy")
		{
			return false;
		}

		style = text == "calm" ? TimeScaleStyle::Calm : TimeScaleStyle::Punchy;
		return true;
	}

	std::vector<TrackKey<float>> SuggestTimeScale(const TimeScaleSuggestDesc& d)
	{
		std::vector<TrackKey<float>> keys;

		if (!std::isfinite(d.From) || !std::isfinite(d.To) || !(d.To > d.From))
		{
			return keys;
		}

		std::vector<double> beats;

		for (const double b : d.Beats)
		{
			if (b >= d.From && b <= d.To)
			{
				beats.push_back(b);
			}
		}

		std::sort(beats.begin(), beats.end());
		double beat = 0.5;

		if (beats.size() >= 2)
		{
			std::vector<double> gaps;

			for (std::size_t i = 1; i < beats.size(); ++i)
			{
				gaps.push_back(beats[i] - beats[i - 1]);
			}

			std::nth_element(gaps.begin(), gaps.begin() + static_cast<std::ptrdiff_t>(gaps.size() / 2), gaps.end());
			beat = std::clamp(gaps[gaps.size() / 2], 0.2, 2.0);
		}

		// Mean onset envelope over a window around a timeline time (0.5 without analysis).
		const auto energy = [&](double t, double window)
		{
			if (!d.Onsets || d.Onsets->Envelope.empty() || d.Onsets->FrameRate <= 0.0)
			{
				return 0.5;
			}

			const auto& env = d.Onsets->Envelope;
			const double music = t - d.MusicOffset;
			const auto first = static_cast<std::ptrdiff_t>(std::floor((music - window * 0.5) * d.Onsets->FrameRate));
			const auto last = static_cast<std::ptrdiff_t>(std::ceil((music + window * 0.5) * d.Onsets->FrameRate));
			double sum = 0.0;
			std::size_t count = 0;

			for (std::ptrdiff_t i = std::max<std::ptrdiff_t>(first, 0); i <= last && i < static_cast<std::ptrdiff_t>(env.size()); ++i)
			{
				sum += env[static_cast<std::size_t>(i)];
				++count;
			}

			return count ? sum / static_cast<double>(count) : 0.0;
		};

		const auto add = [&](double t, double value, Ease ease)
		{
			if (t >= d.From - 1.0e-9 && t <= d.To + 1.0e-9)
			{
				keys.push_back({ t, static_cast<float>(value), ease });
			}
		};

		if (d.Style == TimeScaleStyle::Calm)
		{
			const double low = d.Low > 0.0f ? d.Low : 0.6;
			const double high = std::max(d.High > 0.0f ? static_cast<double>(d.High) : 1.0, low);
			std::vector<double> times{ d.From, d.To };

			for (std::size_t i = 0; i < beats.size(); i += 2)
			{
				times.push_back(beats[i]);
			}

			if (beats.empty())
			{
				for (double t = d.From + 1.0; t < d.To; t += 1.0)
				{
					times.push_back(t);
				}
			}

			for (const double s : d.Sections)
			{
				times.push_back(s);
			}

			std::sort(times.begin(), times.end());
			times.erase(std::unique(times.begin(), times.end(),
							[](double a, double b)
							{
								return std::abs(a - b) < 0.05;
							}),
				times.end());
			std::vector<double> levels;
			double top = 0.0;

			for (const double t : times)
			{
				levels.push_back(energy(t, 2.0));
				top = std::max(top, levels.back());
			}

			for (std::size_t i = 0; i < times.size(); ++i)
			{
				const double e = top > 0.0 ? levels[i] / top : 0.5;
				add(times[i], low + (high - low) * e, Ease::Smooth);
			}
		}
		else
		{
			const double low = d.Low > 0.0f ? d.Low : 0.3;
			const double high = std::max(d.High > 0.0f ? static_cast<double>(d.High) : 1.25, low);
			const double base = std::clamp(1.0, low, high);

			// Section starts first, then the strongest onsets, snapped to beats and at least
			// four beats apart.
			std::vector<double> hits;
			const auto accept = [&](double t)
			{
				t = Snap(t, beats, beat * 0.5);

				if (t > d.From + beat && t < d.To - beat &&
					std::none_of(hits.begin(), hits.end(),
						[&](double h)
						{
							return std::abs(h - t) < 4.0 * beat;
						}))
				{
					hits.push_back(t);
				}
			};

			for (const double s : d.Sections)
			{
				accept(s);
			}

			if (d.Onsets)
			{
				std::vector<std::pair<float, double>> strong;

				for (std::size_t i = 0; i < d.Onsets->Onsets.size() && i < d.Onsets->Strength.size(); ++i)
				{
					const double t = d.Onsets->Onsets[i] + d.MusicOffset;

					if (t >= d.From && t <= d.To)
					{
						strong.emplace_back(d.Onsets->Strength[i], t);
					}
				}

				std::sort(strong.begin(), strong.end(), std::greater<>());
				const std::size_t keep = (strong.size() + 4) / 5; // The top fifth.

				for (std::size_t i = 0; i < keep; ++i)
				{
					accept(strong[i].second);
				}
			}

			std::sort(hits.begin(), hits.end());
			add(d.From, base, Ease::Smooth);

			for (const double h : hits)
			{
				add(h - 0.25 * beat, base, Ease::Out);
				add(h, low, Ease::Smooth);
				add(h + beat, high, Ease::Smooth);
				add(h + 2.0 * beat, base, Ease::Smooth);
			}

			add(d.To, base, Ease::Smooth);
		}

		std::sort(keys.begin(), keys.end(),
			[](const TrackKey<float>& a, const TrackKey<float>& b)
			{
				return a.Time < b.Time;
			});
		keys.erase(std::unique(keys.begin(), keys.end(),
					   [](const TrackKey<float>& a, const TrackKey<float>& b)
					   {
						   return std::abs(a.Time - b.Time) < 1.0e-3;
					   }),
			keys.end());
		return keys;
	}

} // namespace Swim::Replay
