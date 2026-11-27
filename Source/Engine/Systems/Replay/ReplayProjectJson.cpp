#include "Engine/Systems/Replay/ReplayProject.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>

// The project's JSON form: ToJson, FromJson, Save and Load.
namespace Swim::Replay
{

	using Tooling::Json;

	namespace
	{

		template <typename T> Json TrackToJson(const Track<T>& track, Json (*write)(const T&))
		{
			Json keys = Json::MakeArray();

			for (const auto& key : track.Keys)
			{
				Json k = Json::MakeObject();
				k["t"] = key.Time;
				k["v"] = write(key.Data);

				if (key.Interp != Ease::Linear)
				{
					k["ease"] = ToString(key.Interp);
				}

				if (!key.HasDefaultHandles())
				{
					Json out = Json::MakeArray();
					out.Push(key.OutHandle[0]);
					out.Push(key.OutHandle[1]);
					Json in = Json::MakeArray();
					in.Push(key.InHandle[0]);
					in.Push(key.InHandle[1]);
					k["out"] = std::move(out);
					k["in"] = std::move(in);
				}

				keys.Push(std::move(k));
			}

			return keys;
		}

		template <typename T> Track<T> TrackFromJson(const Json& json, T (*read)(const Json&))
		{
			Track<T> track;

			for (const auto& k : json.GetArray())
			{
				Ease ease = Ease::Linear;
				ParseEase(k["ease"].AsString("linear"), ease);
				const std::size_t index = track.Set(k["t"].AsNumber(), read(k["v"]), ease);

				if (index < track.Keys.size())
				{
					auto& key = track.Keys[index];

					for (std::size_t c = 0; c < 2; ++c)
					{
						if (k["out"].IsArray() && k["out"].GetArray().size() == 2)
						{
							key.OutHandle[c] = k["out"][c].AsFloat(key.OutHandle[c]);
						}

						if (k["in"].IsArray() && k["in"].GetArray().size() == 2)
						{
							key.InHandle[c] = k["in"][c].AsFloat(key.InHandle[c]);
						}
					}
				}
			}

			return track;
		}

		Json WriteBool(const bool& v)
		{
			return Json(v);
		}

		bool ReadBool(const Json& j)
		{
			return j.AsBool(true);
		}

		Json WriteFloat(const float& v)
		{
			return Json(v);
		}

		float ReadFloat(const Json& j)
		{
			return j.AsFloat();
		}

		Json WriteVec3(const Vec3& v)
		{
			return ToJson(v);
		}

		Vec3 ReadVec3(const Json& j)
		{
			return Vec3FromJson(j);
		}

		Json WriteQuat(const Quat& q)
		{
			return ToJson(q);
		}

		Quat ReadQuat(const Json& j)
		{
			return QuatFromJson(j);
		}

		Json WriteValue(const Value& v)
		{
			return ToJson(v);
		}

		Value ReadValue(const Json& j)
		{
			return ValueFromJson(j);
		}

		Json AssetRefToJson(const AssetRef& ref)
		{
			Json j = Json::MakeObject();
			j["kind"] = ToString(ref.Kind);
			j["key"] = ref.Key;
			return j;
		}

		AssetRef AssetRefFromJson(const Json& j)
		{
			AssetRef ref;
			ParseAssetKind(j["kind"].AsString("mesh"), ref.Kind);
			ref.Key = j["key"].AsString();
			return ref;
		}

		NodeId ReadNodeId(const Json& j)
		{
			NodeId id = NoNode;
			ParseNodeId(j, id);
			return id;
		}

	} // namespace

