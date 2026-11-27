#include "Engine/Systems/Replay/ReplayToolsInternal.h"

#include "Engine/Systems/Replay/ReplayAudio.h"
#include "Engine/Systems/Replay/ReplayEditing.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <span>

namespace Swim::Replay::Internal
{

	namespace
	{

		void AddMusicSetTool(const std::shared_ptr<Context>& c, const AddTool& add, const EditProject& edit)
		{
			add("music.set",
				"Sets the project music track: a wav, ogg, mp3 or flac file whose second 0 plays at timeline time offset. clear: true "
				"removes it.",
				Schema::Object(
					{ { "path", Schema::String("Audio file.") }, { "offset", Schema::Number("Timeline time of the song's start.") },
						{ "volume", Schema::Number("0..1 (default 1).") }, { "clear", Schema::Boolean("Remove the track.") } }),
				[c, edit](const Json& args)
				{
					const bool clear = args["clear"].AsBool(false);
					MusicTrack track = c->Player->GetProject().Music.value_or(MusicTrack{});
					track.Path = args["path"].AsString(track.Path);
					track.Offset = args["offset"].AsNumber(track.Offset);
					track.Volume = std::clamp(args["volume"].AsFloat(track.Volume), 0.0f, 4.0f);

					if (!clear && track.Path.empty())
					{
						throw ToolError("music path is empty");
					}

					edit(
						[&](ReplayProject& project)
						{
							if (clear)
							{
								project.Music.reset();
							}
							else
							{
								project.Music = track;
							}
						});
					return Ok(*c->Player);
				});
		}

		void AddMusicAnalyzeTool(
			const std::shared_ptr<Context>& c, const AddTool& add, const EditProject& edit, const std::shared_ptr<MusicCache>& music)
		{
			add("music.analyze",
				"Decodes the music and finds onsets, the tempo and beat grid, and sections, writing them as markers (timeline times, "
				"kinds beat, downbeat, section and optionally onset; earlier markers of those kinds are replaced).",
				Schema::Object({ { "sensitivity", Schema::Number("Onset sensitivity (default 1; higher finds more).") },
					{ "minBpm", Schema::Number("Tempo range low (default 70).") },
					{ "maxBpm", Schema::Number("Tempo range high (default 180).") }, { "beatsPerBar", Schema::Integer("Default 4.") },
					{ "minSectionSeconds", Schema::Number("Shortest section (default 8).") },
					{ "markers",
						Schema::ArrayOf(Schema::Enum({ "beat", "downbeat", "section", "onset" }, "Kind."),
							"Marker kinds to write (default beat, downbeat, section; [] writes none).") } }),
				[c, edit, music](const Json& args)
				{
					MusicCache& m = LoadMusic(*c, *music);
					m.Onsets = DetectOnsets(m.Mono, m.Rate, args["sensitivity"].AsFloat(1.0f));
					m.Beats = EstimateBeats(*m.Onsets, args["minBpm"].AsNumber(70.0), args["maxBpm"].AsNumber(180.0),
						static_cast<std::uint32_t>(std::clamp<std::int64_t>(args["beatsPerBar"].AsInt(4), 1, 16)));
					m.Sections = DetectSections(m.Mono, m.Rate, *m.Beats, args["minSectionSeconds"].AsNumber(8.0));
					std::vector<std::string> kinds{ "beat", "downbeat", "section" };

					if (args.Contains("markers"))
					{
						kinds.clear();

						for (const auto& k : args["markers"].GetArray())
						{
							kinds.push_back(k.AsString());
						}
					}

					const double offset = c->Player->GetProject().Music->Offset;
					const auto timesOf = [&](const std::string& kind) -> const std::vector<double>*
					{
						if (kind == "beat")
						{
							return &m.Beats->Beats;
						}

						if (kind == "downbeat")
						{
							return &m.Beats->Downbeats;
						}

						if (kind == "section")
						{
							return &m.Sections;
						}

						if (kind == "onset")
						{
							return &m.Onsets->Onsets;
						}

						return nullptr;
					};

					for (const std::string& kind : kinds)
					{
						if (!timesOf(kind))
						{
							throw ToolError("unknown marker kind '" + kind + "'");
						}
					}

					edit(
						[&](ReplayProject& project)
						{
							for (const std::string& kind : kinds)
							{
								const std::vector<double>* times = timesOf(kind);
								project.RemoveMarkers(kind);
								std::size_t n = 0;

								for (const double t : *times)
								{
									project.AddMarker(t + offset, kind + " " + std::to_string(++n), kind);
								}
							}
						});

					Json r = Json::MakeObject();
					r["duration"] = m.Onsets->Duration;
					r["bpm"] = m.Beats->Bpm;
					r["confidence"] = m.Beats->Confidence;
					r["firstBeat"] = m.Beats->Phase + offset;
					r["beats"] = static_cast<std::uint64_t>(m.Beats->Beats.size());
					r["onsets"] = static_cast<std::uint64_t>(m.Onsets->Onsets.size());
					r["downbeats"] = Times(m.Beats->Downbeats, offset);
					r["sections"] = Times(m.Sections, offset);
					return r;
				});
		}

