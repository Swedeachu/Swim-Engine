#include "Engine/Systems/Replay/ReplayTools.h"

#include "Engine/Systems/Replay/ReplayAudio.h"
#include "Engine/Systems/Replay/ReplayEditing.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <span>

namespace Swim::Replay
{

	using Tooling::Json;
	using Tooling::ToolError;
	namespace Schema = Tooling::Schema;

	namespace
	{

		struct Context
		{
			Tooling::ToolServer* Server = nullptr;
			ReplayPlayer* Player = nullptr;
			ReplayToolsDesc Desc;
		};

		NodeId RequireNode(const Json& args, const char* field = "node")
		{
			NodeId id = NoNode;

			if (!ParseNodeId(args[field], id) || id == NoNode)
			{
				throw ToolError(std::string("missing or invalid '") + field + "' (a node id string)");
			}

			return id;
		}

		NodeId OptionalNode(const Json& args, const char* field)
		{
			NodeId id = NoNode;

			if (args.Contains(field) && !args[field].IsNull() && !ParseNodeId(args[field], id))
			{
				throw ToolError(std::string("invalid '") + field + "'");
			}

			return id;
		}

		Ease ReadEase(const Json& args)
		{
			Ease ease = Ease::Linear;

			if (args.Contains("ease") && !ParseEase(args["ease"].AsString(), ease))
			{
				throw ToolError("ease must be step, linear, smooth, in, out or spline");
			}

			return ease;
		}

		double ReadTime(const Json& args, const ReplayPlayer& player)
		{
			return args.Contains("time") ? args["time"].AsNumber() : player.GetTime();
		}

		std::optional<Quat> ReadRotation(const Json& args)
		{
			if (args.Contains("rotation"))
			{
				if (!args["rotation"].IsArray() || args["rotation"].Size() != 4)
				{
					throw ToolError("rotation is a quaternion [x, y, z, w]");
				}

				return QuatFromJson(args["rotation"]);
			}

			if (args.Contains("euler"))
			{
				const Vec3 e = Vec3FromJson(args["euler"]);
				return EulerRotation(e[0], e[1], e[2]);
			}

			return std::nullopt;
		}

		std::optional<Vec3> ReadVec3(const Json& args, const char* field)
		{
			if (!args.Contains(field))
			{
				return std::nullopt;
			}

			if (!args[field].IsArray() || args[field].Size() != 3)
			{
				throw ToolError(std::string(field) + " is [x, y, z]");
			}

			return Vec3FromJson(args[field]);
		}

		AssetRef ReadAssetRef(const Json& j)
		{
			AssetRef ref;

			if (!ParseAssetKind(j["kind"].AsString("mesh"), ref.Kind))
			{
				throw ToolError("asset kind must be mesh, texture, material, sound, effect or other");
			}

			ref.Key = j["key"].AsString();

			if (ref.Key.empty())
			{
				throw ToolError("asset key is empty");
			}

			return ref;
		}

		Json Ok(ReplayPlayer& player)
		{
			Json result = Json::MakeObject();
			result["ok"] = true;
			result["time"] = player.GetTime();
			return result;
		}

		std::filesystem::path Resolve(const Context& c, const std::string& path)
		{
			std::filesystem::path p(path);
			return p.is_relative() && !c.Desc.Root.empty() ? c.Desc.Root / p : p;
		}

		Json TimeSchema()
		{
			return Schema::Number("Timeline seconds (default: the current time).");
		}

		Json EaseSchema()
		{
			return Schema::Enum({ "step", "linear", "smooth", "in", "out", "spline", "bezier" }, "How this key moves to the next (default linear).");
		}

		Json NodeSchema(std::string_view what = "Node id (string).")
		{
			return Schema::String(what);
		}

		template <typename T> Json KeyCount(const Track<T>& track)
		{
			return static_cast<std::uint64_t>(track.Keys.size());
		}

	} // namespace

	Json DescribeNode(ReplayPlayer& player, const NodeState& node, bool withProps)
	{
		auto& names = player.GetNames();
		Json j = Json::MakeObject();
		j["id"] = FormatNodeId(node.Id);
		j["kind"] = names.Get(node.Kind);
		j["visible"] = node.Visible;
		j["position"] = ToJson(node.Pose.Position);
		j["rotation"] = ToJson(node.Pose.Rotation);
		j["scale"] = ToJson(node.Pose.Scale);

		if (node.Parent != NoNode)
		{
			j["parent"] = FormatNodeId(node.Parent);
		}

		if (const auto* edit = [&]() -> const NodeEdit*
			{
				const auto found = player.GetProject().Nodes.find(node.Id);
				return found == player.GetProject().Nodes.end() ? nullptr : &found->second;
			}();
			edit && !edit->Label.empty())
		{
			j["label"] = edit->Label;
		}

		Json assets = Json::MakeArray();

		for (std::size_t slot = 0; slot < node.Assets.size(); ++slot)
		{
			Json a = Json::MakeObject();
			a["slot"] = static_cast<std::uint64_t>(slot);

			if (const AssetRecord* record = player.GetAsset(node.Assets[slot]))
			{
				a["kind"] = ToString(record->Kind);
				a["key"] = record->Key;
				a["cached"] = record->HasData;
			}

			assets.Push(std::move(a));
		}

		j["assets"] = std::move(assets);

		if (withProps)
		{
			Json props = Json::MakeObject();

			for (const auto& p : node.Props)
			{
				props[names.Get(p.Key)] = ToJson(p.Data);
			}

			j["props"] = std::move(props);
		}

		return j;
	}

	namespace
	{

		std::vector<double> MarkerTimes(const ReplayProject& project, std::string_view kind, std::string_view name = {})
		{
			std::vector<double> times;

			for (const auto& m : project.Markers)
			{
				if ((kind.empty() || m.Kind == kind) && (name.empty() || m.Name == name))
				{
					times.push_back(m.Time);
				}
			}

			std::sort(times.begin(), times.end());
			return times;
		}

		// A time given as seconds or as a marker name.
		double ReadTimeOrMarker(const Json& args, const char* field, const ReplayProject& project)
		{
			const Json& v = args[field];

			if (v.IsNumber())
			{
				return v.AsNumber();
			}

			if (v.IsString())
			{
				for (const auto& m : project.Markers)
				{
					if (m.Name == v.AsString())
					{
						return m.Time;
					}
				}

				throw ToolError(std::string("no marker named '") + v.AsString() + "' for '" + field + "'");
			}

			throw ToolError(std::string("'") + field + "' is a time in seconds or a marker name");
		}

		std::vector<Vec3> ReadPoints(const Json& args, const char* field)
		{
			std::vector<Vec3> points;

			for (const auto& p : args[field].GetArray())
			{
				if (!p.IsArray() || p.Size() != 3)
				{
					throw ToolError(std::string(field) + " is a list of [x, y, z]");
				}

				points.push_back(Vec3FromJson(p));
			}

			return points;
		}

		Json Times(const std::vector<double>& times, double offset = 0.0)
		{
			Json list = Json::MakeArray();

			for (const double t : times)
			{
				list.Push(t + offset);
			}

			return list;
		}

		Json PathSummary(const std::string& name, const CameraPath& path)
		{
			Json j = path.ToJson();
			j["name"] = name;
			j["length"] = path.Length();
			return j;
		}

		Json ShotJson(const Shot& shot)
		{
			Json j = Json::MakeObject();
			j["name"] = shot.Name;
			j["camera"] = shot.Camera;
			j["start"] = shot.Start;
			j["end"] = shot.End;

			if (!shot.Path.empty())
			{
				j["path"] = shot.Path;
				j["progressFrom"] = shot.ProgressFrom;
				j["progressTo"] = shot.ProgressTo;
				j["progressEase"] = ToString(shot.ProgressEase);
			}

			return j;
		}

		Json EaseOrSmoothSchema()
		{
			return Schema::Enum({ "step", "linear", "smooth", "in", "out", "spline", "bezier" }, "Easing (default smooth).");
		}

		Ease ReadEaseOr(const Json& args, Ease fallback)
		{
			return args.Contains("ease") ? ReadEase(args) : fallback;
		}

		// Decoded music and what was found in it, kept for the current path.
		struct MusicCache
		{
			std::string Path;
			std::vector<float> Mono;
			double Rate = 0.0;
			std::optional<OnsetAnalysis> Onsets;
			std::optional<BeatGrid> Beats;
			std::vector<double> Sections;
		};