	Json ReplayProject::ToJson() const
	{
		Json root = Json::MakeObject();
		root["format"] = "swim-replay-project";
		root["version"] = 1;
		root["recording"] = Recording;
		root["nextAddedNode"] = FormatNodeId(NextAddedNode);

		if (Length)
		{
			root["length"] = *Length;
		}

		Json nodes = Json::MakeArray();

		for (const auto& [id, edit] : Nodes)
		{
			Json n = Json::MakeObject();
			n["node"] = FormatNodeId(id);

			if (!edit.Label.empty())
			{
				n["label"] = edit.Label;
			}

			if (!edit.Visible.Empty())
			{
				n["visible"] = TrackToJson(edit.Visible, &WriteBool);
			}

			n["mode"] = edit.Mode == TransformEdit::Override ? "override" : "offset";

			if (!edit.Position.Empty())
			{
				n["position"] = TrackToJson(edit.Position, &WriteVec3);
			}

			if (!edit.Rotation.Empty())
			{
				n["rotation"] = TrackToJson(edit.Rotation, &WriteQuat);
			}

			if (!edit.Scale.Empty())
			{
				n["scale"] = TrackToJson(edit.Scale, &WriteVec3);
			}

			for (const auto& [slot, ref] : edit.AssetSwaps)
			{
				n["assetSwaps"][std::to_string(slot)] = AssetRefToJson(ref);
			}

			for (const auto& [name, track] : edit.Props)
			{
				n["props"][name] = TrackToJson(track, &WriteValue);
			}

			if (edit.IsAdded())
			{
				n["kind"] = edit.Kind;
				Json assets = Json::MakeArray();

				for (const auto& ref : edit.Assets)
				{
					assets.Push(AssetRefToJson(ref));
				}

				n["assets"] = std::move(assets);

				if (edit.AttachTo != NoNode)
				{
					n["attachTo"] = FormatNodeId(edit.AttachTo);
				}
			}

			nodes.Push(std::move(n));
		}

		root["nodes"] = std::move(nodes);
		Json cameras = Json::MakeArray();

		for (const auto& [name, rig] : Cameras)
		{
			Json c = Json::MakeObject();
			c["name"] = name;
			c["position"] = TrackToJson(rig.Position, &WriteVec3);
			c["rotation"] = TrackToJson(rig.Rotation, &WriteQuat);
			c["lookAt"] = TrackToJson(rig.LookAt, &WriteVec3);
			c["fov"] = TrackToJson(rig.Fov, &WriteFloat);
			c["roll"] = TrackToJson(rig.Roll, &WriteFloat);
			c["focusDistance"] = TrackToJson(rig.FocusDistance, &WriteFloat);
			c["aperture"] = TrackToJson(rig.Aperture, &WriteFloat);
			c["lookAtOffset"] = Swim::Replay::ToJson(rig.LookAtOffset);

			if (rig.Aim != CameraAim::Auto)
			{
				c["aim"] = Swim::Replay::ToString(rig.Aim);
			}

			if (!rig.Path.empty())
			{
				c["path"] = rig.Path;
				c["progress"] = TrackToJson(rig.Progress, &WriteFloat);
			}

			if (!rig.LookDirection.Empty())
			{
				c["lookDirection"] = TrackToJson(rig.LookDirection, &WriteVec3);
			}

			if (!rig.LookPath.empty())
			{
				c["lookPath"] = rig.LookPath;
				c["lookProgress"] = TrackToJson(rig.LookProgress, &WriteFloat);
			}

			if (rig.LookAtNode != NoNode)
			{
				c["lookAtNode"] = FormatNodeId(rig.LookAtNode);
			}

			if (rig.RelativeTo != NoNode)
			{
				c["relativeTo"] = FormatNodeId(rig.RelativeTo);

				if (!rig.FollowRotation)
				{
					c["followRotation"] = false;
				}
			}

			if (rig.FocusNode != NoNode)
			{
				c["focusNode"] = FormatNodeId(rig.FocusNode);
			}

			cameras.Push(std::move(c));
		}

		root["cameras"] = std::move(cameras);
		Json paths = Json::MakeObject();

		for (const auto& [name, path] : Paths)
		{
			paths[name] = path.ToJson();
		}

		root["paths"] = std::move(paths);
		Json shots = Json::MakeArray();

		for (const auto& shot : Shots)
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

			shots.Push(std::move(j));
		}

		root["shots"] = std::move(shots);
		Json jumps = Json::MakeArray();

		for (const auto& jump : Jumps)
		{
			Json j = Json::MakeObject();
			j["name"] = jump.Name;
			j["from"] = jump.From;
			j["to"] = jump.To;
			jumps.Push(std::move(j));
		}