		void AddMusicPeaksTool(const std::shared_ptr<Context>& c, const AddTool& add, const std::shared_ptr<MusicCache>& music)
		{
			add("music.peaks",
				"Waveform min/max per bucket for drawing, over a timeline range (default the whole song). Bucket i starts at "
				"start + i / bucketsPerSecond.",
				Schema::Object({ { "bucketsPerSecond", Schema::Number("Default 100.") }, { "from", Schema::Number("Timeline start.") },
					{ "to", Schema::Number("Timeline end.") } }),
				[c, music](const Json& args)
				{
					MusicCache& m = LoadMusic(*c, *music);
					const double offset = c->Player->GetProject().Music->Offset;
					const double duration = static_cast<double>(m.Mono.size()) / m.Rate;
					const double from = std::clamp(args["from"].AsNumber(offset) - offset, 0.0, duration);
					const double to = std::clamp(args["to"].AsNumber(offset + duration) - offset, from, duration);
					const double buckets = std::clamp(args["bucketsPerSecond"].AsNumber(100.0), 1.0, 2000.0);

					if ((to - from) * buckets > 400000.0)
					{
						throw ToolError("too many buckets: lower bucketsPerSecond or narrow the range");
					}

					const auto first = static_cast<std::size_t>(from * m.Rate);
					const auto last = std::min(m.Mono.size(), static_cast<std::size_t>(to * m.Rate));
					const AudioPeaks peaks = ComputePeaks(std::span<const float>(m.Mono).subspan(first, last - first), m.Rate, buckets);
					Json mins = Json::MakeArray();
					Json maxs = Json::MakeArray();

					for (std::size_t i = 0; i < peaks.Min.size(); ++i)
					{
						mins.Push(peaks.Min[i]);
						maxs.Push(peaks.Max[i]);
					}

					Json r = Json::MakeObject();
					r["start"] = from + offset;
					r["bucketsPerSecond"] = peaks.BucketsPerSecond;
					r["min"] = std::move(mins);
					r["max"] = std::move(maxs);
					return r;
				});
		}

