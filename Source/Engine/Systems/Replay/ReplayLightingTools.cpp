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

		void AddLightAddTool(const std::shared_ptr<Context>& c, const AddTool& add, const EditProject& edit)
		{
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
		}

		void AddLightPresetTool(const std::shared_ptr<Context>& c, const AddTool& add, const EditProject& edit)
		{
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
		}

		void AddGradeKeyTool(const std::shared_ptr<Context>& c, const AddTool& add, const EditProject& edit)
		{
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

	void RegisterLightingTools(const std::shared_ptr<Context>& c, const AddTool& add, const EditProject& edit)
	{
		// ---- Lighting ----------------------------------------------------------------------

		AddLightAddTool(c, add, edit);
		AddLightPresetTool(c, add, edit);
		AddGradeKeyTool(c, add, edit);
	}

} // namespace Swim::Replay::Internal