		root["jumps"] = std::move(jumps);

		if (!TimeScale.Empty())
		{
			root["timeScale"] = TrackToJson(TimeScale, &WriteFloat);
		}

		if (Music)
		{
			root["music"]["path"] = Music->Path;
			root["music"]["offset"] = Music->Offset;
			root["music"]["volume"] = Music->Volume;
		}
		Json cuts = Json::MakeArray();

		for (const auto& cut : Cuts)
		{
			Json c = Json::MakeObject();
			c["time"] = cut.Time;
			c["camera"] = cut.Camera;
			cuts.Push(std::move(c));
		}

		root["cuts"] = std::move(cuts);
		Json markers = Json::MakeArray();

		for (const auto& marker : Markers)
		{
			Json m = Json::MakeObject();
			m["time"] = marker.Time;
			m["name"] = marker.Name;

			if (marker.Kind != "user")
			{
				m["kind"] = marker.Kind;
			}
			markers.Push(std::move(m));
		}

		root["markers"] = std::move(markers);
		Json environment = Json::MakeObject();

		for (const auto& [name, track] : Environment)
		{
			environment[name] = TrackToJson(track, &WriteValue);
		}

		root["environment"] = std::move(environment);
		Json remap = Json::MakeArray();

		for (const auto& [timeline, recording] : TimeRemap)
		{
			Json anchor = Json::MakeArray();
			anchor.Push(timeline);
			anchor.Push(recording);
			remap.Push(std::move(anchor));
		}