		void AddMusicSyncRegionTool(const std::shared_ptr<Context>& c, const AddTool& add, const EditProject& edit)
		{
			add("music.sync_region",
				"Fits the time scale over a song region so chosen moments land on beats. Over [from, to] (timeline; where the music "
				"plays) the content now shown between from and contentTo plays instead, retimed by a smooth speed curve (or constant "
				"speeds between anchors with style constant) that passes each event exactly on its beat. pairs: [[beat, event], ...] "
				"(times or marker names; event times are current timeline times); without pairs, markers of the events kind land on "
				"the nearest beat marker within maxShift. Times before from stay put; event markers move with their content. Returns "
				"the anchors, speeds and the largest miss.",
				Schema::Object({ { "from", Schema::String("Region start: seconds or a marker name.") },
								   { "to", Schema::String("Region end: seconds or a marker name.") },
								   { "contentTo", Schema::String("Current timeline time the region ends on (default: to).") },
								   { "pairs",
									   Schema::ArrayOf(
										   Schema::ArrayOf(Schema::String("Seconds or marker name."), "[beat, event]"), "Exact anchors.") },
								   { "beats", Schema::String("Beat marker kind for auto-matching (default beat).") },
								   { "events", Schema::String("Event marker kind for auto-matching (default user).") },
								   { "maxShift", Schema::Number("Auto-matching reach in seconds (default 0.5).") },
								   { "style", Schema::Enum({ "smooth", "constant" }, "Speed curve (default smooth).") },
								   { "minScale", Schema::Number("Slowest speed (default 0.05).") },
								   { "maxScale", Schema::Number("Fastest speed (default 8).") },
								   { "moveMarkers", Schema::Boolean("Move event markers with their content (default true).") } },
					{ "from", "to" }),
				[c, edit](const Json& args)
				{
					const ReplayProject& current = c->Player->GetProject();
					const auto timeOf = [&](const Json& v, const char* what)
					{
						// Numbers may also arrive as strings.
						if (v.IsString() && !v.AsString().empty())
						{
							char* end = nullptr;
							const double number = std::strtod(v.AsString().c_str(), &end);

							if (end && *end == '\0')
							{
								return number;
							}
						}

						Json holder = Json::MakeObject();
						holder[what] = v;
						return ReadTimeOrMarker(holder, what, current);
					};

					RegionSyncDesc d;
					d.From = timeOf(args["from"], "from");
					d.To = timeOf(args["to"], "to");
					d.ContentTo = args.Contains("contentTo") ? timeOf(args["contentTo"], "contentTo") : -1.0;
					const std::string eventKind = args["events"].AsString("user");

					for (const auto& pair : args["pairs"].GetArray())
					{
						if (!pair.IsArray() || pair.Size() != 2)
						{
							throw ToolError("each pair is [beat, event]");
						}

						d.Pairs.emplace_back(timeOf(pair[0], "beat"), timeOf(pair[1], "event"));
					}

					if (d.Pairs.empty())
					{
						d.Beats = MarkerTimes(current, args["beats"].AsString("beat"));
						d.Events = MarkerTimes(current, eventKind);

						if (d.Beats.empty())
						{
							throw ToolError("no beat markers (run music.analyze) and no pairs");
						}
					}

					d.MaxShift = args["maxShift"].AsNumber(d.MaxShift);
					d.Smooth = args["style"].AsString("smooth") != "constant";
					d.MinScale = args["minScale"].AsFloat(d.MinScale);
					d.MaxScale = args["maxScale"].AsFloat(d.MaxScale);
					d.RetimeMarkerKind = args["moveMarkers"].AsBool(true) ? eventKind : std::string();
					const double recording = c->Player->GetReader().GetDuration();

					// Fitted on a copy first, so a region that cannot fit leaves no undo step.
					ReplayProject trial;
					trial.TimeScale = current.TimeScale;
					trial.TimeRemap = current.TimeRemap;
					trial.Markers = current.Markers;
					RegionSyncResult result;

					try
					{
						result = SyncRegion(trial, d, recording);
					}
					catch (const std::invalid_argument& e)
					{
						throw ToolError(e.what());
					}

					edit(
						[&](ReplayProject& project)
						{
							project.TimeScale = trial.TimeScale;
							project.TimeRemap = trial.TimeRemap;
							project.Markers = trial.Markers;
						});

					Json anchors = Json::MakeArray();

					for (const auto& [t, r] : result.Anchors)
					{
						Json a = Json::MakeArray();
						a.Push(t);
						a.Push(r);
						anchors.Push(std::move(a));
					}

					Json r = Ok(*c->Player);
					r["anchors"] = std::move(anchors);
					r["dropped"] = static_cast<std::uint64_t>(result.Dropped);
					r["minSpeed"] = result.MinSpeed;
					r["maxSpeed"] = result.MaxSpeed;
					r["maxError"] = result.MaxError;
					r["length"] = c->Player->GetLength();
					return r;
				});
		}

