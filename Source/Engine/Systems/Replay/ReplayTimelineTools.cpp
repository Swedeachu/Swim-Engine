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

		void AddEnvSetTool(const std::shared_ptr<Context>& c, const AddTool& add, const EditProject& edit)
		{
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
		}

		void AddEnvGetTool(const std::shared_ptr<Context>& c, const AddTool& add)
		{
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
		}

		void AddMarkerAddTool(const std::shared_ptr<Context>& c, const AddTool& add, const EditProject& edit)
		{
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
		}

		void AddMarkerRemoveTool(const std::shared_ptr<Context>& c, const AddTool& add, const EditProject& edit)
		{
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
		}

		void AddMarkerListTool(const std::shared_ptr<Context>& c, const AddTool& add)
		{
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
		}

		void AddTimeRemapTool(const std::shared_ptr<Context>& c, const AddTool& add, const EditProject& edit)
		{
			add("time.remap",
				"Sets the time remap: [timeline, recording] second pairs, linear between anchors (slow motion, speed ramps, holds). An "
				"empty "
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
		}

		void AddProjectGetTool(const std::shared_ptr<Context>& c, const AddTool& add)
		{
			add("project.get", "The whole edit project as JSON (the same document project.save writes).", Schema::Object(),
				[c](const Json&)
				{
					return c->Player->GetProject().ToJson();
				});
		}

		void AddProjectSetTool(const std::shared_ptr<Context>& c, const AddTool& add, const EditProject& edit)
		{
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
		}

		void AddProjectSaveTool(const std::shared_ptr<Context>& c, const AddTool& add)
		{
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
		}

		void AddProjectLoadTool(const std::shared_ptr<Context>& c, const AddTool& add)
		{
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
		}

		void AddUndoTool(const std::shared_ptr<Context>& c, const AddTool& add)
		{
			add("undo", "Undoes the last edit.", Schema::Object(),
				[c](const Json&)
				{
					Json r = Ok(*c->Player);
					r["undone"] = c->Player->GetProject().Undo();
					c->Player->Refresh();
					return r;
				});
		}

		void AddRedoTool(const std::shared_ptr<Context>& c, const AddTool& add)
		{
			add("redo", "Redoes the last undone edit.", Schema::Object(),
				[c](const Json&)
				{
					Json r = Ok(*c->Player);
					r["redone"] = c->Player->GetProject().Redo();
					c->Player->Refresh();
					return r;
				});
		}

	} // namespace

	void RegisterTimelineTools(const std::shared_ptr<Context>& c, const AddTool& add, const EditProject& edit)
	{
		// ---- Environment, markers, time -----------------------------------------------

		AddEnvSetTool(c, add, edit);
		AddEnvGetTool(c, add);
		AddMarkerAddTool(c, add, edit);
		AddMarkerRemoveTool(c, add, edit);
		AddMarkerListTool(c, add);
		AddTimeRemapTool(c, add, edit);

		// ---- Project ---------------------------------------------------------------------

		AddProjectGetTool(c, add);
		AddProjectSetTool(c, add, edit);
		AddProjectSaveTool(c, add);
		AddProjectLoadTool(c, add);
		AddUndoTool(c, add);
		AddRedoTool(c, add);
	}

} // namespace Swim::Replay::Internal