		MusicCache& LoadMusic(Context& c, MusicCache& cache)
		{
			const auto& music = c.Player->GetProject().Music;

			if (!music || music->Path.empty())
			{
				throw ToolError("no music track (music.set first)");
			}

			if (cache.Path == music->Path && !cache.Mono.empty())
			{
				return cache;
			}

			if (!c.Desc.LoadMusic)
			{
				throw ToolError("this host cannot decode audio (ReplayToolsDesc::LoadMusic is unset)");
			}

			MusicCache fresh;
			fresh.Path = music->Path;
			std::string error;

			if (!c.Desc.LoadMusic(Resolve(c, music->Path), fresh.Mono, fresh.Rate, error) || fresh.Rate <= 0.0)
			{
				throw ToolError("cannot load music: " + error);
			}

			cache = std::move(fresh);
			return cache;
		}

		void RegisterEditingTools(const std::shared_ptr<Context>& c,
			const std::function<void(std::string, std::string, Json, std::function<Json(const Json&)>)>& add,
			const std::function<void(const std::function<void(ReplayProject&)>&)>& edit)
		{
			auto music = std::make_shared<MusicCache>();

			// ---- Paths -----------------------------------------------------------------

			add("path.set",
				"Creates or replaces a named spline path. kind: linear, catmull_rom, centripetal (no loops or cusps; the default) or "
				"bezier. points: [x, y, z] or { position, in, out } with tangent offsets (bezier; auto when omitted).",
				Schema::Object({ { "name", Schema::String("Path name.") },
								   { "kind", Schema::Enum({ "linear", "catmull_rom", "centripetal", "bezier" }, "Interpolation.") },
								   { "closed", Schema::Boolean("Loop back to the first point.") },
								   { "points", Schema::ArrayOf(Json::MakeObject(), "Control points.") } },
					{ "name", "points" }),
				[c, edit](const Json& args)
				{
					const std::string name = args["name"].AsString();

					if (name.empty())
					{
						throw ToolError("path name is empty");
					}

					CameraPath path;

					try
					{
						Json source = args;

						if (!args.Contains("kind"))
						{
							source["kind"] = "centripetal";
						}

						path = CameraPath::FromJson(source);
					}
					catch (const std::exception& e)
					{
						throw ToolError(e.what());
					}

					if (path.Points.size() < 2)
					{
						throw ToolError("a path needs at least two points");
					}

					edit(
						[&](ReplayProject& project)
						{
							project.EditPath(name) = path;
						});
					Json r = Ok(*c->Player);
					r["length"] = path.Length();
					return r;
				});

			add("path.get", "A path's points and length, with a sampled polyline for drawing.",
				Schema::Object({ { "name", Schema::String("Path name.") },
								   { "samples", Schema::Integer("Polyline samples per segment (default 16).") } },
					{ "name" }),
				[c](const Json& args)
				{
					const auto& paths = c->Player->GetProject().Paths;
					const auto found = paths.find(args["name"].AsString());

					if (found == paths.end())
					{
						throw ToolError("no path named '" + args["name"].AsString() + "'");
					}

					Json r = PathSummary(found->first, found->second);
					Json line = Json::MakeArray();

					for (const Vec3& p :
						found->second.Polyline(static_cast<std::uint32_t>(std::clamp<std::int64_t>(args["samples"].AsInt(16), 1, 256))))
					{
						line.Push(ToJson(p));
					}

					r["polyline"] = std::move(line);
					return r;
				});

			add("path.list", "Every path with its kind, point count and length.", Schema::Object(),
				[c](const Json&)
				{
					Json list = Json::MakeArray();

					for (const auto& [name, path] : c->Player->GetProject().Paths)
					{
						Json j = Json::MakeObject();
						j["name"] = name;
						j["kind"] = ToString(path.Kind);
						j["closed"] = path.Closed;
						j["points"] = static_cast<std::uint64_t>(path.Points.size());
						j["length"] = path.Length();
						list.Push(std::move(j));
					}

					Json r = Json::MakeObject();
					r["paths"] = std::move(list);
					return r;
				});

			add("path.remove", "Deletes a path (rigs and shots using it fall back to their keys).",
				Schema::Object({ { "name", Schema::String("Path name.") } }, { "name" }),
				[c, edit](const Json& args)
				{
					bool removed = false;
					edit(
						[&](ReplayProject& project)
						{
							removed = project.Paths.erase(args["name"].AsString()) > 0;
						});
					Json r = Ok(*c->Player);
					r["removed"] = removed;
					return r;
				});

			add("path.sample", "Positions and directions at evenly spaced arc-length fractions (or at given fractions).",
				Schema::Object(
					{ { "name", Schema::String("Path name.") }, { "count", Schema::Integer("Evenly spaced samples (default 16).") },
						{ "fractions", Schema::ArrayOf(Schema::Number("0..1"), "Arc-length fractions.") } },
					{ "name" }),
				[c](const Json& args)
				{
					const auto& paths = c->Player->GetProject().Paths;
					const auto found = paths.find(args["name"].AsString());

					if (found == paths.end())
					{
						throw ToolError("no path named '" + args["name"].AsString() + "'");
					}

					std::vector<float> fractions;

					if (args.Contains("fractions"))
					{
						for (const auto& f : args["fractions"].GetArray())
						{
							fractions.push_back(std::clamp(f.AsFloat(), 0.0f, 1.0f));
						}
					}
					else
					{
						const auto count = static_cast<int>(std::clamp<std::int64_t>(args["count"].AsInt(16), 2, 1024));

						for (int i = 0; i < count; ++i)
						{
							fractions.push_back(static_cast<float>(i) / static_cast<float>(count - 1));
						}
					}

					Json samples = Json::MakeArray();

					for (const float f : fractions)
					{
						Json j = Json::MakeObject();
						j["fraction"] = f;
						j["position"] = ToJson(found->second.Sample(f));
						j["direction"] = ToJson(found->second.Direction(f));
						samples.Push(std::move(j));
					}

					Json r = Json::MakeObject();
					r["length"] = found->second.Length();
					r["samples"] = std::move(samples);
					return r;
				});

			// ---- Camera paths and aim ------------------------------------------------------

			add("camera.path",
				"Moves a director camera along a path: progress keys (arc-length 0..1) at times, or from/to over start/end. relativeTo "
				"puts the path in a node's frame; followRotation false keeps only its position. An empty path stops following.",
				Schema::Object(
					{ { "camera", Schema::String("Camera name (created on first use).") }, { "path", Schema::String("Path name.") },
						{ "time", TimeSchema() }, { "progress", Schema::Number("One progress key at time.") },
						{ "start", Schema::Number("Start time of a from/to move.") }, { "end", Schema::Number("End time.") },
						{ "from", Schema::Number("Progress at start (default 0).") },
						{ "to", Schema::Number("Progress at end (default 1).") },
						{ "relativeTo", NodeSchema("Path is in this node's frame.") },
						{ "followRotation", Schema::Boolean("The node's rotation turns the path too (default true).") },
						{ "ease", EaseOrSmoothSchema() } },
					{ "camera" }),
				[c, edit](const Json& args)
				{
					const std::string name = args["camera"].AsString();

					if (name.empty())
					{
						throw ToolError("camera name is empty");
					}

					const std::string pathName = args["path"].AsString();

					if (!pathName.empty() && !c->Player->GetProject().Paths.contains(pathName))
					{
						throw ToolError("no path named '" + pathName + "'");
					}

					const Ease ease = ReadEaseOr(args, Ease::Smooth);
					const NodeId relativeTo = OptionalNode(args, "relativeTo");
					const double t = ReadTime(args, *c->Player);
					edit(
						[&](ReplayProject& project)
						{
							CameraRig& rig = project.EditCamera(name);

							if (args.Contains("path"))
							{
								rig.Path = pathName;
							}

							if (args.Contains("relativeTo"))
							{
								rig.RelativeTo = relativeTo;
							}

							if (args.Contains("followRotation"))
							{
								rig.FollowRotation = args["followRotation"].AsBool(true);
							}

							if (args.Contains("start") && args.Contains("end"))
							{
								rig.Progress.Set(args["start"].AsNumber(), args["from"].AsFloat(0.0f), ease);
								rig.Progress.Set(args["end"].AsNumber(), args["to"].AsFloat(1.0f), ease);
							}
							else if (args.Contains("progress"))
							{
								rig.Progress.Set(t, args["progress"].AsFloat(), ease);
							}
						});
					return Ok(*c->Player);
				});

			add("camera.aim",
				"Sets what a director camera looks at: auto (from its keys), rotation (its rotation keys), point (keys a lookAt point), "
				"node (a node plus offset), direction (keys a world direction; none keyed: along the path of travel) or path (a point "
				"moving along lookPath by progress keys).",
				Schema::Object(
					{ { "camera", Schema::String("Camera name.") },
						{ "mode", Schema::Enum({ "auto", "rotation", "point", "node", "direction", "path" }, "Aim mode.") },
						{ "time", TimeSchema() }, { "point", Schema::Vector(3, "point: look at this point (keyed at time).") },
						{ "node", NodeSchema("node: look at this node.") }, { "offset", Schema::Vector(3, "node: offset from it.") },
						{ "direction", Schema::Vector(3, "direction: world direction (keyed at time).") },
						{ "path", Schema::String("path: look-at path name.") },
						{ "progress", Schema::Number("path: progress key at time (0..1).") }, { "ease", EaseSchema() } },
					{ "camera", "mode" }),
				[c, edit](const Json& args)
				{
					const std::string name = args["camera"].AsString();
					CameraAim aim = CameraAim::Auto;

					if (name.empty() || !ParseCameraAim(args["mode"].AsString(), aim))
					{
						throw ToolError("camera and a valid mode are required");
					}

					const double t = ReadTime(args, *c->Player);
					const Ease ease = ReadEase(args);
					const auto point = ReadVec3(args, "point");
					const auto offset = ReadVec3(args, "offset");
					const auto direction = ReadVec3(args, "direction");
					const NodeId node = OptionalNode(args, "node");

					if (aim == CameraAim::Path && args.Contains("path") && !c->Player->GetProject().Paths.contains(args["path"].AsString()))
					{
						throw ToolError("no path named '" + args["path"].AsString() + "'");
					}

					edit(
						[&](ReplayProject& project)
						{
							CameraRig& rig = project.EditCamera(name);
							rig.Aim = aim;

							if (point)
							{
								rig.LookAt.Set(t, *point, ease);
							}

							if (args.Contains("node"))
							{
								rig.LookAtNode = node;
							}

							if (offset)
							{
								rig.LookAtOffset = *offset;
							}

							if (direction)
							{
								rig.LookDirection.Set(t, *direction, ease);
							}

							if (args.Contains("path"))
							{
								rig.LookPath = args["path"].AsString();
							}

							if (args.Contains("progress"))
							{
								rig.LookProgress.Set(t, args["progress"].AsFloat(), ease);
							}
						});
					return Ok(*c->Player);
				});

			// ---- Shots ---------------------------------------------------------------------

			add("shot.create",
				"Creates a shot from a preset over [start, end): static, orbit (radius, height, startAngle, degrees around target), dolly "
				"(radius to endRadius toward target), follow (offset in the target's frame) or flythrough (points, looking along the "
				"path). target is a node; without one, point. Makes rig and path 'shot:<name>'; consecutive shots cut instantly.",
				Schema::Object(
					{ { "name", Schema::String("Shot name (default: numbered).") },
						{ "preset", Schema::Enum({ "static", "orbit", "dolly", "follow", "flythrough" }, "Preset.") },
						{ "start", Schema::Number("Start time (default: current time).") },
						{ "end", Schema::Number("End time (default start + 4).") }, { "target", NodeSchema("Subject node.") },
						{ "point", Schema::Vector(3, "World target without a node.") },
						{ "targetOffset", Schema::Vector(3, "Look-at offset from the target (default [0, 1, 0]).") },
						{ "radius", Schema::Number("Orbit radius / start distance (default 6).") },
						{ "endRadius", Schema::Number("Dolly end distance (default 2.5).") },
						{ "height", Schema::Number("Height above the target (default 2).") },
						{ "startAngle", Schema::Number("Degrees around +Y (0: on the target's +Z side).") },
						{ "degrees", Schema::Number("Orbit sweep (default 180; negative: clockwise).") },
						{ "offset", Schema::Vector(3, "Follow: camera offset in the target's frame (default [0, 2, 5]).") },
						{ "points", Schema::ArrayOf(Schema::Vector(3, "Point."), "Flythrough points (world).") },
						{ "fov", Schema::Number("Field of view, degrees (default: recorded).") }, { "ease", EaseOrSmoothSchema() } },
					{ "preset" }),
				[c, edit](const Json& args)
				{
					ShotDesc d;

					if (!ParseShotPreset(args["preset"].AsString(), d.Preset))
					{
						throw ToolError("preset must be static, orbit, dolly, follow or flythrough");
					}

					d.Name = args["name"].AsString();
					d.Start = args.Contains("start") ? args["start"].AsNumber() : c->Player->GetTime();
					d.End = args["end"].AsNumber(d.Start + 4.0);
					d.Target = OptionalNode(args, "target");
					d.Point = ReadVec3(args, "point").value_or(d.Point);
					d.TargetOffset = ReadVec3(args, "targetOffset").value_or(d.TargetOffset);
					d.Radius = args["radius"].AsFloat(d.Radius);
					d.EndRadius = args["endRadius"].AsFloat(d.EndRadius);
					d.Height = args["height"].AsFloat(d.Height);
					d.StartAngle = args["startAngle"].AsFloat(d.StartAngle);
					d.Degrees = args["degrees"].AsFloat(d.Degrees);
					d.Offset = ReadVec3(args, "offset").value_or(d.Offset);
					d.Points = ReadPoints(args, "points");
					d.Fov = args["fov"].AsFloat(0.0f);
					d.Motion = ReadEaseOr(args, Ease::Smooth);

					if (d.Preset == ShotPreset::Flythrough && d.Points.size() < 2)
					{
						throw ToolError("flythrough needs at least two points");
					}

					if (d.End <= d.Start)
					{
						throw ToolError("end must be after start");
					}

					Json shot;
					edit(
						[&](ReplayProject& project)
						{
							shot = ShotJson(CreateShot(project, d));
						});
					Json r = Ok(*c->Player);
					r["shot"] = std::move(shot);
					return r;
				});

			add("shot.set",
				"Creates or edits a shot: a camera rig over [start, end), optionally moving along a path from progressFrom to progressTo. "
				"Overlapped shots are trimmed or split.",
				Schema::Object(
					{ { "name", Schema::String("Shot name.") }, { "camera", Schema::String("Rig name (optional with path).") },
						{ "start", Schema::Number("Start time.") }, { "end", Schema::Number("End time.") },
						{ "path", Schema::String("Path to move along.") },
						{ "progressFrom", Schema::Number("Path progress at start (default 0).") },
						{ "progressTo", Schema::Number("Path progress at end (default 1).") }, { "progressEase", EaseOrSmoothSchema() } },
					{ "name" }),
				[c, edit](const Json& args)
				{
					const auto& project = c->Player->GetProject();
					Shot shot;

					if (const Shot* existing = project.FindShot(args["name"].AsString()))
					{
						shot = *existing;
					}

					shot.Name = args["name"].AsString();
					shot.Camera = args["camera"].AsString(shot.Camera);
					shot.Start = args["start"].AsNumber(shot.Start);
					shot.End = args["end"].AsNumber(shot.End);
					shot.Path = args["path"].AsString(shot.Path);
					shot.ProgressFrom = args["progressFrom"].AsFloat(shot.ProgressFrom);
					shot.ProgressTo = args["progressTo"].AsFloat(shot.ProgressTo);

					if (args.Contains("progressEase") && !ParseEase(args["progressEase"].AsString(), shot.ProgressEase))
					{
						throw ToolError("unknown progressEase");
					}

					if (shot.Name.empty() || shot.End <= shot.Start)
					{
						throw ToolError("a shot needs a name and end after start");
					}

					if (shot.Camera.empty() && shot.Path.empty())
					{
						throw ToolError("a shot needs a camera or a path");
					}

					if (!shot.Path.empty() && !project.Paths.contains(shot.Path))
					{
						throw ToolError("no path named '" + shot.Path + "'");
					}

					edit(
						[&](ReplayProject& p)
						{
							p.SetShot(shot);
						});
					return Ok(*c->Player);
				});

			add("shot.remove", "Deletes a shot (its rig and path stay).",
				Schema::Object({ { "name", Schema::String("Shot name.") } }, { "name" }),
				[c, edit](const Json& args)
				{
					bool removed = false;
					edit(
						[&](ReplayProject& project)
						{
							removed = project.RemoveShot(args["name"].AsString());
						});
					Json r = Ok(*c->Player);
					r["removed"] = removed;
					return r;
				});

			add("shot.list", "Shots in time order and the one at the current time.", Schema::Object(),
				[c](const Json&)
				{
					const auto& project = c->Player->GetProject();
					Json list = Json::MakeArray();

					for (const auto& shot : project.Shots)
					{
						list.Push(ShotJson(shot));
					}

					Json r = Json::MakeObject();
					r["shots"] = std::move(list);
					const Shot* active = project.ActiveShot(c->Player->GetTime());
					r["active"] = active ? Json(active->Name) : Json();
					return r;
				});

			// ---- Timeline ------------------------------------------------------------------

			add("jump.add",
				"Adds a jump key: forward playback reaching 'from' teleports to 'to' (cut out a stretch, or repeat one). from and to are "
				"seconds or marker names.",
				Schema::Object(
					{ { "name", Schema::String("Jump name (default: numbered).") }, { "from", Schema::String("Seconds or a marker name.") },
						{ "to", Schema::String("Seconds or a marker name.") } },
					{ "from", "to" }),
				[c, edit](const Json& args)
				{
					const auto& project = c->Player->GetProject();
					JumpKey jump;
					jump.From = ReadTimeOrMarker(args, "from", project);
					jump.To = ReadTimeOrMarker(args, "to", project);
					jump.Name = args["name"].AsString("jump " + std::to_string(project.Jumps.size() + 1));

					if (std::abs(jump.From - jump.To) < 1.0e-4)
					{
						throw ToolError("from and to are the same time");
					}

					edit(
						[&](ReplayProject& p)
						{
							p.AddJump(jump);
						});
					Json r = Ok(*c->Player);
					r["name"] = jump.Name;
					r["from"] = jump.From;
					r["to"] = jump.To;
					return r;
				});

			add("jump.remove", "Deletes a jump key by name.", Schema::Object({ { "name", Schema::String("Jump name.") } }, { "name" }),
				[c, edit](const Json& args)
				{
					bool removed = false;
					edit(
						[&](ReplayProject& project)
						{
							removed = project.RemoveJump(args["name"].AsString());
						});
					Json r = Ok(*c->Player);
					r["removed"] = removed;
					return r;
				});

			add("jump.list", "Jump keys in time order.", Schema::Object(),
				[c](const Json&)
				{
					Json list = Json::MakeArray();

					for (const auto& jump : c->Player->GetProject().Jumps)
					{
						Json j = Json::MakeObject();
						j["name"] = jump.Name;
						j["from"] = jump.From;
						j["to"] = jump.To;
						list.Push(std::move(j));
					}

					Json r = Json::MakeObject();
					r["jumps"] = std::move(list);
					return r;
				});

			add("time.scale",
				"Keys world speed over the timeline (1 = real time, 0.25 = slow motion; eased keys ramp) and rebakes the time remap from "
				"it. keys: [[time, scale], ...] replace the track; or one scale at time. clear: true restores real time.",
				Schema::Object({ { "keys", Schema::ArrayOf(Schema::Vector(2, "[timeline seconds, scale]"), "Replace the track.") },
					{ "time", TimeSchema() }, { "scale", Schema::Number("One key at time.") }, { "ease", EaseOrSmoothSchema() },
					{ "clear", Schema::Boolean("Remove every key.") } }),
				[c, edit](const Json& args)
				{
					const Ease ease = ReadEaseOr(args, Ease::Smooth);
					const double t = ReadTime(args, *c->Player);
					const double recording = c->Player->GetReader().GetDuration();
					edit(
						[&](ReplayProject& project)
						{
							if (args["clear"].AsBool(false) || args.Contains("keys"))
							{
								project.TimeScale = {};
							}

							for (const auto& k : args["keys"].GetArray())
							{
								project.TimeScale.Set(k[0].AsNumber(), std::max(k[1].AsFloat(1.0f), 0.0f), ease);
							}

							if (args.Contains("scale"))
							{
								project.TimeScale.Set(t, std::max(args["scale"].AsFloat(1.0f), 0.0f), ease);
							}

							project.BakeTimeScale(recording);
						});
					Json r = Ok(*c->Player);
					r["length"] = c->Player->GetLength();
					r["anchors"] = static_cast<std::uint64_t>(c->Player->GetProject().TimeRemap.size());
					return r;
				});

			// ---- Music -----------------------------------------------------------------------

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

			// ---- Sync --------------------------------------------------------------------------

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

			// ---- Lighting ----------------------------------------------------------------------

			add("light.add",
				"Places an extra light as a project node (animate it with edit.transform / edit.prop). type: point, spot or directional.",
				Schema::Object({ { "type", Schema::Enum({ "point", "spot", "directional" }, "Light type (default point).") },
					{ "color", Schema::Vector(3, "Linear RGB (default white).") }, { "intensity", Schema::Number("Default 10.") },
					{ "range", Schema::Number("Metres (default 10).") }, { "innerCone", Schema::Number("Spot inner angle, radians.") },
					{ "outerCone", Schema::Number("Spot outer angle, radians.") }, { "castShadows", Schema::Boolean("Default false.") },
					{ "position", Schema::Vector(3, "Position.") }, { "rotation", Schema::Vector(4, "Quaternion.") },
					{ "euler", Schema::Vector(3, "Yaw, pitch, roll degrees.") },
					{ "attachTo", NodeSchema("Follow this node (pose relative to it).") }, { "label", Schema::String("Display name.") } }),
				[c, edit](const Json& args)
				{
					LightDesc d;
					d.Type = args["type"].AsString(d.Type);

					if (d.Type != "point" && d.Type != "spot" && d.Type != "directional")
					{
						throw ToolError("type must be point, spot or directional");
					}

					d.Color = ReadVec3(args, "color").value_or(d.Color);
					d.Intensity = args["intensity"].AsFloat(d.Intensity);
					d.Range = args["range"].AsFloat(d.Range);
					d.InnerCone = args["innerCone"].AsFloat(d.InnerCone);
					d.OuterCone = args["outerCone"].AsFloat(d.OuterCone);
					d.CastShadows = args["castShadows"].AsBool(d.CastShadows);
					d.Position = ReadVec3(args, "position").value_or(d.Position);
					d.Rotation = ReadRotation(args).value_or(d.Rotation);
					d.AttachTo = OptionalNode(args, "attachTo");
					d.Label = args["label"].AsString();
					NodeId id = NoNode;
					edit(
						[&](ReplayProject& project)
						{
							id = AddLight(project, d).Node;
						});
					Json r = Ok(*c->Player);
					r["node"] = FormatNodeId(id);
					return r;
				});

			add("light.preset",
				"Keys a lighting look at a time (exposure, white balance, contrast, saturation, colour slope, bloom): golden_hour, night, "
				"noir, studio, neon or neutral. Two presets at different times blend between them.",
				Schema::Object({ { "preset", Schema::String("Preset name.") }, { "time", TimeSchema() }, { "ease", EaseOrSmoothSchema() } },
					{ "preset" }),
				[c, edit](const Json& args)
				{
					const std::string preset = args["preset"].AsString();
					const double t = ReadTime(args, *c->Player);
					const Ease ease = ReadEaseOr(args, Ease::Smooth);
					const auto& names = LightingPresets();

					if (std::find(names.begin(), names.end(), preset) == names.end())
					{
						std::string list;

						for (const auto& n : names)
						{
							list += (list.empty() ? "" : ", ") + n;
						}

						throw ToolError("unknown preset; one of " + list);
					}

					edit(
						[&](ReplayProject& project)
						{
							ApplyLightingPreset(project, preset, t, ease);
						});
					return Ok(*c->Player);
				});

			add("grade.key",
				"Keys exposure and colour grading at a time: exposure (EV compensation), ev100 (manual exposure), temperature and tint "
				"(-100..100), contrast, saturation, slope/offset/power (RGB), bloom (intensity).",
				Schema::Object({ { "time", TimeSchema() }, { "ease", EaseSchema() },
					{ "exposure", Schema::Number("EV, positive brightens.") }, { "ev100", Schema::Number("Manual exposure EV100.") },
					{ "temperature", Schema::Number("-100..100, warmer positive.") },
					{ "tint", Schema::Number("-100..100, magenta positive.") }, { "contrast", Schema::Number("1 = none.") },
					{ "saturation", Schema::Number("1 = none, 0 = grey.") }, { "slope", Schema::Vector(3, "CDL slope.") },
					{ "offset", Schema::Vector(3, "CDL offset.") }, { "power", Schema::Vector(3, "CDL power.") },
					{ "bloom", Schema::Number("Bloom intensity.") } }),
				[c, edit](const Json& args)
				{
					const double t = ReadTime(args, *c->Player);
					const Ease ease = ReadEase(args);
					std::size_t keyed = 0;
					edit(
						[&](ReplayProject& project)
						{
							for (const auto& [field, key] : { std::pair{ "exposure", "post.exposure" }, std::pair{ "ev100", "post.ev100" },
									 std::pair{ "temperature", "grade.temperature" }, std::pair{ "tint", "grade.tint" },
									 std::pair{ "contrast", "grade.contrast" }, std::pair{ "saturation", "grade.saturation" },
									 std::pair{ "bloom", "bloom.intensity" } })
							{
								if (args.Contains(field))
								{
									project.Environment[key].Set(t, args[field].AsFloat(), ease);
									++keyed;
								}
							}

							for (const auto& [field, key] : { std::pair{ "slope", "grade.slope" }, std::pair{ "offset", "grade.offset" },
									 std::pair{ "power", "grade.power" } })
							{
								if (const auto v = ReadVec3(args, field))
								{
									project.Environment[key].Set(t, *v, ease);
									++keyed;
								}
							}
						});
					Json r = Ok(*c->Player);
					r["keyed"] = static_cast<std::uint64_t>(keyed);
					return r;
				});
		}

	} // namespace