		void AddMusicSuggestTimescaleTool(
			const std::shared_ptr<Context>& c, const AddTool& add, const EditProject& edit, const std::shared_ptr<MusicCache>& music)
		{
			add("music.suggest_timescale",
				"Suggests a time scale from the music over a timeline range (default the whole song): calm follows the music's energy "
				"(slower in quiet passages, smooth); punchy keeps real time with slow-motion hits on section starts and the strongest "
				"onsets, snapped to beats. Applies it (replacing keys in the range, then rebaking) unless apply is false. Returns the "
				"keys.",
				Schema::Object({ { "from", Schema::Number("Range start (timeline seconds).") }, { "to", Schema::Number("Range end.") },
					{ "style", Schema::Enum({ "calm", "punchy" }, "Default calm.") },
					{ "low", Schema::Number("Slowest speed (default: calm 0.6, punchy 0.3).") },
					{ "high", Schema::Number("Fastest speed (default: calm 1, punchy 1.25).") },
					{ "apply", Schema::Boolean("Write the keys (default true).") } }),
				[c, edit, music](const Json& args)
				{
					MusicCache& m = LoadMusic(*c, *music);

					if (!m.Onsets)
					{
						m.Onsets = DetectOnsets(m.Mono, m.Rate);
					}

					if (!m.Beats)
					{
						m.Beats = EstimateBeats(*m.Onsets);
						m.Sections = DetectSections(m.Mono, m.Rate, *m.Beats);
					}

					const double offset = c->Player->GetProject().Music->Offset;
					TimeScaleSuggestDesc d;

					if (!ParseTimeScaleStyle(args["style"].AsString("calm"), d.Style))
					{
						throw ToolError("style must be calm or punchy");
					}

					d.From = args["from"].AsNumber(std::max(offset, 0.0));
					d.To = args["to"].AsNumber(offset + m.Onsets->Duration);
					d.Onsets = &*m.Onsets;
					d.MusicOffset = offset;
					d.Low = std::max(args["low"].AsFloat(0.0f), 0.0f);
					d.High = std::max(args["high"].AsFloat(0.0f), 0.0f);

					for (const double b : m.Beats->Beats)
					{
						d.Beats.push_back(b + offset);
					}

					for (const double s : m.Sections)
					{
						d.Sections.push_back(s + offset);
					}

					if (!std::isfinite(d.From) || !std::isfinite(d.To) || !(d.To > d.From))
					{
						throw ToolError("the range is empty");
					}

					const std::vector<TrackKey<float>> keys = SuggestTimeScale(d);
					const bool apply = args["apply"].AsBool(true);

					if (apply && !keys.empty())
					{
						const double recording = c->Player->GetReader().GetDuration();
						edit(
							[&](ReplayProject& project)
							{
								std::erase_if(project.TimeScale.Keys,
									[&](const TrackKey<float>& k)
									{
										return k.Time >= d.From && k.Time <= d.To;
									});

								for (const auto& k : keys)
								{
									project.TimeScale.Set(k.Time, k.Data, k.Interp);
								}

								project.BakeTimeScale(recording);
							});
					}

					Json list = Json::MakeArray();

					for (const auto& k : keys)
					{
						Json j = Json::MakeArray();
						j.Push(k.Time);
						j.Push(k.Data);
						j.Push(ToString(k.Interp));
						list.Push(std::move(j));
					}

					Json r = Ok(*c->Player);
					r["style"] = ToString(d.Style);
					r["keys"] = std::move(list);
					r["applied"] = apply && !keys.empty();
					r["length"] = c->Player->GetLength();
					return r;
				});
		}