		root["timeRemap"] = std::move(remap);
		return root;
	}

	ReplayProject ReplayProject::FromJson(const Json& root)
	{
		if (!root.IsObject())
		{
			throw std::runtime_error("a replay project is a JSON object");
		}

		ReplayProject project;
		project.Recording = root["recording"].AsString();
		ParseNodeId(root["nextAddedNode"], project.NextAddedNode);
		project.NextAddedNode |= ProjectNodeBit;

		if (root["length"].IsNumber())
		{
			project.Length = root["length"].AsNumber();
		}

		for (const auto& n : root["nodes"].GetArray())
		{
			NodeId id = NoNode;

			if (!ParseNodeId(n["node"], id) || id == NoNode)
			{
				throw std::runtime_error("node edit without a valid node id");
			}

			NodeEdit& edit = project.EditNode(id);
			edit.Label = n["label"].AsString();
			edit.Visible = TrackFromJson<bool>(n["visible"], &ReadBool);
			edit.Mode = n["mode"].AsString() == "override" ? TransformEdit::Override : TransformEdit::Offset;
			edit.Position = TrackFromJson<Vec3>(n["position"], &ReadVec3);
			edit.Rotation = TrackFromJson<Quat>(n["rotation"], &ReadQuat);
			edit.Scale = TrackFromJson<Vec3>(n["scale"], &ReadVec3);

			for (const auto& [slot, ref] : n["assetSwaps"].GetObject())
			{
				edit.AssetSwaps[static_cast<std::uint32_t>(std::stoul(slot))] = AssetRefFromJson(ref);
			}

			for (const auto& [name, track] : n["props"].GetObject())
			{
				edit.Props[name] = TrackFromJson<Value>(track, &ReadValue);
			}

			edit.Kind = n["kind"].AsString();

			for (const auto& ref : n["assets"].GetArray())
			{
				edit.Assets.push_back(AssetRefFromJson(ref));
			}

			edit.AttachTo = ReadNodeId(n["attachTo"]);
		}

		for (const auto& c : root["cameras"].GetArray())
		{
			CameraRig& rig = project.EditCamera(c["name"].AsString());
			rig.Position = TrackFromJson<Vec3>(c["position"], &ReadVec3);
			rig.Rotation = TrackFromJson<Quat>(c["rotation"], &ReadQuat);
			rig.LookAt = TrackFromJson<Vec3>(c["lookAt"], &ReadVec3);
			rig.Fov = TrackFromJson<float>(c["fov"], &ReadFloat);
			rig.Roll = TrackFromJson<float>(c["roll"], &ReadFloat);
			rig.FocusDistance = TrackFromJson<float>(c["focusDistance"], &ReadFloat);
			rig.Aperture = TrackFromJson<float>(c["aperture"], &ReadFloat);
			rig.LookAtOffset = Vec3FromJson(c["lookAtOffset"]);
			rig.LookAtNode = ReadNodeId(c["lookAtNode"]);
			rig.RelativeTo = ReadNodeId(c["relativeTo"]);
			rig.FollowRotation = c["followRotation"].AsBool(true);
			rig.FocusNode = ReadNodeId(c["focusNode"]);
			ParseCameraAim(c["aim"].AsString("auto"), rig.Aim);
			rig.Path = c["path"].AsString();
			rig.Progress = TrackFromJson<float>(c["progress"], &ReadFloat);
			rig.LookDirection = TrackFromJson<Vec3>(c["lookDirection"], &ReadVec3);
			rig.LookPath = c["lookPath"].AsString();
			rig.LookProgress = TrackFromJson<float>(c["lookProgress"], &ReadFloat);
		}

		for (const auto& [name, path] : root["paths"].GetObject())
		{
			project.Paths[name] = CameraPath::FromJson(path);
		}

		for (const auto& j : root["shots"].GetArray())
		{
			Shot shot;
			shot.Name = j["name"].AsString();
			shot.Camera = j["camera"].AsString();
			shot.Start = j["start"].AsNumber();
			shot.End = j["end"].AsNumber();
			shot.Path = j["path"].AsString();
			shot.ProgressFrom = j["progressFrom"].AsFloat(0.0f);
			shot.ProgressTo = j["progressTo"].AsFloat(1.0f);
			ParseEase(j["progressEase"].AsString("smooth"), shot.ProgressEase);
			project.Shots.push_back(std::move(shot));
		}

		std::sort(project.Shots.begin(), project.Shots.end(),
			[](const Shot& a, const Shot& b)
			{
				return a.Start < b.Start;
			});

		for (const auto& j : root["jumps"].GetArray())
		{
			project.AddJump({ j["name"].AsString(), j["from"].AsNumber(), j["to"].AsNumber() });
		}

		project.TimeScale = TrackFromJson<float>(root["timeScale"], &ReadFloat);

		if (const Json* music = root.Find("music"); music && music->IsObject())
		{
			project.Music = MusicTrack{ (*music)["path"].AsString(), (*music)["offset"].AsNumber(), (*music)["volume"].AsFloat(1.0f) };
		}

		for (const auto& c : root["cuts"].GetArray())
		{
			project.SetCut(c["time"].AsNumber(), c["camera"].AsString());
		}

		for (const auto& m : root["markers"].GetArray())
		{
			project.AddMarker(m["time"].AsNumber(), m["name"].AsString(), m["kind"].AsString("user"));
		}

		for (const auto& [name, track] : root["environment"].GetObject())
		{
			project.Environment[name] = TrackFromJson<Value>(track, &ReadValue);
		}

		for (const auto& anchor : root["timeRemap"].GetArray())
		{
			project.TimeRemap.emplace_back(anchor[0].AsNumber(), anchor[1].AsNumber());
		}

		std::sort(project.TimeRemap.begin(), project.TimeRemap.end());
		return project;
	}

	bool ReplayProject::Save(const std::filesystem::path& path) const
	{
		std::ofstream out(path, std::ios::binary | std::ios::trunc);
		out << ToJson().Dump(2) << '\n';
		return static_cast<bool>(out);
	}

	bool ReplayProject::Load(const std::filesystem::path& path, std::string* error)
	{
		std::ifstream in(path, std::ios::binary);

		if (!in)
		{
			if (error)
			{
				*error = "cannot open " + path.string();
			}

			return false;
		}

		std::stringstream text;
		text << in.rdbuf();

		try
		{
			Assign(FromJson(Json::Parse(text.str())));
			ClearHistory();
			return true;
		}
		catch (const std::exception& e)
		{
			if (error)
			{
				*error = e.what();
			}

			return false;
		}
	}

} // namespace Swim::Replay
