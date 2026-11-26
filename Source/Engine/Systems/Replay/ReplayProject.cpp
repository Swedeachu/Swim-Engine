#include "Engine/Systems/Replay/ReplayProject.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace Swim::Replay
{

	using Tooling::Json;

	namespace
	{

		constexpr float DegreesToRadians = 3.14159265358979f / 180.0f;

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

		Quat FromMatrixColumns(const Vec3& x, const Vec3& y, const Vec3& z)
		{
			const float trace = x[0] + y[1] + z[2];
			Quat q;

			if (trace > 0.0f)
			{
				const float s = std::sqrt(trace + 1.0f) * 2.0f;
				q = { (y[2] - z[1]) / s, (z[0] - x[2]) / s, (x[1] - y[0]) / s, 0.25f * s };
			}
			else if (x[0] > y[1] && x[0] > z[2])
			{
				const float s = std::sqrt(1.0f + x[0] - y[1] - z[2]) * 2.0f;
				q = { 0.25f * s, (y[0] + x[1]) / s, (z[0] + x[2]) / s, (y[2] - z[1]) / s };
			}
			else if (y[1] > z[2])
			{
				const float s = std::sqrt(1.0f + y[1] - x[0] - z[2]) * 2.0f;
				q = { (y[0] + x[1]) / s, 0.25f * s, (z[1] + y[2]) / s, (z[0] - x[2]) / s };
			}
			else
			{
				const float s = std::sqrt(1.0f + z[2] - x[0] - y[1]) * 2.0f;
				q = { (z[0] + x[2]) / s, (z[1] + y[2]) / s, 0.25f * s, (x[1] - y[0]) / s };
			}

			return Animation::Normalize(q);
		}

		Vec3 Cross(const Vec3& a, const Vec3& b)
		{
			return { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
		}

		Vec3 Normalized(const Vec3& v)
		{
			const float length = Animation::Length(v);
			return length > 1.0e-8f ? Animation::Scale(v, 1.0f / length) : Vec3{ 0.0f, 0.0f, 0.0f };
		}

	} // namespace

	// ---- Shared JSON forms --------------------------------------------------------

	Json ToJson(const Vec3& v)
	{
		Json j = Json::MakeArray();
		j.Push(v[0]);
		j.Push(v[1]);
		j.Push(v[2]);
		return j;
	}

	Json ToJson(const Quat& q)
	{
		Json j = Json::MakeArray();

		for (const float c : q)
		{
			j.Push(c);
		}

		return j;
	}

	Vec3 Vec3FromJson(const Json& json, Vec3 fallback)
	{
		if (!json.IsArray() || json.Size() != 3)
		{
			return fallback;
		}

		return { json[0].AsFloat(), json[1].AsFloat(), json[2].AsFloat() };
	}

	Quat QuatFromJson(const Json& json, Quat fallback)
	{
		if (!json.IsArray() || json.Size() != 4)
		{
			return fallback;
		}

		return Animation::Normalize(Quat{ json[0].AsFloat(), json[1].AsFloat(), json[2].AsFloat(), json[3].AsFloat() });
	}

	Json ToJson(const Value& value)
	{
		return std::visit(
			[](const auto& v) -> Json
			{
				using T = std::decay_t<decltype(v)>;

				if constexpr (std::is_same_v<T, bool> || std::is_same_v<T, float> || std::is_same_v<T, std::string>)
				{
					return Json(v);
				}
				else if constexpr (std::is_same_v<T, std::int64_t>)
				{
					Json j = Json::MakeObject();
					j["int"] = v;
					return j;
				}
				else if constexpr (std::is_same_v<T, AssetId>)
				{
					Json j = Json::MakeObject();
					j["asset"] = v;
					return j;
				}
				else
				{
					Json j = Json::MakeArray();

					for (const float c : v)
					{
						j.Push(c);
					}

					return j;
				}
			},
			value);
	}

	Value ValueFromJson(const Json& json)
	{
		if (json.IsBool())
		{
			return json.AsBool();
		}

		if (json.IsNumber())
		{
			return json.AsFloat();
		}

		if (json.IsString())
		{
			return json.AsString();
		}

		if (json.IsObject())
		{
			if (json.Contains("int"))
			{
				return json["int"].AsInt();
			}

			if (json.Contains("asset"))
			{
				return static_cast<AssetId>(json["asset"].AsInt());
			}
		}

		if (json.IsArray())
		{
			const auto& a = json.GetArray();

			if (a.size() == 2)
			{
				return Vec2{ a[0].AsFloat(), a[1].AsFloat() };
			}

			if (a.size() == 3)
			{
				return Vec3{ a[0].AsFloat(), a[1].AsFloat(), a[2].AsFloat() };
			}

			if (a.size() == 4)
			{
				return Vec4{ a[0].AsFloat(), a[1].AsFloat(), a[2].AsFloat(), a[3].AsFloat() };
			}

			if (a.size() == 1)
			{
				return a[0].AsFloat();
			}
		}

		throw std::runtime_error("unsupported property value");
	}

	Quat LookRotation(const Vec3& eye, const Vec3& target, float rollDegrees)
	{
		Vec3 forward = Normalized(Animation::Sub(target, eye));

		if (!(Animation::Length(forward) >= 0.5f)) // Also catches NaN.
		{
			forward = { 0.0f, 0.0f, -1.0f };
		}

		Vec3 up{ 0.0f, 1.0f, 0.0f };

		if (std::abs(forward[1]) > 0.999f)
		{
			up = { 0.0f, 0.0f, forward[1] > 0.0f ? 1.0f : -1.0f };
		}

		const Vec3 right = Normalized(Cross(forward, up));
		const Vec3 cameraUp = Cross(right, forward);
		Quat q = FromMatrixColumns(right, cameraUp, Animation::Scale(forward, -1.0f));

		if (rollDegrees != 0.0f)
		{
			q = Animation::Normalize(
				Animation::Multiply(q, Animation::FromAxisAngle({ 0.0f, 0.0f, -1.0f }, rollDegrees * DegreesToRadians)));
		}

		return q;
	}

	Quat EulerRotation(float yawDegrees, float pitchDegrees, float rollDegrees)
	{
		const Quat yaw = Animation::FromAxisAngle({ 0.0f, 1.0f, 0.0f }, yawDegrees * DegreesToRadians);
		const Quat pitch = Animation::FromAxisAngle({ 1.0f, 0.0f, 0.0f }, pitchDegrees * DegreesToRadians);
		const Quat roll = Animation::FromAxisAngle({ 0.0f, 0.0f, -1.0f }, rollDegrees * DegreesToRadians);
		return Animation::Normalize(Animation::Multiply(Animation::Multiply(yaw, pitch), roll));
	}

	std::string FormatNodeId(NodeId id)
	{
		return std::to_string(id);
	}

	bool ParseNodeId(const Json& json, NodeId& id)
	{
		if (json.IsString())
		{
			const auto& s = json.AsString();
			NodeId value = 0;
			const auto result = std::from_chars(s.data(), s.data() + s.size(), value);

			if (result.ec != std::errc() || result.ptr != s.data() + s.size())
			{
				return false;
			}

			id = value;
			return true;
		}

		if (json.IsNumber() && json.AsNumber() >= 0.0)
		{
			id = static_cast<NodeId>(json.AsNumber());
			return true;
		}

		return false;
	}

	const char* ToString(CameraAim aim)
	{
		switch (aim)
		{
		case CameraAim::Rotation:
			return "rotation";
		case CameraAim::Point:
			return "point";
		case CameraAim::Node:
			return "node";
		case CameraAim::Direction:
			return "direction";
		case CameraAim::Path:
			return "path";
		default:
			return "auto";
		}
	}

	bool ParseCameraAim(std::string_view text, CameraAim& aim)
	{
		for (CameraAim a :
			{ CameraAim::Auto, CameraAim::Rotation, CameraAim::Point, CameraAim::Node, CameraAim::Direction, CameraAim::Path })
		{
			if (text == ToString(a))
			{
				aim = a;
				return true;
			}
		}

		return false;
	}

	float Shot::ProgressAt(double timelineTime) const
	{
		const double span = End - Start;
		const float t = span > 0.0 ? static_cast<float>(std::clamp((timelineTime - Start) / span, 0.0, 1.0)) : 1.0f;
		const float eased = ApplyEase(ProgressEase == Ease::Spline ? Ease::Smooth : ProgressEase, t);
		return ProgressFrom + (ProgressTo - ProgressFrom) * eased;
	}

	// ---- Project -----------------------------------------------------------------

	NodeEdit& ReplayProject::EditNode(NodeId node)
	{
		auto& edit = Nodes[node];
		edit.Node = node;
		return edit;
	}

	NodeEdit& ReplayProject::AddNode(std::string kind)
	{
		const NodeId id = NextAddedNode++;
		auto& edit = EditNode(id);
		edit.Kind = std::move(kind);
		return edit;
	}

	CameraRig& ReplayProject::EditCamera(std::string_view name)
	{
		auto found = Cameras.find(std::string(name));

		if (found == Cameras.end())
		{
			found = Cameras.emplace(std::string(name), CameraRig{}).first;
			found->second.Name = std::string(name);
		}

		return found->second;
	}

	CameraPath& ReplayProject::EditPath(std::string_view name)
	{
		return Paths[std::string(name)];
	}

	void ReplayProject::SetShot(Shot shot)
	{
		// Non-finite times would break the sorted list (and its sort): ignored.
		if (!std::isfinite(shot.Start) || !std::isfinite(shot.End) || !std::isfinite(shot.ProgressFrom) || !std::isfinite(shot.ProgressTo))
		{
			return;
		}

		RemoveShot(shot.Name);

		if (shot.End < shot.Start)
		{
			std::swap(shot.Start, shot.End);
		}

		std::vector<Shot> kept;

		for (Shot& other : Shots)
		{
			if (other.End <= shot.Start || other.Start >= shot.End)
			{
				kept.push_back(std::move(other));
				continue;
			}

			// Overlapped: keep the parts outside the new shot (a covered middle splits it).
			if (other.Start < shot.Start)
			{
				Shot before = other;
				before.End = shot.Start;
				kept.push_back(std::move(before));
			}

			if (other.End > shot.End)
			{
				Shot after = other;
				after.Start = shot.End;

				if (other.Start < shot.Start)
				{
					after.Name += " (2)";
				}

				kept.push_back(std::move(after));
			}
		}

		kept.push_back(std::move(shot));
		std::sort(kept.begin(), kept.end(),
			[](const Shot& a, const Shot& b)
			{
				return a.Start < b.Start;
			});
		Shots = std::move(kept);
	}

	bool ReplayProject::RemoveShot(std::string_view name)
	{
		return std::erase_if(Shots,
				   [&](const Shot& s)
				   {
					   return s.Name == name;
				   }) > 0;
	}

	const Shot* ReplayProject::FindShot(std::string_view name) const
	{
		const auto found = std::find_if(Shots.begin(), Shots.end(),
			[&](const Shot& s)
			{
				return s.Name == name;
			});
		return found == Shots.end() ? nullptr : &*found;
	}

	const Shot* ReplayProject::ActiveShot(double timelineTime) const
	{
		const auto next = std::upper_bound(Shots.begin(), Shots.end(), timelineTime,
			[](double t, const Shot& s)
			{
				return t < s.Start;
			});

		if (next == Shots.begin())
		{
			return nullptr;
		}

		const Shot& shot = *(next - 1);
		return timelineTime < shot.End ? &shot : nullptr;
	}

	void ReplayProject::AddJump(JumpKey jump)
	{
		if (!std::isfinite(jump.From) || !std::isfinite(jump.To))
		{
			return;
		}

		if (!jump.Name.empty())
		{
			RemoveJump(jump.Name);
		}

		const auto at = std::upper_bound(Jumps.begin(), Jumps.end(), jump.From,
			[](double t, const JumpKey& j)
			{
				return t < j.From;
			});
		Jumps.insert(at, std::move(jump));
	}

	bool ReplayProject::RemoveJump(std::string_view name)
	{
		return std::erase_if(Jumps,
				   [&](const JumpKey& j)
				   {
					   return j.Name == name;
				   }) > 0;
	}

	const JumpKey* ReplayProject::JumpBetween(double from, double to) const
	{
		for (const JumpKey& jump : Jumps)
		{
			if (jump.From > from && jump.From <= to)
			{
				return &jump;
			}
		}

		return nullptr;
	}

	void ReplayProject::BakeTimeScale(double recordingLength, double samplesPerSecond)
	{
		TimeRemap.clear();

		if (TimeScale.Empty())
		{
			return;
		}

		const double step = 1.0 / std::max(samplesPerSecond, 1.0);
		const double maxTimeline = 1.0e5;
		double timeline = 0.0;
		double recording = 0.0;
		TimeRemap.emplace_back(0.0, 0.0);
		const auto speed = [this](double t)
		{
			return std::max(0.0, static_cast<double>(*TimeScale.Evaluate(t)));
		};

		// Simpson integration of the speed over steps that never straddle a key, with the
		// step's end read just inside it: exact for step and linear keys, so retimes that
		// must land on a beat do. A zero speed (a hold) still advances the timeline.
		while (recording < recordingLength - 1.0e-9 && timeline < maxTimeline)
		{
			double next = timeline + step;
			const auto key = std::upper_bound(TimeScale.Keys.begin(), TimeScale.Keys.end(), timeline,
				[](double t, const TrackKey<float>& k)
				{
					return t < k.Time;
				});

			if (key != TimeScale.Keys.end() && key->Time < next)
			{
				next = key->Time;
			}

			const double h = next - timeline;
			const double advance = h * (speed(timeline) + 4.0 * speed(timeline + 0.5 * h) + speed(next - h * 1.0e-7)) / 6.0;

			// The last step ends exactly at the recording's end (linear within the step).
			if (recording + advance >= recordingLength)
			{
				timeline += h * (recordingLength - recording) / advance;
				TimeRemap.emplace_back(timeline, recordingLength);
				break;
			}

			timeline = next;
			recording += advance;
			TimeRemap.emplace_back(timeline, recording);
		}
	}

	void ReplayProject::SetCut(double time, std::string camera)
	{
		if (!std::isfinite(time))
		{
			return;
		}

		RemoveCut(time, 1.0e-6);
		const auto at = std::upper_bound(Cuts.begin(), Cuts.end(), time,
			[](double t, const CameraCut& c)
			{
				return t < c.Time;
			});
		Cuts.insert(at, CameraCut{ time, std::move(camera) });
	}

	bool ReplayProject::RemoveCut(double time, double tolerance)
	{
		const auto found = std::find_if(Cuts.begin(), Cuts.end(),
			[&](const CameraCut& c)
			{
				return std::abs(c.Time - time) <= tolerance;
			});

		if (found == Cuts.end())
		{
			return false;
		}

		Cuts.erase(found);
		return true;
	}

	void ReplayProject::AddMarker(double time, std::string name, std::string kind)
	{
		if (!std::isfinite(time))
		{
			return;
		}

		const auto at = std::upper_bound(Markers.begin(), Markers.end(), time,
			[](double t, const ReplayMarker& m)
			{
				return t < m.Time;
			});
		Markers.insert(at, ReplayMarker{ time, std::move(name), kind.empty() ? std::string("user") : std::move(kind) });
	}

	std::size_t ReplayProject::RemoveMarkers(std::string_view kind, double from, double to)
	{
		return std::erase_if(Markers,
			[&](const ReplayMarker& m)
			{
				return (kind.empty() || m.Kind == kind) && m.Time >= from && m.Time <= to;
			});
	}

	double ReplayProject::ToRecordingTime(double timelineTime) const
	{
		if (TimeRemap.empty())
		{
			return timelineTime;
		}

		if (timelineTime <= TimeRemap.front().first)
		{
			return TimeRemap.front().second + (timelineTime - TimeRemap.front().first);
		}

		if (timelineTime >= TimeRemap.back().first)
		{
			return TimeRemap.back().second + (timelineTime - TimeRemap.back().first);
		}

		const auto next = std::upper_bound(TimeRemap.begin(), TimeRemap.end(), timelineTime,
			[](double t, const std::pair<double, double>& anchor)
			{
				return t < anchor.first;
			});
		const auto& b = *next;
		const auto& a = *(next - 1);
		const double span = b.first - a.first;
		return span > 0.0 ? a.second + (b.second - a.second) * (timelineTime - a.first) / span : b.second;
	}

	std::optional<std::string> ReplayProject::ActiveCamera(double timelineTime) const
	{
		if (const Shot* shot = ActiveShot(timelineTime))
		{
			return shot->Camera;
		}

		if (Cuts.empty())
		{
			return std::nullopt;
		}

		const auto next = std::upper_bound(Cuts.begin(), Cuts.end(), timelineTime,
			[](double t, const CameraCut& c)
			{
				return t < c.Time;
			});
		// Before the first cut the recorded camera is used.
		return next == Cuts.begin() ? std::nullopt : std::optional<std::string>((next - 1)->Camera);
	}

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

	void ReplayProject::Assign(const ReplayProject& other)
	{
		Recording = other.Recording;
		Nodes = other.Nodes;
		Cameras = other.Cameras;
		Paths = other.Paths;
		Cuts = other.Cuts;
		Shots = other.Shots;
		Jumps = other.Jumps;
		TimeScale = other.TimeScale;
		Music = other.Music;
		Markers = other.Markers;
		Environment = other.Environment;
		TimeRemap = other.TimeRemap;
		Length = other.Length;
		NextAddedNode = other.NextAddedNode;
	}

	void ReplayProject::Checkpoint()
	{
		undo.push_back(ToJson().Dump());

		if (undo.size() > MaxHistory)
		{
			undo.erase(undo.begin());
		}

		redo.clear();
	}

	bool ReplayProject::Undo()
	{
		if (undo.empty())
		{
			return false;
		}

		redo.push_back(ToJson().Dump());
		Assign(FromJson(Json::Parse(undo.back())));
		undo.pop_back();
		return true;
	}

	bool ReplayProject::Redo()
	{
		if (redo.empty())
		{
			return false;
		}

		undo.push_back(ToJson().Dump());
		Assign(FromJson(Json::Parse(redo.back())));
		redo.pop_back();
		return true;
	}

	void ReplayProject::ClearHistory()
	{
		undo.clear();
		redo.clear();
	}

} // namespace Swim::Replay
