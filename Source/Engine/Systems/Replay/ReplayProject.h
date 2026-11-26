#pragma once

#include "Engine/Systems/Replay/ReplayPath.h"
#include "Engine/Systems/Replay/ReplayTrack.h"
#include "Engine/Systems/Tooling/Json.h"

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace Swim::Replay
{

	// An asset named by key for edits (a swap, an added object). The game's resolver
	// loads it at playback like any recorded key-only asset.
	struct AssetRef
	{
		AssetKind Kind = AssetKind::Mesh;
		std::string Key;

		bool operator==(const AssetRef&) const = default;

		auto operator<=>(const AssetRef&) const = default;
	};

	enum class TransformEdit : std::uint8_t
	{
		Offset,	  // Tracks move the recorded pose (position added, rotation applied after, scale multiplied).
		Override, // Tracks replace the recorded pose where they have keys.
	};

	// Per-node edits. A node whose id has ProjectNodeBit is added by the project: Kind,
	// Assets and AttachTo describe it and its tracks are its pose.
	struct NodeEdit
	{
		NodeId Node = NoNode;
		std::string Label;
		Track<bool> Visible;
		TransformEdit Mode = TransformEdit::Offset;
		Track<Vec3> Position;
		Track<Quat> Rotation;
		Track<Vec3> Scale;
		std::map<std::uint32_t, AssetRef> AssetSwaps; // Slot -> replacement.
		std::map<std::string, Track<Value>> Props;	  // Property overrides by name.

		// Added nodes only.
		std::string Kind;
		std::vector<AssetRef> Assets;
		NodeId AttachTo = NoNode; // Follow a recorded node (pose relative to it).

		bool IsAdded() const { return (Node & ProjectNodeBit) != 0; }
	};

	// What a director camera looks at.
	enum class CameraAim : std::uint8_t
	{
		Auto,	   // Node if LookAtNode is set, else Point if LookAt has keys, else Rotation.
		Rotation,  // The Rotation track.
		Point,	   // The LookAt track.
		Node,	   // LookAtNode (+ LookAtOffset).
		Direction, // The LookDirection track (world direction).
		Path,	   // A point moving along LookPath by LookProgress.
	};

	const char* ToString(CameraAim aim);

	bool ParseCameraAim(std::string_view text, CameraAim& aim);

	// A director camera. Its position is keyed directly or follows a project path (Path,
	// with Progress the arc-length fraction over time; key eases shape the speed). It
	// looks along keyed rotations, at a keyed point, a node, a keyed direction or a point
	// moving along another path. RelativeTo makes positions (and paths) follow a node.
	// Focus is a distance or a node.
	struct CameraRig
	{
		std::string Name;
		Track<Vec3> Position;
		Track<Quat> Rotation;
		std::string Path;	   // Project path the position follows (empty: Position keys).
		Track<float> Progress; // 0..1 along Path by arc length.
		CameraAim Aim = CameraAim::Auto;
		Track<Vec3> LookAt;
		NodeId LookAtNode = NoNode;
		Vec3 LookAtOffset{ 0.0f, 0.0f, 0.0f };
		Track<Vec3> LookDirection;
		std::string LookPath;
		Track<float> LookProgress;
		NodeId RelativeTo = NoNode;
		bool FollowRotation = true; // RelativeTo's rotation turns the frame too (false: position only).
		Track<float> Fov;			// Vertical degrees.
		Track<float> Roll;			// Degrees around the view direction.
		Track<float> FocusDistance;
		Track<float> Aperture; // f-stop.
		NodeId FocusNode = NoNode;
	};

	struct CameraCut
	{
		double Time = 0.0;
		std::string Camera; // Empty: the recorded camera.
	};

	struct ReplayMarker
	{
		double Time = 0.0;
		std::string Name;
		std::string Kind = "user"; // "user", "beat", "downbeat", "onset", "section", or a game's own.
	};

	// A camera over a time range. The shot covering a time wins over Cuts; consecutive
	// shots cut instantly. Outside every shot the cuts (or the recorded camera) apply.
	// A shot may also move along a path by itself: with Path set the position goes from
	// ProgressFrom to ProgressTo over the shot (eased), and the rig (optional) supplies
	// the aim, field of view and focus; without a rig the camera looks along the path.
	struct Shot
	{
		std::string Name;
		std::string Camera; // Rig name (may be empty when Path is set).
		double Start = 0.0;
		double End = 0.0;
		std::string Path;
		float ProgressFrom = 0.0f;
		float ProgressTo = 1.0f;
		Ease ProgressEase = Ease::Smooth;

		// Path progress at a timeline time inside the shot.
		float ProgressAt(double timelineTime) const;
	};

	// Forward playback crossing From jumps to To (a teleport in the edit; seeks ignore it).
	struct JumpKey
	{
		std::string Name;
		double From = 0.0;
		double To = 0.0;
	};

	// Music played with the timeline: music second 0 is at timeline time Offset.
	struct MusicTrack
	{
		std::string Path;
		double Offset = 0.0;
		float Volume = 1.0f;
	};

	// The evaluated camera for a frame.
	struct CameraPose
	{
		std::string Camera; // Rig name, empty for the recorded camera.
		std::string Shot;	// The active shot, if any.
		Vec3 Position{ 0.0f, 0.0f, 0.0f };
		Quat Rotation = Animation::IdentityQuat; // Looks down -Z, +Y up.
		float Fov = 60.0f;
		float FocusDistance = 0.0f; // 0: no focus override.
		float Aperture = 0.0f;		// 0: no depth of field override.
		bool Valid = false;
	};

	// Everything a viewer changes about a recording, kept beside it as readable JSON.
	// Times are timeline (output) seconds; TimeRemap maps them to recording seconds
	// (identity without anchors). The recording is never modified.
	class ReplayProject
	{

	  public:
		std::string Recording; // Path or name of the recording this edits.
		std::map<NodeId, NodeEdit> Nodes;
		std::map<std::string, CameraRig> Cameras;
		std::map<std::string, CameraPath> Paths;
		std::vector<CameraCut> Cuts; // Sorted by time.
		std::vector<Shot> Shots;	 // Sorted by start; non-overlapping (SetShot trims neighbours).
		std::vector<JumpKey> Jumps;	 // Sorted by From.
		std::vector<ReplayMarker> Markers;
		std::map<std::string, Track<Value>> Environment;
		// (timeline seconds, recording seconds) anchors; linear between them, speed 1 outside.
		std::vector<std::pair<double, double>> TimeRemap;
		// World speed over timeline time (1 = real time). BakeTimeScale integrates it into TimeRemap.
		Track<float> TimeScale;
		std::optional<MusicTrack> Music;
		std::optional<double> Length; // Timeline length; empty: the recording's.
		NodeId NextAddedNode = ProjectNodeBit | 1;

		NodeEdit& EditNode(NodeId node);

		// A new project-added node of this kind.
		NodeEdit& AddNode(std::string kind);

		CameraRig& EditCamera(std::string_view name);

		CameraPath& EditPath(std::string_view name);

		// Adds or replaces the shot named shot.Name; overlapped neighbours are trimmed (or
		// dropped when fully covered).
		void SetShot(Shot shot);

		bool RemoveShot(std::string_view name);

		const Shot* FindShot(std::string_view name) const;

		// The shot covering a timeline time (Start inclusive, End exclusive).
		const Shot* ActiveShot(double timelineTime) const;

		void AddJump(JumpKey jump);

		bool RemoveJump(std::string_view name);

		// The first jump whose From lies in (from, to] (forward playback over that range).
		const JumpKey* JumpBetween(double from, double to) const;

		// Rewrites TimeRemap from TimeScale: anchors every 1 / samplesPerSecond timeline
		// seconds until recordingLength is reached. An empty TimeScale clears the remap.
		void BakeTimeScale(double recordingLength, double samplesPerSecond = 30.0);

		void SetCut(double time, std::string camera);

		bool RemoveCut(double time, double tolerance = 1.0e-3);

		void AddMarker(double time, std::string name, std::string kind = "user");

		// Removes markers of a kind (all kinds when empty) within [from, to].
		std::size_t RemoveMarkers(std::string_view kind, double from = -1.0e30, double to = 1.0e30);

		double ToRecordingTime(double timelineTime) const;

		// Active camera name at a timeline time: the covering shot's, else the last cut's
		// (nullopt or empty: the recorded camera).
		std::optional<std::string> ActiveCamera(double timelineTime) const;

		Tooling::Json ToJson() const;

		// Throws std::runtime_error on malformed documents.
		static ReplayProject FromJson(const Tooling::Json& json);

		bool Save(const std::filesystem::path& path) const;

		bool Load(const std::filesystem::path& path, std::string* error = nullptr);

		// Undo history: call Checkpoint before each change.
		void Checkpoint();

		bool Undo();

		bool Redo();

		bool CanUndo() const { return !undo.empty(); }

		bool CanRedo() const { return !redo.empty(); }

		void ClearHistory();

		static constexpr std::size_t MaxHistory = 256;

	  private:
		void Assign(const ReplayProject& other);

		std::vector<std::string> undo;
		std::vector<std::string> redo;
	};

	// JSON forms shared with the tool API.
	Tooling::Json ToJson(const Value& value);

	Value ValueFromJson(const Tooling::Json& json);

	Tooling::Json ToJson(const Vec3& v);

	Tooling::Json ToJson(const Quat& q);

	Vec3 Vec3FromJson(const Tooling::Json& json, Vec3 fallback = { 0.0f, 0.0f, 0.0f });

	Quat QuatFromJson(const Tooling::Json& json, Quat fallback = Animation::IdentityQuat);

	// Rotation that looks from eye toward target with +Y up (camera forward is -Z).
	Quat LookRotation(const Vec3& eye, const Vec3& target, float rollDegrees = 0.0f);

	// Rotation from yaw/pitch/roll in degrees (yaw about +Y, pitch about +X, then roll about -Z).
	Quat EulerRotation(float yawDegrees, float pitchDegrees, float rollDegrees = 0.0f);

	std::string FormatNodeId(NodeId id);

	bool ParseNodeId(const Tooling::Json& json, NodeId& id);

} // namespace Swim::Replay
