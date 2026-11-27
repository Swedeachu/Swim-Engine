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

		void AddCameraKeyTool(const std::shared_ptr<Context>& c, const AddTool& add, const EditProject& edit)
		{
			add("camera.key",
				"Keys a director camera (created on first use). Give position plus rotation/euler, or a lookAt point or lookAtNode. "
				"relativeTo "
				"makes positions follow a node. Fov in degrees, roll in degrees, focusDistance in metres or focusNode, aperture as an "
				"f-stop.",
				Schema::Object(
					{ { "camera", Schema::String("Camera name.") }, { "time", TimeSchema() },
						{ "position", Schema::Vector(3, "Eye position.") },
						{ "rotation", Schema::Vector(4, "Quaternion (camera looks down -Z).") },
						{ "euler", Schema::Vector(3, "Yaw, pitch, roll degrees.") }, { "lookAt", Schema::Vector(3, "Point to look at.") },
						{ "lookAtNode", NodeSchema("Node to look at (whole camera).") },
						{ "lookAtOffset", Schema::Vector(3, "Offset from lookAtNode.") }, { "relativeTo", NodeSchema("Follow this node.") },
						{ "fov", Schema::Number("Vertical field of view, degrees.") }, { "roll", Schema::Number("Roll, degrees.") },
						{ "focusDistance", Schema::Number("Focus distance, metres.") },
						{ "focusNode", NodeSchema("Keep this node in focus.") }, { "aperture", Schema::Number("Aperture f-stop.") },
						{ "ease", EaseSchema() } },
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
		}

		void AddCameraDeleteKeyTool(const std::shared_ptr<Context>& c, const AddTool& add, const EditProject& edit)
		{
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
		}

		void AddCameraRemoveTool(const std::shared_ptr<Context>& c, const AddTool& add, const EditProject& edit)
		{
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
		}

		void AddCameraListTool(const std::shared_ptr<Context>& c, const AddTool& add)
		{
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
		}

		void AddCameraCutTool(const std::shared_ptr<Context>& c, const AddTool& add, const EditProject& edit)
		{
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
		}

	} // namespace

	void RegisterCameraTools(const std::shared_ptr<Context>& c, const AddTool& add, const EditProject& edit)
	{
		// ---- Cameras -----------------------------------------------------------------

		AddCameraKeyTool(c, add, edit);
		AddCameraDeleteKeyTool(c, add, edit);
		AddCameraRemoveTool(c, add, edit);
		AddCameraListTool(c, add);
		AddCameraCutTool(c, add, edit);
	}

} // namespace Swim::Replay::Internal
