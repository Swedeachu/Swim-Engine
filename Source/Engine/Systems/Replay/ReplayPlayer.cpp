#include "Engine/Systems/Replay/ReplayPlayer.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace Swim::Replay
{

	namespace
	{

		auto LowerBound(std::vector<NodeState>& nodes, NodeId id)
		{
			return std::lower_bound(nodes.begin(), nodes.end(), id,
				[](const NodeState& n, NodeId k)
				{
					return n.Id < k;
				});
		}

		const NodeState* FindIn(const std::vector<NodeState>& nodes, NodeId id)
		{
			const auto found = std::lower_bound(nodes.begin(), nodes.end(), id,
				[](const NodeState& n, NodeId k)
				{
					return n.Id < k;
				});
			return found != nodes.end() && found->Id == id ? &*found : nullptr;
		}

		// Translation and rotation only: cameras follow a node without its scale.
		Transform Rigid(const Transform& t)
		{
			Transform out = t;
			out.Scale = { 1.0f, 1.0f, 1.0f };
			return out;
		}

		bool IsFinitePose(const CameraPose& pose)
		{
			const auto finite = [](float v)
			{
				return std::isfinite(v);
			};
			const float q = pose.Rotation[0] * pose.Rotation[0] + pose.Rotation[1] * pose.Rotation[1] +
				pose.Rotation[2] * pose.Rotation[2] + pose.Rotation[3] * pose.Rotation[3];
			return std::all_of(pose.Position.begin(), pose.Position.end(), finite) &&
				std::all_of(pose.Rotation.begin(), pose.Rotation.end(), finite) && q > 0.5f && q < 2.0f && finite(pose.Fov) &&
				finite(pose.FocusDistance) && finite(pose.Aperture);
		}

	} // namespace

	bool ReplayPlayer::Open(const std::filesystem::path& path, std::string* error)
	{
		Close();

		if (!reader.Open(path, error))
		{
			return false;
		}

		names = reader.GetNames();
		project = {};
		project.Recording = path.filename().string();
		Refresh();
		return true;
	}

	void ReplayPlayer::Close()
	{
		reader.Close();
		names = {};
		projectAssets.clear();
		projectAssetIds.clear();
		time = 0.0;
		playing = false;
		frameA = SIZE_MAX;
		haveB = false;
		stateA.clear();
		stateB.clear();
		eventsB.clear();
		pendingEvents.clear();
		view = {};
		presented.clear();
		presentedEnvironment.clear();
	}

	void ReplayPlayer::SetSpeed(double newSpeed)
	{
		speed = std::clamp(newSpeed, -64.0, 64.0);
	}

	double ReplayPlayer::RecordingTimeAt(double timelineTime) const
	{
		return reader.GetStartTime() + project.ToRecordingTime(timelineTime);
	}

	double ReplayPlayer::TimelineTimeAt(double recordingTime) const
	{
		// The remap is monotonic in practice; bisect over the timeline.
		double low = 0.0;
		double high = std::max(GetLength(), 1.0e-6);

		for (int i = 0; i < 60; ++i)
		{
			const double mid = 0.5 * (low + high);
			(RecordingTimeAt(mid) < recordingTime ? low : high) = mid;
		}

		return high;
	}

	double ReplayPlayer::GetLength() const
	{
		if (project.Length)
		{
			return std::max(0.0, *project.Length);
		}

		const double duration = reader.GetDuration();

		if (project.TimeRemap.empty())
		{
			return duration;
		}

		const auto& [timeline, recording] = project.TimeRemap.back();
		return std::max(0.0, timeline + (duration - recording));
	}

	void ReplayPlayer::Seek(double newTime)
	{
		++discontinuities;
		time = std::clamp(newTime, 0.0, GetLength());
		pendingEvents.clear();
		Evaluate(RecordingTimeAt(time), false);
	}

	void ReplayPlayer::Step(int frames)
	{
		playing = false;

		if (reader.GetFrameCount() == 0)
		{
			return;
		}

		const auto current = static_cast<std::int64_t>(reader.FindFrame(RecordingTimeAt(time)));
		const auto target = std::clamp<std::int64_t>(current + frames, 0, static_cast<std::int64_t>(reader.GetFrameCount()) - 1);
		Seek(TimelineTimeAt(reader.GetFrameTime(static_cast<std::size_t>(target))));
	}

	const ReplayView& ReplayPlayer::Update(double dt)
	{
		bool forward = false;

		if (playing && dt > 0.0)
		{
			const double length = GetLength();
			const double before = time;
			time += dt * speed;
			forward = speed > 0.0;

			// Jump keys teleport forward playback (no events fire across the jump).
			if (const JumpKey* jump = forward ? project.JumpBetween(before, time) : nullptr)
			{
				time = jump->To + (time - jump->From);
				forward = false;
				++discontinuities;
				pendingEvents.clear();
			}

			if (time >= length || time <= 0.0)
			{
				time = std::clamp(time, 0.0, length);
				playing = false;
			}
		}

		Evaluate(RecordingTimeAt(time), forward);
		return view;
	}

	const ReplayView& ReplayPlayer::Refresh()
	{
		Evaluate(RecordingTimeAt(time), false);
		return view;
	}

	ReplayView ReplayPlayer::Peek(double timelineTime)
	{
		// Everything Evaluate touches is put back, so playback and its events carry on.
		const double savedTime = time;
		ReplayView savedView = view;
		std::vector<ReplayEvent> savedPending = pendingEvents;
		const std::size_t savedFrame = frameA;
		std::vector<NodeState> savedA = stateA;
		std::vector<NodeState> savedB = stateB;
		std::vector<ReplayEvent> savedEventsB = eventsB;
		const bool savedHaveB = haveB;

		time = std::isfinite(timelineTime) ? std::clamp(timelineTime, 0.0, GetLength()) : savedTime;
		pendingEvents.clear();
		Evaluate(RecordingTimeAt(time), false);
		ReplayView result = std::move(view);
		result.Events.clear();

		time = savedTime;
		view = std::move(savedView);
		pendingEvents = std::move(savedPending);
		frameA = savedFrame;
		stateA = std::move(savedA);
		stateB = std::move(savedB);
		eventsB = std::move(savedEventsB);
		haveB = savedHaveB;
		return result;
	}

	void ReplayPlayer::LoadFrames(std::size_t frame, bool collectEvents)
	{
		if (frameA == frame)
		{
			return;
		}

		if (frameA != SIZE_MAX && frame > frameA && frame - frameA <= 512)
		{
			// Roll forward frame by frame, collecting the events crossed.
			for (std::size_t g = frameA + 1; g <= frame; ++g)
			{
				if (!haveB)
				{
					stateB = stateA;
					eventsB.clear();
					reader.ApplyFrame(g, stateB, &eventsB);
				}

				stateA.swap(stateB);
				haveB = false;

				if (collectEvents)
				{
					pendingEvents.insert(pendingEvents.end(), eventsB.begin(), eventsB.end());
				}
			}
		}
		else
		{
			stateA.clear();
			reader.ReadState(frame, stateA);
		}

		frameA = frame;
		haveB = false;

		if (frame + 1 < reader.GetFrameCount())
		{
			stateB = stateA;
			eventsB.clear();
			haveB = reader.ApplyFrame(frame + 1, stateB, &eventsB);
		}
	}

	void ReplayPlayer::Evaluate(double recordingTime, bool collectEvents)
	{
		view.Time = time;
		view.RecordingTime = recordingTime;
		view.Nodes.clear();
		view.Environment.clear();

		if (reader.GetFrameCount() > 0)
		{
			const std::size_t frame = reader.FindFrame(recordingTime);
			LoadFrames(frame, collectEvents);
			float alpha = 0.0f;

			if (haveB)
			{
				const double a = reader.GetFrameTime(frame);
				const double b = reader.GetFrameTime(frame + 1);
				alpha = b > a ? static_cast<float>(std::clamp((recordingTime - a) / (b - a), 0.0, 1.0)) : 0.0f;
			}

			view.Nodes.reserve(stateA.size());

			for (const auto& node : stateA)
			{
				NodeState out = node;
				const NodeState* next = haveB && alpha > 0.0f ? FindIn(std::as_const(stateB), node.Id) : nullptr;

				if (next && next->Kind == node.Kind && next->Parent == node.Parent)
				{
					out.Pose = Blend(node.Pose, next->Pose, alpha);

					for (auto& prop : out.Props)
					{
						if (const Value* to = next->Find(prop.Key))
						{
							prop.Data = Blend(prop.Data, *to, alpha);
						}
					}
				}

				view.Nodes.push_back(std::move(out));
			}
		}

		view.Events.swap(pendingEvents);
		pendingEvents.clear();
		ApplyEdits();
		EvaluateCamera();

		for (const auto& [name, track] : project.Environment)
		{
			if (auto value = track.Evaluate(time))
			{
				view.Environment[name] = std::move(*value);
			}
		}
	}

	void ReplayPlayer::ApplyEdits()
	{
		for (const auto& [id, edit] : project.Nodes)
		{
			auto at = LowerBound(view.Nodes, id);

			if (edit.IsAdded())
			{
				if (at == view.Nodes.end() || at->Id != id)
				{
					NodeState node;
					node.Id = id;
					at = view.Nodes.insert(at, std::move(node));
				}

				at->Kind = names.Intern(edit.Kind);
				at->Parent = edit.AttachTo;
				at->Assets.clear();

				for (const auto& ref : edit.Assets)
				{
					at->Assets.push_back(ProjectAsset(ref));
				}
			}
			else if (at == view.Nodes.end() || at->Id != id)
			{
				continue; // Not alive at this time.
			}

			NodeState& node = *at;

			// Visibility keys apply from their time on (hiding at t does not hide before it).
			if (auto visible = edit.Visible.Evaluate(time); visible && time >= edit.Visible.Keys.front().Time - 1.0e-9)
			{
				node.Visible = *visible;
			}

			const auto position = edit.Position.Evaluate(time);
			const auto rotation = edit.Rotation.Evaluate(time);
			const auto scale = edit.Scale.Evaluate(time);

			if (edit.Mode == TransformEdit::Override || edit.IsAdded())
			{
				node.Pose.Position = position.value_or(node.Pose.Position);
				node.Pose.Rotation = rotation.value_or(node.Pose.Rotation);
				node.Pose.Scale = scale.value_or(node.Pose.Scale);
			}
			else
			{
				if (position)
				{
					node.Pose.Position = Animation::Add(node.Pose.Position, *position);
				}

				if (rotation)
				{
					node.Pose.Rotation = Animation::Normalize(Animation::Multiply(*rotation, node.Pose.Rotation));
				}

				if (scale)
				{
					node.Pose.Scale = Animation::Mul(node.Pose.Scale, *scale);
				}
			}

			for (const auto& [slot, ref] : edit.AssetSwaps)
			{
				if (slot >= node.Assets.size())
				{
					node.Assets.resize(slot + 1, NoAsset);
				}

				node.Assets[slot] = ProjectAsset(ref);
			}

			for (const auto& [name, track] : edit.Props)
			{
				if (auto value = track.Evaluate(time))
				{
					node.Set(names.Intern(name), std::move(*value));
				}
			}
		}
	}

	void ReplayPlayer::EvaluateCamera()
	{
		CameraPose pose;
		const auto active = project.ActiveCamera(time);
		const auto rig = active && !active->empty() ? project.Cameras.find(*active) : project.Cameras.end();

		// The recorded camera: the first "camera" node marked active (or the first one).
		const NodeState* recorded = nullptr;

		if (const auto cameraKind = names.Find(Names::Camera))
		{
			const auto activeKey = names.Find("active");

			for (const auto& node : view.Nodes)
			{
				if (node.Kind != *cameraKind)
				{
					continue;
				}

				const Value* flag = activeKey ? node.Find(*activeKey) : nullptr;

				if (!recorded || (flag && std::holds_alternative<bool>(*flag) && std::get<bool>(*flag)))
				{
					recorded = &node;
				}
			}
		}

		float recordedFov = 60.0f;

		if (recorded)
		{
			if (const auto fovKey = names.Find("fov"))
			{
				if (const Value* fov = recorded->Find(*fovKey); fov && std::holds_alternative<float>(*fov))
				{
					recordedFov = std::get<float>(*fov);
				}
			}
		}

		const Shot* shot = project.ActiveShot(time);
		const CameraPath* shotPath = nullptr;

		if (shot && !shot->Path.empty())
		{
			const auto found = project.Paths.find(shot->Path);
			shotPath = found == project.Paths.end() || found->second.Points.empty() ? nullptr : &found->second;
		}

		if (rig != project.Cameras.end() || shotPath)
		{
			static const CameraRig none;
			const CameraRig& r = rig != project.Cameras.end() ? rig->second : none;
			const float roll = r.Roll.Evaluate(time).value_or(0.0f);
			Transform frame;

			if (r.RelativeTo != NoNode && FindNode(r.RelativeTo))
			{
				frame = Rigid(WorldPose(r.RelativeTo));

				if (!r.FollowRotation)
				{
					frame.Rotation = Animation::IdentityQuat;
				}
			}

			const auto pathOf = [&](const std::string& name) -> const CameraPath*
			{
				const auto found = name.empty() ? project.Paths.end() : project.Paths.find(name);
				return found == project.Paths.end() || found->second.Points.empty() ? nullptr : &found->second;
			};

			Vec3 local = r.Position.Evaluate(time).value_or(Vec3{ 0.0f, 0.0f, 0.0f });
			Vec3 travel{ 0.0f, 0.0f, -1.0f };

			if (shotPath)
			{
				const float progress = shot->ProgressAt(time);
				local = shotPath->Sample(progress);
				travel = shotPath->Direction(progress);

				if (shot->ProgressTo < shot->ProgressFrom)
				{
					travel = Animation::Scale(travel, -1.0f);
				}
			}
			else if (const CameraPath* path = pathOf(r.Path))
			{
				const float progress = r.Progress.Evaluate(time).value_or(0.0f);
				local = path->Sample(progress);
				travel = path->Direction(progress);
			}

			pose.Position = Compose(frame, Transform{ local }).Position;
			CameraAim aim = r.Aim;

			if (aim == CameraAim::Auto)
			{
				if (r.LookAtNode != NoNode)
				{
					aim = CameraAim::Node;
				}
				else if (!r.LookAt.Empty())
				{
					aim = CameraAim::Point;
				}
				else if (!r.LookPath.empty())
				{
					aim = CameraAim::Path;
				}
				else if (!r.LookDirection.Empty())
				{
					aim = CameraAim::Direction;
				}
				else
				{
					aim = !r.Rotation.Empty() || !shotPath ? CameraAim::Rotation : CameraAim::Direction;
				}
			}

			std::optional<Vec3> target;

			switch (aim)
			{
			case CameraAim::Node:
				if (r.LookAtNode != NoNode && FindNode(r.LookAtNode))
				{
					target = Animation::Add(WorldPose(r.LookAtNode).Position, r.LookAtOffset);
				}

				break;
			case CameraAim::Point:
				if (auto point = r.LookAt.Evaluate(time))
				{
					target = Compose(frame, Transform{ *point }).Position;
				}

				break;
			case CameraAim::Path:
				if (const CameraPath* look = pathOf(r.LookPath))
				{
					target = Compose(frame, Transform{ look->Sample(r.LookProgress.Evaluate(time).value_or(0.0f)) }).Position;
				}

				break;
			case CameraAim::Direction:
			{
				// A keyed direction, or along the path of travel without one.
				const Vec3 direction = r.LookDirection.Evaluate(time).value_or(travel);
				target = Animation::Add(pose.Position, Animation::Rotate(frame.Rotation, direction));
				break;
			}
			default:
				break;
			}

			if (target)
			{
				pose.Rotation = LookRotation(pose.Position, *target, roll);
			}
			else
			{
				const Quat rotation = r.Rotation.Evaluate(time).value_or(Animation::IdentityQuat);
				pose.Rotation = Animation::Normalize(Animation::Multiply(frame.Rotation, rotation));

				if (roll != 0.0f)
				{
					pose.Rotation = Animation::Normalize(
						Animation::Multiply(pose.Rotation, Animation::FromAxisAngle({ 0.0f, 0.0f, -1.0f }, roll * 3.14159265f / 180.0f)));
				}
			}

			pose.Fov = r.Fov.Evaluate(time).value_or(recordedFov);
			pose.Aperture = r.Aperture.Evaluate(time).value_or(0.0f);

			if (r.FocusNode != NoNode && FindNode(r.FocusNode))
			{
				pose.FocusDistance = Animation::Length(Animation::Sub(WorldPose(r.FocusNode).Position, pose.Position));
			}
			else
			{
				pose.FocusDistance = r.FocusDistance.Evaluate(time).value_or(0.0f);
			}

			pose.Camera = r.Name;
			pose.Shot = shot ? shot->Name : std::string();
			pose.Valid = true;
		}

		// A broken edit (NaN key, degenerate path) never reaches the renderer: fall back to
		// the recorded camera, or the last good pose.
		if (pose.Valid && !IsFinitePose(pose))
		{
			if (recorded)
			{
				const Transform world = WorldPose(recorded->Id);
				pose.Position = world.Position;
				pose.Rotation = world.Rotation;
				pose.Fov = recordedFov;
			}
			else
			{
				pose.Position = view.Camera.Position;
				pose.Rotation = view.Camera.Rotation;
				pose.Fov = view.Camera.Fov;
			}

			pose.Aperture = 0.0f;
			pose.FocusDistance = 0.0f;
			pose.Valid = IsFinitePose(pose);
		}

		if (pose.Valid)
		{
			pose.Fov = std::clamp(pose.Fov, 1.0f, 170.0f);
			pose.Aperture = std::max(pose.Aperture, 0.0f);
			pose.FocusDistance = std::max(pose.FocusDistance, 0.0f);
		}
		else if (recorded)
		{
			const Transform world = WorldPose(recorded->Id);
			pose.Position = world.Position;
			pose.Rotation = world.Rotation;
			pose.Fov = recordedFov;
			pose.Valid = true;
		}

		view.Camera = pose;
	}

	void ReplayPlayer::Present(IReplayPresenter& presenter)
	{
		for (const auto& node : view.Nodes)
		{
			const auto found = presented.find(node.Id);

			if (found == presented.end())
			{
				presenter.OnAdd(node);
				presented.emplace(node.Id, node);
			}
			else if (!(found->second == node))
			{
				presenter.OnUpdate(node, found->second);
				found->second = node;
			}
		}

		for (auto it = presented.begin(); it != presented.end();)
		{
			if (!FindIn(std::as_const(view.Nodes), it->first))
			{
				presenter.OnRemove(it->second);
				it = presented.erase(it);
			}
			else
			{
				++it;
			}
		}

		for (const auto& event : view.Events)
		{
			presenter.OnEvent(event);
		}

		view.Events.clear();

		if (view.Camera.Valid)
		{
			presenter.OnCamera(view.Camera);
		}

		if (view.Environment != presentedEnvironment)
		{
			presenter.OnEnvironment(view.Environment);
			presentedEnvironment = view.Environment;
		}
	}

	const AssetRecord* ReplayPlayer::GetAsset(AssetId id) const
	{
		const std::size_t recorded = reader.GetAssetCount();

		if (id < recorded)
		{
			return reader.GetAsset(id);
		}

		const std::size_t index = static_cast<std::size_t>(id) - recorded;
		return id != NoAsset && index < projectAssets.size() ? &projectAssets[index] : nullptr;
	}

	std::vector<std::uint8_t> ReplayPlayer::LoadAssetData(AssetId id)
	{
		return id < reader.GetAssetCount() ? reader.LoadAssetData(id) : std::vector<std::uint8_t>{};
	}

	AssetId ReplayPlayer::ProjectAsset(const AssetRef& ref)
	{
		const auto found = projectAssetIds.find(ref);

		if (found != projectAssetIds.end())
		{
			return found->second;
		}

		AssetRecord record;
		record.Kind = ref.Kind;
		record.Key = ref.Key;
		const auto id = static_cast<AssetId>(reader.GetAssetCount() + projectAssets.size());
		projectAssets.push_back(std::move(record));
		projectAssetIds.emplace(ref, id);
		return id;
	}

	const NodeState* ReplayPlayer::FindNode(NodeId id) const
	{
		return FindIn(view.Nodes, id);
	}

	Transform ReplayPlayer::WorldPose(NodeId id) const
	{
		const NodeState* node = FindNode(id);

		if (!node)
		{
			return {};
		}

		Transform world = node->Pose;
		NodeId parent = node->Parent;

		for (int depth = 0; parent != NoNode && depth < 64; ++depth)
		{
			const NodeState* p = FindNode(parent);

			if (!p)
			{
				break;
			}

			world = Compose(p->Pose, world);
			parent = p->Parent;
		}

		return world;
	}

	std::optional<Transform> ReplayPlayer::SampleNode(NodeId id, double timelineTime)
	{
		if (reader.GetFrameCount() == 0)
		{
			return std::nullopt;
		}

		std::vector<NodeState> state;

		if (!reader.ReadState(reader.FindFrame(RecordingTimeAt(timelineTime)), state))
		{
			return std::nullopt;
		}

		const NodeState* node = FindIn(state, id);

		if (!node)
		{
			return std::nullopt;
		}

		Transform world = node->Pose;
		NodeId parent = node->Parent;

		for (int depth = 0; parent != NoNode && depth < 64; ++depth)
		{
			const NodeState* p = FindIn(state, parent);

			if (!p)
			{
				break;
			}

			world = Compose(p->Pose, world);
			parent = p->Parent;
		}

		return world;
	}

} // namespace Swim::Replay
