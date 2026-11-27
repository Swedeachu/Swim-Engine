#include "Engine/Systems/Replay/ReplayTools.h"
#include "Engine/Systems/Replay/ReplayToolsInternal.h"

#include "Engine/Systems/Replay/ReplayAudio.h"
#include "Engine/Systems/Replay/ReplayEditing.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <span>

namespace Swim::Replay
{

	using namespace Internal;

	namespace Internal
	{

		namespace
		{

			void AddStatusTool(const std::shared_ptr<Context>& c, const AddTool& add)
			{
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
			}

			void AddOpenTool(const std::shared_ptr<Context>& c, const AddTool& add)
			{
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
			}

			void AddPlayTool(const std::shared_ptr<Context>& c, const AddTool& add)
			{
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
			}

			void AddPauseTool(const std::shared_ptr<Context>& c, const AddTool& add)
			{
				add("pause", "Pauses playback.", Schema::Object(),
					[c](const Json&)
					{
						c->Player->Pause();
						return Ok(*c->Player);
					});
			}

			void AddSeekTool(const std::shared_ptr<Context>& c, const AddTool& add)
			{
				add("seek", "Moves to a timeline time.", Schema::Object({ { "time", Schema::Number("Timeline seconds.") } }, { "time" }),
					[c](const Json& args)
					{
						c->Player->Seek(args["time"].AsNumber());
						return Ok(*c->Player);
					});
			}

			void AddStepTool(const std::shared_ptr<Context>& c, const AddTool& add)
			{
				add("step", "Steps whole recorded frames (negative: back) and pauses.",
					Schema::Object({ { "frames", Schema::Integer("Frames to move (default 1).") } }),
					[c](const Json& args)
					{
						c->Player->Step(static_cast<int>(args["frames"].AsInt(1)));
						return Ok(*c->Player);
					});
			}

			void AddSpeedTool(const std::shared_ptr<Context>& c, const AddTool& add)
			{
				add("speed", "Sets the playback speed.",
					Schema::Object({ { "speed", Schema::Number("Speed (1 = real time).") } }, { "speed" }),
					[c](const Json& args)
					{
						c->Player->SetSpeed(args["speed"].AsNumber(1.0));
						return Ok(*c->Player);
					});
			}

			void AddNodesListTool(const std::shared_ptr<Context>& c, const AddTool& add)
			{
				add("nodes.list", "Lists nodes in the current view, paged, optionally by kind or near a point.",
					Schema::Object({ { "kind", Schema::String("Only this kind (mesh, light, camera, emitter, ...).") },
						{ "near", Schema::Vector(3, "Sort by distance to this point.") },
						{ "radius", Schema::Number("With near: only within this distance.") },
						{ "offset", Schema::Integer("Skip this many.") },
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
			}

			void AddNodesGetTool(const std::shared_ptr<Context>& c, const AddTool& add)
			{
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
			}

			void AddNodesPathTool(const std::shared_ptr<Context>& c, const AddTool& add)
			{
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
			}

		} // namespace

		void RegisterSessionTools(const std::shared_ptr<Context>& c, const AddTool& add, const EditProject&)
		{
			// ---- Session and transport ------------------------------------------------

			AddStatusTool(c, add);
			AddOpenTool(c, add);
			AddPlayTool(c, add);
			AddPauseTool(c, add);
			AddSeekTool(c, add);
			AddStepTool(c, add);
			AddSpeedTool(c, add);

			// ---- Inspection -----------------------------------------------------------

			AddNodesListTool(c, add);
			AddNodesGetTool(c, add);
			AddNodesPathTool(c, add);
		}

	} // namespace Internal

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

	void RegisterReplayTools(Tooling::ToolServer& server, ReplayPlayer& player, ReplayToolsDesc desc)
	{
		auto c = std::make_shared<Context>();
		c->Server = &server;
		c->Player = &player;
		c->Desc = std::move(desc);
		const std::string p = c->Desc.Prefix;

		const AddTool add = [&](std::string name, std::string description, Json schema, std::function<Json(const Json&)> handler)
		{
			server.Register({ p + name, std::move(description), std::move(schema), std::move(handler) });
		};

		// Edits: one undo step, then the view is re-evaluated.
		const EditProject edit = [c](const std::function<void(ReplayProject&)>& change)
		{
			c->Player->GetProject().Checkpoint();
			change(c->Player->GetProject());
			c->Player->Refresh();
		};
		RegisterSessionTools(c, add, edit);
		RegisterNodeTools(c, add, edit);
		RegisterCameraTools(c, add, edit);
		RegisterTimelineTools(c, add, edit);
		RegisterPathTools(c, add, edit);
		RegisterTimeTools(c, add, edit);
		RegisterMusicTools(c, add, edit);
		RegisterLightingTools(c, add, edit);
		Internal::RegisterStudioTools(server, player, c->Desc);
	}

} // namespace Swim::Replay
