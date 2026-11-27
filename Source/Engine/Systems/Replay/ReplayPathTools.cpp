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

		void AddPathSetTool(const std::shared_ptr<Context>& c, const AddTool& add, const EditProject& edit)
		{
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
		}

		void AddPathGetTool(const std::shared_ptr<Context>& c, const AddTool& add)
		{
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
		}

		void AddPathListTool(const std::shared_ptr<Context>& c, const AddTool& add)
		{
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
		}

		void AddPathRemoveTool(const std::shared_ptr<Context>& c, const AddTool& add, const EditProject& edit)
		{
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
		}

		void AddPathSampleTool(const std::shared_ptr<Context>& c, const AddTool& add)
		{
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
		}

		void AddCameraPathTool(const std::shared_ptr<Context>& c, const AddTool& add, const EditProject& edit)
		{
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
		}

		void AddCameraAimTool(const std::shared_ptr<Context>& c, const AddTool& add, const EditProject& edit)
		{
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
		}

		void AddShotCreateTool(const std::shared_ptr<Context>& c, const AddTool& add, const EditProject& edit)
		{
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
		}

		void AddShotSetTool(const std::shared_ptr<Context>& c, const AddTool& add, const EditProject& edit)
		{
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
		}

		void AddShotRemoveTool(const std::shared_ptr<Context>& c, const AddTool& add, const EditProject& edit)
		{
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
		}

		void AddShotListTool(const std::shared_ptr<Context>& c, const AddTool& add)
		{
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
		}

	} // namespace

	void RegisterPathTools(const std::shared_ptr<Context>& c, const AddTool& add, const EditProject& edit)
	{
		// ---- Paths -----------------------------------------------------------------

		AddPathSetTool(c, add, edit);
		AddPathGetTool(c, add);
		AddPathListTool(c, add);
		AddPathRemoveTool(c, add, edit);
		AddPathSampleTool(c, add);

		// ---- Camera paths and aim ------------------------------------------------------

		AddCameraPathTool(c, add, edit);
		AddCameraAimTool(c, add, edit);

		// ---- Shots ---------------------------------------------------------------------

		AddShotCreateTool(c, add, edit);
		AddShotSetTool(c, add, edit);
		AddShotRemoveTool(c, add, edit);
		AddShotListTool(c, add);
	}

} // namespace Swim::Replay::Internal