	void RegisterReplayTools(Tooling::ToolServer& server, ReplayPlayer& player, ReplayToolsDesc desc)
	{
		auto c = std::make_shared<Context>();
		c->Server = &server;
		c->Player = &player;
		c->Desc = std::move(desc);
		const std::string p = c->Desc.Prefix;

		auto add = [&](std::string name, std::string description, Json schema, std::function<Json(const Json&)> handler)
		{
			server.Register({ p + name, std::move(description), std::move(schema), std::move(handler) });
		};

		// Edits: one undo step, then the view is re-evaluated.
		auto edit = [c](const std::function<void(ReplayProject&)>& change)
		{
			c->Player->GetProject().Checkpoint();
			change(c->Player->GetProject());
			c->Player->Refresh();
		};

		// ---- Session and transport ------------------------------------------------

		add("status", "Recording, timeline position, transport state, counts, active camera and undo state.", Schema::Object(),
			[c](const Json&)
			{
				auto& player = *c->Player;
				const auto& view = player.GetView();
				Json r = Json::MakeObject();
				r["open"] = player.IsOpen();
				r["recording"] = player.GetProject().Recording;
				r["metadata"] = player.GetReader().GetMetadata();
				r["time"] = player.GetTime();
				r["length"] = player.GetLength();
				r["recordingTime"] = view.RecordingTime;
				r["playing"] = player.IsPlaying();
				r["speed"] = player.GetSpeed();
				r["frames"] = static_cast<std::uint64_t>(player.GetReader().GetFrameCount());
				r["nodes"] = static_cast<std::uint64_t>(view.Nodes.size());
				r["camera"] = view.Camera.Camera;
				r["canUndo"] = player.GetProject().CanUndo();
				r["canRedo"] = player.GetProject().CanRedo();
				return r;
			});

		add("open", "Opens a recording (and its project when one is given).",
			Schema::Object(
				{ { "path", Schema::String("Recording file.") }, { "project", Schema::String("Optional project JSON to load.") } },
				{ "path" }),
			[c](const Json& args)
			{
				const auto path = Resolve(*c, args["path"].AsString());
				std::string error;
				const bool opened = c->Desc.OpenRecording ? c->Desc.OpenRecording(path, error) : c->Player->Open(path, &error);

				if (!opened)
				{
					throw ToolError("cannot open recording: " + error);
				}

				if (args.Contains("project") && !c->Player->GetProject().Load(Resolve(*c, args["project"].AsString()), &error))
				{
					throw ToolError("recording opened, project failed: " + error);
				}

				c->Player->Refresh();
				return Ok(*c->Player);
			});

		add("play", "Starts playback, optionally at a speed.",
			Schema::Object({ { "speed", Schema::Number("Playback speed (1 = real time).") } }),
			[c](const Json& args)
			{
				if (args.Contains("speed"))
				{
					c->Player->SetSpeed(args["speed"].AsNumber(1.0));
				}

				c->Player->Play();
				return Ok(*c->Player);
			});

		add("pause", "Pauses playback.", Schema::Object(),
			[c](const Json&)
			{
				c->Player->Pause();
				return Ok(*c->Player);
			});

		add("seek", "Moves to a timeline time.", Schema::Object({ { "time", Schema::Number("Timeline seconds.") } }, { "time" }),
			[c](const Json& args)
			{
				c->Player->Seek(args["time"].AsNumber());
				return Ok(*c->Player);
			});

		add("step", "Steps whole recorded frames (negative: back) and pauses.",
			Schema::Object({ { "frames", Schema::Integer("Frames to move (default 1).") } }),
			[c](const Json& args)
			{
				c->Player->Step(static_cast<int>(args["frames"].AsInt(1)));
				return Ok(*c->Player);
			});

		add("speed", "Sets the playback speed.", Schema::Object({ { "speed", Schema::Number("Speed (1 = real time).") } }, { "speed" }),
			[c](const Json& args)
			{
				c->Player->SetSpeed(args["speed"].AsNumber(1.0));
				return Ok(*c->Player);
			});

		// ---- Inspection -----------------------------------------------------------

		add("nodes.list", "Lists nodes in the current view, paged, optionally by kind or near a point.",
			Schema::Object({ { "kind", Schema::String("Only this kind (mesh, light, camera, emitter, ...).") },
				{ "near", Schema::Vector(3, "Sort by distance to this point.") },
				{ "radius", Schema::Number("With near: only within this distance.") }, { "offset", Schema::Integer("Skip this many.") },
				{ "limit", Schema::Integer("At most this many (default 50, max 500).") } }),
			[c](const Json& args)
			{
				auto& player = *c->Player;
				const auto& nodes = player.GetView().Nodes;
				const std::string kind = args["kind"].AsString();
				const auto kindId = kind.empty() ? std::nullopt : player.GetNames().Find(kind);
				const auto near = ReadVec3(args, "near");
				const double radius = args["radius"].AsNumber(1.0e30);
				std::vector<std::pair<float, const NodeState*>> picked;

				for (const auto& node : nodes)
				{
					if (!kind.empty() && (!kindId || node.Kind != *kindId))
					{
						continue;
					}

					float distance = 0.0f;

					if (near)
					{
						distance = Animation::Length(Animation::Sub(player.WorldPose(node.Id).Position, *near));

						if (distance > radius)
						{
							continue;
						}
					}

					picked.emplace_back(distance, &node);
				}

				if (near)
				{
					std::stable_sort(picked.begin(), picked.end(),
						[](const auto& a, const auto& b)
						{
							return a.first < b.first;
						});
				}

				const auto offset = static_cast<std::size_t>(std::max<std::int64_t>(0, args["offset"].AsInt(0)));
				const auto limit = static_cast<std::size_t>(std::clamp<std::int64_t>(args["limit"].AsInt(50), 1, 500));
				Json list = Json::MakeArray();

				for (std::size_t i = offset; i < picked.size() && i < offset + limit; ++i)
				{
					Json n = DescribeNode(player, *picked[i].second, false);

					if (near)
					{
						n["distance"] = picked[i].first;
					}

					list.Push(std::move(n));
				}

				Json r = Json::MakeObject();
				r["total"] = static_cast<std::uint64_t>(picked.size());
				r["nodes"] = std::move(list);
				return r;
			});

		add("nodes.get", "One node with its properties and edits.", Schema::Object({ { "node", NodeSchema() } }, { "node" }),
			[c](const Json& args)
			{
				const NodeId id = RequireNode(args);
				const NodeState* node = c->Player->FindNode(id);

				if (!node)
				{
					throw ToolError("node is not in the scene at this time");
				}

				Json r = DescribeNode(*c->Player, *node, true);
				r["world"] = Json::MakeObject();
				const Transform world = c->Player->WorldPose(id);
				r["world"]["position"] = ToJson(world.Position);
				r["world"]["rotation"] = ToJson(world.Rotation);
				const auto& edits = c->Player->GetProject().Nodes;

				if (const auto found = edits.find(id); found != edits.end())
				{
					r["edited"] = true;
				}

				return r;
			});

		add("nodes.path", "Samples a node's recorded world position over a time range (for planning camera moves).",
			Schema::Object({ { "node", NodeSchema() }, { "from", Schema::Number("Start (timeline seconds).") },
							   { "to", Schema::Number("End.") }, { "samples", Schema::Integer("Samples (default 20, max 200).") } },
				{ "node", "from", "to" }),
			[c](const Json& args)
			{
				const NodeId id = RequireNode(args);
				const double from = args["from"].AsNumber();
				const double to = args["to"].AsNumber();
				const int samples = static_cast<int>(std::clamp<std::int64_t>(args["samples"].AsInt(20), 2, 200));
				Json list = Json::MakeArray();

				for (int i = 0; i < samples; ++i)
				{
					const double t = from + (to - from) * i / (samples - 1);

					if (const auto pose = c->Player->SampleNode(id, t))
					{
						Json s = Json::MakeObject();
						s["time"] = t;
						s["position"] = ToJson(pose->Position);
						list.Push(std::move(s));
					}
				}

				Json r = Json::MakeObject();
				r["samples"] = std::move(list);
				return r;
			});

		// ---- Node edits ------------------------------------------------------------

		add("edit.visible", "Shows or hides a node from a time on (a step key).",
			Schema::Object(
				{ { "node", NodeSchema() }, { "visible", Schema::Boolean("Shown?") }, { "time", TimeSchema() } }, { "node", "visible" }),
			[c, edit](const Json& args)
			{
				const NodeId id = RequireNode(args);
				const double t = ReadTime(args, *c->Player);
				edit(
					[&](ReplayProject& project)
					{
						project.EditNode(id).Visible.Set(t, args["visible"].AsBool(), Ease::Step);
					});
				return Ok(*c->Player);
			});

		add("edit.transform",
			"Keys a node's pose. Mode offset (default) moves the recorded pose; override replaces it. Rotation as a quaternion or euler "
			"[yaw, pitch, roll] degrees.",
			Schema::Object(
				{ { "node", NodeSchema() }, { "time", TimeSchema() }, { "position", Schema::Vector(3, "Position or offset.") },
					{ "rotation", Schema::Vector(4, "Quaternion [x, y, z, w].") },
					{ "euler", Schema::Vector(3, "Yaw, pitch, roll in degrees.") }, { "scale", Schema::Vector(3, "Scale or factor.") },
					{ "mode", Schema::Enum({ "offset", "override" }, "How the tracks apply.") }, { "ease", EaseSchema() } },
				{ "node" }),
			[c, edit](const Json& args)
			{
				const NodeId id = RequireNode(args);
				const double t = ReadTime(args, *c->Player);
				const Ease ease = ReadEase(args);
				const auto position = ReadVec3(args, "position");
				const auto rotation = ReadRotation(args);
				const auto scale = ReadVec3(args, "scale");
				edit(
					[&](ReplayProject& project)
					{
						NodeEdit& e = project.EditNode(id);

						if (args.Contains("mode"))
						{
							e.Mode = args["mode"].AsString() == "override" ? TransformEdit::Override : TransformEdit::Offset;
						}

						if (position)
						{
							e.Position.Set(t, *position, ease);
						}

						if (rotation)
						{
							e.Rotation.Set(t, *rotation, ease);
						}

						if (scale)
						{
							e.Scale.Set(t, *scale, ease);
						}
					});
				return Ok(*c->Player);
			});

		add("edit.asset", "Swaps the asset in one of a node's slots (slot 0 is the mesh for mesh nodes). An empty key removes the swap.",
			Schema::Object({ { "node", NodeSchema() }, { "slot", Schema::Integer("Asset slot.") },
							   { "kind", Schema::Enum({ "mesh", "texture", "material", "sound", "effect", "other" }, "Asset kind.") },
							   { "key", Schema::String("Asset key the game resolves.") } },
				{ "node", "slot", "key" }),
			[c, edit](const Json& args)
			{
				const NodeId id = RequireNode(args);
				const auto slot = static_cast<std::uint32_t>(std::max<std::int64_t>(0, args["slot"].AsInt()));
				const bool remove = args["key"].AsString().empty();
				const AssetRef ref = remove ? AssetRef{} : ReadAssetRef(args);
				edit(
					[&](ReplayProject& project)
					{
						auto& swaps = project.EditNode(id).AssetSwaps;

						if (remove)
						{
							swaps.erase(slot);
						}
						else
						{
							swaps[slot] = ref;
						}
					});
				return Ok(*c->Player);
			});

		add("edit.prop", "Keys a node property (light colour, intensity, fov, any recorded property name).",
			Schema::Object({ { "node", NodeSchema() }, { "name", Schema::String("Property name.") },
							   { "value", Json::MakeObject() /* any JSON: number, [x,y(,z(,w))], bool, string */ },
							   { "time", TimeSchema() }, { "ease", EaseSchema() } },
				{ "node", "name", "value" }),
			[c, edit](const Json& args)
			{
				const NodeId id = RequireNode(args);
				const std::string name = args["name"].AsString();
				const double t = ReadTime(args, *c->Player);
				const Ease ease = ReadEase(args);
				const Value value = ValueFromJson(args["value"]);
				edit(
					[&](ReplayProject& project)
					{
						project.EditNode(id).Props[name].Set(t, value, ease);
					});
				return Ok(*c->Player);
			});

		add("edit.clear", "Removes edits from a node: all, or one of visible, transform, assets, props, or one property by name.",
			Schema::Object({ { "node", NodeSchema() },
							   { "what", Schema::String("all (default), visible, transform, assets, props, or prop:<name>.") } },
				{ "node" }),
			[c, edit](const Json& args)
			{
				const NodeId id = RequireNode(args);
				const std::string what = args["what"].AsString("all");
				edit(
					[&](ReplayProject& project)
					{
						const auto found = project.Nodes.find(id);

						if (found == project.Nodes.end())
						{
							return;
						}

						NodeEdit& e = found->second;

						if (what == "all" && !e.IsAdded())
						{
							project.Nodes.erase(found);
						}
						else if (what == "visible")
						{
							e.Visible = {};
						}
						else if (what == "transform")
						{
							e.Position = {};
							e.Rotation = {};
							e.Scale = {};
						}
						else if (what == "assets")
						{
							e.AssetSwaps.clear();
						}
						else if (what == "props")
						{
							e.Props.clear();
						}
						else if (what.starts_with("prop:"))
						{
							e.Props.erase(what.substr(5));
						}
					});
				return Ok(*c->Player);
			});

		add("edit.delete_key", "Deletes the key nearest a time from one of a node's tracks.",
			Schema::Object({ { "node", NodeSchema() }, { "track", Schema::String("visible, position, rotation, scale or prop:<name>.") },
							   { "time", TimeSchema() }, { "tolerance", Schema::Number("Seconds (default 0.05).") } },
				{ "node", "track" }),
			[c, edit](const Json& args)
			{
				const NodeId id = RequireNode(args);
				const std::string track = args["track"].AsString();
				const double t = ReadTime(args, *c->Player);
				const double tolerance = args["tolerance"].AsNumber(0.05);
				bool removed = false;
				edit(
					[&](ReplayProject& project)
					{
						NodeEdit& e = project.EditNode(id);
						removed = track == "visible"	 ? e.Visible.Remove(t, tolerance)
							: track == "position"		 ? e.Position.Remove(t, tolerance)
							: track == "rotation"		 ? e.Rotation.Remove(t, tolerance)
							: track == "scale"			 ? e.Scale.Remove(t, tolerance)
							: track.starts_with("prop:") ? e.Props[track.substr(5)].Remove(t, tolerance)
														 : false;
					});
				Json r = Ok(*c->Player);
				r["removed"] = removed;
				return r;
			});

		add("node.add",
			"Adds an object to the scene (a mesh, light, emitter, text or any kind the game presents) with assets by key. Returns its id; "
			"key "
			"its pose with edit.transform.",
			Schema::Object(
				{ { "kind", Schema::String("Node kind (mesh, light, emitter, ...).") }, { "label", Schema::String("Name for people.") },
					{ "assets",
						Schema::ArrayOf(Schema::Object({ { "kind", Schema::String("Asset kind.") }, { "key", Schema::String("Key.") } }),
							"Asset slots.") },
					{ "position", Schema::Vector(3, "Position.") }, { "rotation", Schema::Vector(4, "Quaternion.") },
					{ "euler", Schema::Vector(3, "Yaw, pitch, roll degrees.") }, { "scale", Schema::Vector(3, "Scale.") },
					{ "attachTo", NodeSchema("Follow this node (pose relative to it).") },
					{ "props", Json::MakeObject() /* name -> value */ } },
				{ "kind" }),
			[c, edit](const Json& args)
			{
				const std::string kind = args["kind"].AsString();

				if (kind.empty())
				{
					throw ToolError("kind is empty");
				}

				std::vector<AssetRef> assets;

				for (const auto& a : args["assets"].GetArray())
				{
					assets.push_back(ReadAssetRef(a));
				}

				const NodeId attach = OptionalNode(args, "attachTo");
				const auto position = ReadVec3(args, "position");
				const auto rotation = ReadRotation(args);
				const auto scale = ReadVec3(args, "scale");
				const double t = c->Player->GetTime();
				NodeId id = NoNode;
				edit(
					[&](ReplayProject& project)
					{
						NodeEdit& e = project.AddNode(kind);
						id = e.Node;
						e.Label = args["label"].AsString();
						e.Assets = assets;
						e.AttachTo = attach;
						e.Mode = TransformEdit::Override;
						e.Position.Set(t, position.value_or(Vec3{ 0.0f, 0.0f, 0.0f }));
						e.Rotation.Set(t, rotation.value_or(Animation::IdentityQuat));
						e.Scale.Set(t, scale.value_or(Vec3{ 1.0f, 1.0f, 1.0f }));

						for (const auto& [name, value] : args["props"].GetObject())
						{
							e.Props[name].Set(t, ValueFromJson(value), Ease::Step);
						}
					});
				Json r = Ok(*c->Player);
				r["node"] = FormatNodeId(id);
				return r;
			});

		add("node.remove", "Removes a node added with node.add.", Schema::Object({ { "node", NodeSchema() } }, { "node" }),
			[c, edit](const Json& args)
			{
				const NodeId id = RequireNode(args);

				if (!(id & ProjectNodeBit))
				{
					throw ToolError("only added nodes can be removed; hide recorded ones with edit.visible");
				}

				edit(
					[&](ReplayProject& project)
					{
						project.Nodes.erase(id);
					});
				return Ok(*c->Player);
			});

		// ---- Cameras -----------------------------------------------------------------

		add("camera.key",
			"Keys a director camera (created on first use). Give position plus rotation/euler, or a lookAt point or lookAtNode. relativeTo "
			"makes positions follow a node. Fov in degrees, roll in degrees, focusDistance in metres or focusNode, aperture as an f-stop.",
			Schema::Object(
				{ { "camera", Schema::String("Camera name.") }, { "time", TimeSchema() },
					{ "position", Schema::Vector(3, "Eye position.") },
					{ "rotation", Schema::Vector(4, "Quaternion (camera looks down -Z).") },
					{ "euler", Schema::Vector(3, "Yaw, pitch, roll degrees.") }, { "lookAt", Schema::Vector(3, "Point to look at.") },
					{ "lookAtNode", NodeSchema("Node to look at (whole camera).") },
					{ "lookAtOffset", Schema::Vector(3, "Offset from lookAtNode.") }, { "relativeTo", NodeSchema("Follow this node.") },
					{ "fov", Schema::Number("Vertical field of view, degrees.") }, { "roll", Schema::Number("Roll, degrees.") },
					{ "focusDistance", Schema::Number("Focus distance, metres.") }, { "focusNode", NodeSchema("Keep this node in focus.") },
					{ "aperture", Schema::Number("Aperture f-stop.") }, { "ease", EaseSchema() } },
				{ "camera" }),
			[c, edit](const Json& args)
			{
				const std::string name = args["camera"].AsString();

				if (name.empty())
				{
					throw ToolError("camera name is empty");
				}

				const double t = ReadTime(args, *c->Player);
				const Ease ease = ReadEase(args);
				const auto position = ReadVec3(args, "position");
				const auto rotation = ReadRotation(args);
				const auto lookAt = ReadVec3(args, "lookAt");
				const auto offset = ReadVec3(args, "lookAtOffset");
				const NodeId lookAtNode = OptionalNode(args, "lookAtNode");
				const NodeId relativeTo = OptionalNode(args, "relativeTo");
				const NodeId focusNode = OptionalNode(args, "focusNode");
				bool first = false;
				edit(
					[&](ReplayProject& project)
					{
						first = !project.Cameras.contains(name);
						CameraRig& rig = project.EditCamera(name);

						if (position)
						{
							rig.Position.Set(t, *position, ease);
						}

						if (rotation)
						{
							rig.Rotation.Set(t, *rotation, ease);
						}

						if (lookAt)
						{
							rig.LookAt.Set(t, *lookAt, ease);
						}

						if (args.Contains("lookAtNode"))
						{
							rig.LookAtNode = lookAtNode;
						}

						if (offset)
						{
							rig.LookAtOffset = *offset;
						}

						if (args.Contains("relativeTo"))
						{
							rig.RelativeTo = relativeTo;
						}

						if (args.Contains("focusNode"))
						{
							rig.FocusNode = focusNode;
						}

						for (const auto& [field, track] : { std::pair{ "fov", &rig.Fov }, std::pair{ "roll", &rig.Roll },
								 std::pair{ "focusDistance", &rig.FocusDistance }, std::pair{ "aperture", &rig.Aperture } })
						{
							if (args.Contains(field))
							{
								track->Set(t, args[field].AsFloat(), ease);
							}
						}

						// The first key of the first camera also cuts to it, so the change is visible.
						if (first && project.Cuts.empty())
						{
							project.SetCut(0.0, name);
						}
					});
				Json r = Ok(*c->Player);
				r["created"] = first;
				return r;
			});

		add("camera.delete_key", "Deletes the key nearest a time from a camera track.",
			Schema::Object({ { "camera", Schema::String("Camera name.") },
							   { "track",
								   Schema::Enum({ "all", "position", "rotation", "lookAt", "fov", "roll", "focusDistance", "aperture" },
									   "Track (default all: every track's key at that time).") },
							   { "time", TimeSchema() }, { "tolerance", Schema::Number("Seconds (default 0.05).") } },
				{ "camera" }),
			[c, edit](const Json& args)
			{
				const std::string name = args["camera"].AsString();
				const std::string track = args["track"].AsString("all");
				const double t = ReadTime(args, *c->Player);
				const double tolerance = args["tolerance"].AsNumber(0.05);
				int removed = 0;
				edit(
					[&](ReplayProject& project)
					{
						const auto found = project.Cameras.find(name);

						if (found == project.Cameras.end())
						{
							throw ToolError("no camera named " + name);
						}

						CameraRig& rig = found->second;
						const bool all = track == "all";
						removed += (all || track == "position") && rig.Position.Remove(t, tolerance);
						removed += (all || track == "rotation") && rig.Rotation.Remove(t, tolerance);
						removed += (all || track == "lookAt") && rig.LookAt.Remove(t, tolerance);
						removed += (all || track == "fov") && rig.Fov.Remove(t, tolerance);
						removed += (all || track == "roll") && rig.Roll.Remove(t, tolerance);
						removed += (all || track == "focusDistance") && rig.FocusDistance.Remove(t, tolerance);
						removed += (all || track == "aperture") && rig.Aperture.Remove(t, tolerance);
					});
				Json r = Ok(*c->Player);
				r["removed"] = removed;
				return r;
			});

		add("camera.remove", "Deletes a director camera and its cuts.",
			Schema::Object({ { "camera", Schema::String("Camera name.") } }, { "camera" }),
			[c, edit](const Json& args)
			{
				const std::string name = args["camera"].AsString();
				edit(
					[&](ReplayProject& project)
					{
						project.Cameras.erase(name);
						std::erase_if(project.Cuts,
							[&](const CameraCut& cut)
							{
								return cut.Camera == name;
							});
					});
				return Ok(*c->Player);
			});

		add("camera.list", "Director cameras with their key times, and the cut list.", Schema::Object(),
			[c](const Json&)
			{
				const auto& project = c->Player->GetProject();
				Json cameras = Json::MakeArray();

				for (const auto& [name, rig] : project.Cameras)
				{
					Json cam = Json::MakeObject();
					cam["name"] = name;
					Json times = Json::MakeArray();
					std::vector<double> all;

					for (const auto& k : rig.Position.Keys)
					{
						all.push_back(k.Time);
					}

					for (const auto& k : rig.Rotation.Keys)
					{
						all.push_back(k.Time);
					}

					for (const auto& k : rig.LookAt.Keys)
					{
						all.push_back(k.Time);
					}

					std::sort(all.begin(), all.end());
					all.erase(std::unique(all.begin(), all.end(),
								  [](double a, double b)
								  {
									  return std::abs(a - b) < 1.0e-6;
								  }),
						all.end());

					for (const double t : all)
					{
						times.Push(t);
					}

					cam["keyTimes"] = std::move(times);
					cam["fovKeys"] = KeyCount(rig.Fov);
					cam["focusKeys"] = KeyCount(rig.FocusDistance);

					if (rig.LookAtNode != NoNode)
					{
						cam["lookAtNode"] = FormatNodeId(rig.LookAtNode);
					}

					if (rig.RelativeTo != NoNode)
					{
						cam["relativeTo"] = FormatNodeId(rig.RelativeTo);
					}

					cameras.Push(std::move(cam));
				}

				Json cuts = Json::MakeArray();

				for (const auto& cut : project.Cuts)
				{
					Json j = Json::MakeObject();
					j["time"] = cut.Time;
					j["camera"] = cut.Camera;
					cuts.Push(std::move(j));
				}

				Json r = Json::MakeObject();
				r["cameras"] = std::move(cameras);
				r["cuts"] = std::move(cuts);
				r["active"] = c->Player->GetView().Camera.Camera;
				return r;
			});

		add("camera.cut", "Cuts to a camera at a time (empty camera: the recorded one). remove: true deletes the cut at that time.",
			Schema::Object({ { "time", TimeSchema() }, { "camera", Schema::String("Camera name, empty for the recorded camera.") },
				{ "remove", Schema::Boolean("Delete the cut at this time instead.") } }),
			[c, edit](const Json& args)
			{
				const double t = ReadTime(args, *c->Player);
				const std::string name = args["camera"].AsString();

				if (!args["remove"].AsBool() && !name.empty() && !c->Player->GetProject().Cameras.contains(name))
				{
					throw ToolError("no camera named " + name);
				}

				edit(
					[&](ReplayProject& project)
					{
						if (args["remove"].AsBool())
						{
							project.RemoveCut(t, 0.05);
						}
						else
						{
							project.SetCut(t, name);
						}
					});
				return Ok(*c->Player);
			});

		// ---- Environment, markers, time -----------------------------------------------

		add("env.set",
			"Keys an environment value by name (the game maps names to its settings, for example fog, sun, time of day or any console "
			"variable). clear: true removes the track.",
			Schema::Object(
				{ { "name", Schema::String("Setting name.") }, { "value", Json::MakeObject() /* any JSON value */ },
					{ "time", TimeSchema() }, { "ease", EaseSchema() }, { "clear", Schema::Boolean("Remove this setting's track.") } },
				{ "name" }),
			[c, edit](const Json& args)
			{
				const std::string name = args["name"].AsString();

				if (name.empty())
				{
					throw ToolError("name is empty");
				}

				const bool clear = args["clear"].AsBool();
				const double t = ReadTime(args, *c->Player);
				const Ease ease = ReadEase(args);
				const Value value = clear ? Value(false) : ValueFromJson(args["value"]);
				edit(
					[&](ReplayProject& project)
					{
						if (clear)
						{
							project.Environment.erase(name);
						}
						else
						{
							project.Environment[name].Set(t, value, ease);
						}
					});
				return Ok(*c->Player);
			});

		add("env.get", "Environment values at the current time.", Schema::Object(),
			[c](const Json&)
			{
				Json values = Json::MakeObject();

				for (const auto& [name, value] : c->Player->GetView().Environment)
				{
					values[name] = ToJson(value);
				}

				Json r = Json::MakeObject();
				r["environment"] = std::move(values);
				return r;
			});

		add("marker.add", "Adds a named marker (beats, hits, moments to land on).",
			Schema::Object({ { "time", TimeSchema() }, { "name", Schema::String("Marker name.") },
							   { "kind", Schema::String("user (default), beat, downbeat, onset, section or your own.") } },
				{ "name" }),
			[c, edit](const Json& args)
			{
				const double t = ReadTime(args, *c->Player);
				edit(
					[&](ReplayProject& project)
					{
						project.AddMarker(t, args["name"].AsString(), args["kind"].AsString("user"));
					});
				return Ok(*c->Player);
			});

		add("marker.remove", "Removes markers by name, by kind (optionally within from/to), or the one nearest a time.",
			Schema::Object({ { "name", Schema::String("Remove every marker with this name.") },
				{ "kind", Schema::String("Or every marker of this kind.") }, { "from", Schema::Number("Kind range start.") },
				{ "to", Schema::Number("Kind range end.") }, { "time", Schema::Number("Or the one nearest this time.") } }),
			[c, edit](const Json& args)
			{
				edit(
					[&](ReplayProject& project)
					{
						if (args.Contains("kind"))
						{
							project.RemoveMarkers(args["kind"].AsString(), args["from"].AsNumber(-1.0e30), args["to"].AsNumber(1.0e30));
						}
						else if (args.Contains("name"))
						{
							const std::string name = args["name"].AsString();
							std::erase_if(project.Markers,
								[&](const ReplayMarker& m)
								{
									return m.Name == name;
								});
						}
						else if (args.Contains("time") && !project.Markers.empty())
						{
							const double t = args["time"].AsNumber();
							const auto nearest = std::min_element(project.Markers.begin(), project.Markers.end(),
								[t](const ReplayMarker& a, const ReplayMarker& b)
								{
									return std::abs(a.Time - t) < std::abs(b.Time - t);
								});
							project.Markers.erase(nearest);
						}
					});
				return Ok(*c->Player);
			});

		add("marker.list", "All markers.", Schema::Object(),
			[c](const Json&)
			{
				Json list = Json::MakeArray();

				for (const auto& m : c->Player->GetProject().Markers)
				{
					Json j = Json::MakeObject();
					j["time"] = m.Time;
					j["name"] = m.Name;
					j["kind"] = m.Kind;
					list.Push(std::move(j));
				}

				Json r = Json::MakeObject();
				r["markers"] = std::move(list);
				return r;
			});

		add("time.remap",
			"Sets the time remap: [timeline, recording] second pairs, linear between anchors (slow motion, speed ramps, holds). An empty "
			"list "
			"restores real time. length optionally fixes the timeline length.",
			Schema::Object({ { "anchors", Schema::ArrayOf(Schema::Vector(2, "[timeline seconds, recording seconds]"), "Anchors.") },
							   { "length", Schema::Number("Timeline length in seconds (omit: from the recording).") } },
				{ "anchors" }),
			[c, edit](const Json& args)
			{
				std::vector<std::pair<double, double>> anchors;

				for (const auto& a : args["anchors"].GetArray())
				{
					anchors.emplace_back(a[0].AsNumber(), a[1].AsNumber());
				}

				std::sort(anchors.begin(), anchors.end());
				edit(
					[&](ReplayProject& project)
					{
						project.TimeRemap = anchors;

						if (args.Contains("length"))
						{
							project.Length = args["length"].AsNumber();
						}
					});
				return Ok(*c->Player);
			});

		// ---- Project ---------------------------------------------------------------------

		add("project.get", "The whole edit project as JSON (the same document project.save writes).", Schema::Object(),
			[c](const Json&)
			{
				return c->Player->GetProject().ToJson();
			});

		add("project.set", "Replaces the whole edit project with a JSON document (one undo step).",
			Schema::Object({ { "project", Json::MakeObject() /* the project document */ } }, { "project" }),
			[c, edit](const Json& args)
			{
				ReplayProject replacement = ReplayProject::FromJson(args["project"]);
				edit(
					[&](ReplayProject& project)
					{
						project.Recording = replacement.Recording.empty() ? project.Recording : replacement.Recording;
						project.Nodes = std::move(replacement.Nodes);
						project.Cameras = std::move(replacement.Cameras);
						project.Paths = std::move(replacement.Paths);
						project.Cuts = std::move(replacement.Cuts);
						project.Shots = std::move(replacement.Shots);
						project.Jumps = std::move(replacement.Jumps);
						project.Markers = std::move(replacement.Markers);
						project.Environment = std::move(replacement.Environment);
						project.TimeRemap = std::move(replacement.TimeRemap);
						project.TimeScale = std::move(replacement.TimeScale);
						project.Music = std::move(replacement.Music);
						project.Length = replacement.Length;
						project.NextAddedNode = replacement.NextAddedNode;
					});
				return Ok(*c->Player);
			});

		add("project.save", "Writes the edit project as JSON.",
			Schema::Object({ { "path", Schema::String("File (default: the configured project path).") } }),
			[c](const Json& args)
			{
				const std::filesystem::path path = args.Contains("path") ? Resolve(*c, args["path"].AsString()) : c->Desc.ProjectPath;

				if (path.empty())
				{
					throw ToolError("no project path configured; pass path");
				}

				if (!c->Player->GetProject().Save(path))
				{
					throw ToolError("cannot write " + path.string());
				}

				Json r = Ok(*c->Player);
				r["path"] = path.string();
				return r;
			});

		add("project.load", "Loads an edit project (clears undo history).",
			Schema::Object({ { "path", Schema::String("File.") } }, { "path" }),
			[c](const Json& args)
			{
				std::string error;

				if (!c->Player->GetProject().Load(Resolve(*c, args["path"].AsString()), &error))
				{
					throw ToolError("cannot load project: " + error);
				}

				c->Player->Refresh();
				return Ok(*c->Player);
			});

		add("undo", "Undoes the last edit.", Schema::Object(),
			[c](const Json&)
			{
				Json r = Ok(*c->Player);
				r["undone"] = c->Player->GetProject().Undo();
				c->Player->Refresh();
				return r;
			});

		add("redo", "Redoes the last undone edit.", Schema::Object(),
			[c](const Json&)
			{
				Json r = Ok(*c->Player);
				r["redone"] = c->Player->GetProject().Redo();
				c->Player->Refresh();
				return r;
			});

		RegisterEditingTools(c, add, edit);
		Internal::RegisterStudioTools(server, player, c->Desc);
	}

} // namespace Swim::Replay
