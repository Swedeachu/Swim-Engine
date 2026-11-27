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

		void AddEditVisibleTool(const std::shared_ptr<Context>& c, const AddTool& add, const EditProject& edit)
		{
			add("edit.visible", "Shows or hides a node from a time on (a step key).",
				Schema::Object({ { "node", NodeSchema() }, { "visible", Schema::Boolean("Shown?") }, { "time", TimeSchema() } },
					{ "node", "visible" }),
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
		}

		void AddEditTransformTool(const std::shared_ptr<Context>& c, const AddTool& add, const EditProject& edit)
		{
			add("edit.transform",
				"Keys a node's pose. Mode offset (default) moves the recorded pose; override replaces it. Rotation as a quaternion or "
				"euler "
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
		}

		void AddEditAssetTool(const std::shared_ptr<Context>& c, const AddTool& add, const EditProject& edit)
		{
			add("edit.asset",
				"Swaps the asset in one of a node's slots (slot 0 is the mesh for mesh nodes). An empty key removes the swap.",
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
		}

		void AddEditPropTool(const std::shared_ptr<Context>& c, const AddTool& add, const EditProject& edit)
		{
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
		}

		void AddEditClearTool(const std::shared_ptr<Context>& c, const AddTool& add, const EditProject& edit)
		{
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
		}

		void AddEditDeleteKeyTool(const std::shared_ptr<Context>& c, const AddTool& add, const EditProject& edit)
		{
			add("edit.delete_key", "Deletes the key nearest a time from one of a node's tracks.",
				Schema::Object(
					{ { "node", NodeSchema() }, { "track", Schema::String("visible, position, rotation, scale or prop:<name>.") },
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
		}

		void AddNodeAddTool(const std::shared_ptr<Context>& c, const AddTool& add, const EditProject& edit)
		{
			add("node.add",
				"Adds an object to the scene (a mesh, light, emitter, text or any kind the game presents) with assets by key. Returns its "
				"id; "
				"key "
				"its pose with edit.transform.",
				Schema::Object(
					{ { "kind", Schema::String("Node kind (mesh, light, emitter, ...).") }, { "label", Schema::String("Name for people.") },
						{ "assets",
							Schema::ArrayOf(
								Schema::Object({ { "kind", Schema::String("Asset kind.") }, { "key", Schema::String("Key.") } }),
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
		}

		void AddNodeRemoveTool(const std::shared_ptr<Context>& c, const AddTool& add, const EditProject& edit)
		{
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
		}

	} // namespace

	void RegisterNodeTools(const std::shared_ptr<Context>& c, const AddTool& add, const EditProject& edit)
	{
		// ---- Node edits ------------------------------------------------------------

		AddEditVisibleTool(c, add, edit);
		AddEditTransformTool(c, add, edit);
		AddEditAssetTool(c, add, edit);
		AddEditPropTool(c, add, edit);
		AddEditClearTool(c, add, edit);
		AddEditDeleteKeyTool(c, add, edit);
		AddNodeAddTool(c, add, edit);
		AddNodeRemoveTool(c, add, edit);
	}

} // namespace Swim::Replay::Internal
