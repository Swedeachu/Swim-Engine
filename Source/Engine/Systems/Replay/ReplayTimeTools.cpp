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

		void AddJumpAddTool(const std::shared_ptr<Context>& c, const AddTool& add, const EditProject& edit)
		{
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
		}

		void AddJumpRemoveTool(const std::shared_ptr<Context>& c, const AddTool& add, const EditProject& edit)
		{
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
		}

		void AddJumpListTool(const std::shared_ptr<Context>& c, const AddTool& add)
		{
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
		}

		void AddTimeScaleTool(const std::shared_ptr<Context>& c, const AddTool& add, const EditProject& edit)
		{
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
		}

	} // namespace

	void RegisterTimeTools(const std::shared_ptr<Context>& c, const AddTool& add, const EditProject& edit)
	{
		// ---- Timeline ------------------------------------------------------------------

		AddJumpAddTool(c, add, edit);
		AddJumpRemoveTool(c, add, edit);
		AddJumpListTool(c, add);
		AddTimeScaleTool(c, add, edit);
	}

} // namespace Swim::Replay::Internal
