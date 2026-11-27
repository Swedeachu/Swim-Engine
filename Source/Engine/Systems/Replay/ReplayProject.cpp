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