		void AddSyncCutsToBeatsTool(const std::shared_ptr<Context>& c, const AddTool& add, const EditProject& edit)
		{
			add("sync.cuts_to_beats",
				"Cuts between cameras on the music: consecutive shots from markers of a kind (beat, downbeat, section, onset, or any "
				"kind; run music.analyze first) or explicit times, every n-th one, rotating through sources (shot names used as "
				"templates, or rig names). Shots inside the range are replaced.",
				Schema::Object(
					{ { "sources", Schema::ArrayOf(Schema::String("Shot or rig name."), "Cameras to rotate through.") },
						{ "on", Schema::String("Marker kind (default downbeat).") },
						{ "times", Schema::ArrayOf(Schema::Number("Seconds."), "Explicit cut times instead.") },
						{ "every", Schema::Integer("Cut on every n-th time (default 1).") }, { "from", Schema::Number("Range start.") },
						{ "to", Schema::Number("Range end.") }, { "prefix", Schema::String("Shot name prefix (default 'cut').") } },
					{ "sources" }),
				[c, edit](const Json& args)
				{
					BeatCutDesc d;

					for (const auto& s : args["sources"].GetArray())
					{
						d.Sources.push_back(s.AsString());
					}

					if (d.Sources.empty())
					{
						throw ToolError("sources is empty");
					}

					if (args.Contains("times"))
					{
						for (const auto& t : args["times"].GetArray())
						{
							d.Times.push_back(t.AsNumber());
						}
					}
					else
					{
						d.Times = MarkerTimes(c->Player->GetProject(), args["on"].AsString("downbeat"));
					}

					d.Every = static_cast<std::uint32_t>(std::clamp<std::int64_t>(args["every"].AsInt(1), 1, 1024));
					d.From = args["from"].AsNumber(d.From);
					d.To = args["to"].AsNumber(d.To);
					d.Prefix = args["prefix"].AsString(d.Prefix);
					std::size_t created = 0;
					edit(
						[&](ReplayProject& project)
						{
							created = CutsToBeats(project, d);
						});

					if (created == 0)
					{
						throw ToolError("fewer than two cut times in range (analyze the music or pass times)");
					}

					Json r = Ok(*c->Player);
					r["shots"] = static_cast<std::uint64_t>(created);
					return r;
				});
		}

		void AddSnapTool(const std::shared_ptr<Context>& c, const AddTool& add)
		{
			add("snap",
				"Snaps a time to the nearest marker, shot edge, cut or jump key within tolerance (or only markers of the given kinds).",
				Schema::Object({ { "time", Schema::Number("Seconds.") }, { "tolerance", Schema::Number("Seconds (default 0.1).") },
								   { "kinds", Schema::ArrayOf(Schema::String("Marker kind."), "Only these marker kinds.") } },
					{ "time" }),
				[c](const Json& args)
				{
					const auto& project = c->Player->GetProject();
					std::vector<double> targets;

					if (args.Contains("kinds"))
					{
						for (const auto& k : args["kinds"].GetArray())
						{
							const auto times = MarkerTimes(project, k.AsString());
							targets.insert(targets.end(), times.begin(), times.end());
						}

						std::sort(targets.begin(), targets.end());
					}
					else
					{
						targets = CollectSnapTargets(project);
					}

					const double t = args["time"].AsNumber();
					const double snapped = Snap(t, targets, std::max(args["tolerance"].AsNumber(0.1), 0.0));
					Json r = Json::MakeObject();
					r["time"] = snapped;
					r["snapped"] = snapped != t;
					return r;
				});
		}

	} // namespace

	void RegisterMusicTools(const std::shared_ptr<Context>& c, const AddTool& add, const EditProject& edit)
	{
		auto music = std::make_shared<MusicCache>();

		// ---- Music -----------------------------------------------------------------------

		AddMusicSetTool(c, add, edit);
		AddMusicAnalyzeTool(c, add, edit, music);
		AddMusicPeaksTool(c, add, music);
		AddMusicSyncRegionTool(c, add, edit);
		AddMusicSuggestTimescaleTool(c, add, edit, music);

		// ---- Sync --------------------------------------------------------------------------

		AddSyncCutsToBeatsTool(c, add, edit);
		AddSnapTool(c, add);
	}

} // namespace Swim::Replay::Internal
