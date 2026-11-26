#pragma once

#include "Engine/Systems/Replay/ReplayFile.h"
#include "Engine/Systems/Replay/ReplayProject.h"

#include <cstdint>
#include <map>
#include <span>
#include <unordered_map>

namespace Swim::Replay
{

	// The evaluated scene at one timeline time: recorded nodes interpolated between
	// frames with the project's edits applied, plus added nodes, the camera and the
	// environment overrides.
	struct ReplayView
	{
		double Time = 0.0;				 // Timeline seconds.
		double RecordingTime = 0.0;		 // After the time remap.
		std::vector<NodeState> Nodes;	 // Sorted by id; hidden nodes keep Visible = false.
		std::vector<ReplayEvent> Events; // Crossed since the previous update while playing forward.
		CameraPose Camera;
		std::map<std::string, Value> Environment;
	};

	// Receives scene changes from ReplayPlayer::Present. The engine's scene presenter
	// builds entities through a resolver; a game may implement its own for its kinds.
	class IReplayPresenter
	{

	  public:
		virtual ~IReplayPresenter() = default;

		virtual void OnAdd(const NodeState& node) = 0;

		virtual void OnUpdate(const NodeState& node, const NodeState& before) = 0;

		virtual void OnRemove(const NodeState& node) = 0;

		virtual void OnEvent(const ReplayEvent&) {}

		virtual void OnCamera(const CameraPose&) {}

		virtual void OnEnvironment(const std::map<std::string, Value>&) {}
	};

	// Turns an asset of the recording (or a project's added asset) into whatever the
	// game loads: a key-only record by its key, a cached one from its bytes.
	class IReplayAssetResolver
	{

	  public:
		virtual ~IReplayAssetResolver() = default;

		// Returns an opaque handle the presenter understands (0: unresolved). data is
		// empty for key-only assets.
		virtual std::uint64_t Resolve(const AssetRecord& asset, std::span<const std::uint8_t> data) = 0;
	};

	class ReplayPlayer
	{

	  public:
		bool Open(const std::filesystem::path& path, std::string* error = nullptr);

		void Close();

		bool IsOpen() const { return reader.IsOpen(); }

		ReplayReader& GetReader() { return reader; }

		ReplayProject& GetProject() { return project; }

		const ReplayProject& GetProject() const { return project; }

		// The recording's names plus names the project introduced.
		NameTable& GetNames() { return names; }

		// Transport (timeline seconds).
		void Play() { playing = true; }

		void Pause() { playing = false; }

		bool IsPlaying() const { return playing; }

		void SetSpeed(double speed);

		double GetSpeed() const { return speed; }

		void Seek(double time);

		// Moves by whole recording frames (negative: back) and pauses.
		void Step(int frames);

		double GetTime() const { return time; }

		// Counts seeks, steps and jumps: anything following the timeline (music) resyncs
		// when it changes.
		std::uint64_t GetDiscontinuities() const { return discontinuities; }

		// Timeline length: the project's, or the recording's duration through the remap.
		double GetLength() const;

		// Advances while playing (dt in real seconds times speed) and evaluates.
		const ReplayView& Update(double dt);

		// Re-evaluates at the current time (after edits) without advancing or firing events.
		const ReplayView& Refresh();

		const ReplayView& GetView() const { return view; }

		// The view at another timeline time (camera, nodes, environment; no events) without
		// moving the transport or the presented view (previews, contact sheets, trajectories).
		ReplayView Peek(double timelineTime);

		// Sends the difference between the last presented view and the current one.
		void Present(IReplayPresenter& presenter);

		// Forgets what was presented (the next Present re-adds everything).
		void ResetPresented() { presented.clear(); }

		// Asset records by id: the recording's, then the project's (swaps, added nodes).
		const AssetRecord* GetAsset(AssetId id) const;

		std::vector<std::uint8_t> LoadAssetData(AssetId id);

		// The id for a project asset reference (created on first use).
		AssetId ProjectAsset(const AssetRef& ref);

		// A node in the current view (nullptr if absent).
		const NodeState* FindNode(NodeId id) const;

		// World pose of a node in the current view, following parents.
		Transform WorldPose(NodeId id) const;

		// Recorded world pose of a node at any timeline time (for camera targets in tools).
		std::optional<Transform> SampleNode(NodeId id, double timelineTime);

	  private:
		void Evaluate(double recordingTime, bool collectEvents);

		void LoadFrames(std::size_t frame, bool collectEvents);

		void ApplyEdits();

		void EvaluateCamera();

		double RecordingTimeAt(double timelineTime) const;

		double TimelineTimeAt(double recordingTime) const;

		ReplayReader reader;
		ReplayProject project;
		NameTable names;
		std::vector<AssetRecord> projectAssets;
		std::map<AssetRef, AssetId> projectAssetIds;

		double time = 0.0;
		std::uint64_t discontinuities = 0;
		double speed = 1.0;
		bool playing = false;

		// Decoded frames around the current time: frameA <= time < frameB.
		std::size_t frameA = SIZE_MAX;
		std::vector<NodeState> stateA;
		std::vector<NodeState> stateB; // frameA + 1 when haveB.
		std::vector<ReplayEvent> eventsB;
		bool haveB = false;
		std::vector<ReplayEvent> pendingEvents;
		std::map<std::string, Value> presentedEnvironment;

		ReplayView view;
		std::unordered_map<NodeId, NodeState> presented;
	};

} // namespace Swim::Replay
