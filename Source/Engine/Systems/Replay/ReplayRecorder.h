#pragma once

#include "Engine/Systems/Replay/ReplayFile.h"

#include <functional>
#include <map>
#include <span>
#include <tuple>

namespace Swim::Replay
{

	class ReplayCapture;
	class ReplayRecorder;

	// Something that contributes to the recorded scene each frame. The engine's scene
	// source covers entities (meshes, lights, emitters, cameras) and sounds; a game
	// adds a source only for what it draws outside the entity scene (for example a
	// custom render feature), as ordinary "mesh" nodes with mesh/texture assets.
	class IReplayCaptureSource
	{

	  public:
		virtual ~IReplayCaptureSource() = default;

		virtual void Capture(ReplayCapture& capture) = 0;
	};

	// Decides whether an asset can be found again at playback from its key alone. The
	// game knows (pack files, registered names it recreates); unresolvable assets with
	// bytes are cached in the recording once per content hash.
	using ReplayAssetPolicy = std::function<bool(const AssetRecord& asset)>;

	// The engine's default policy, for a game policy to fall back on: sounds and named
	// meshes/textures (cooked paths) are found again by key; procedural ("Procedural/...")
	// meshes and textures, materials, effects and other data are cached.
	bool DefaultIsResolvable(const AssetRecord& asset);

	struct ReplayRecorderDesc
	{
		double KeyframeInterval = 5.0;	// Seconds between full keyframes (seek cost).
		std::string Metadata;			// Free text for the game (often JSON): title, map, version.
		ReplayAssetPolicy IsResolvable; // Empty: DefaultIsResolvable. Only asked for assets given with bytes.
	};

	// What a source writes into during Capture.
	class ReplayCapture
	{

	  public:
		NameId Name(std::string_view name);

		// The node with this id for this frame, created from the previous frame's state
		// (so a source may update only what changed) or blank. Nodes not touched this
		// frame are removed unless kept with Keep.
		NodeState& Node(NodeId id);

		// Keeps a node from the previous frame unchanged (a static object).
		void Keep(NodeId id);

		// Registers an asset; returns its id in this recording. data is copied only when
		// the policy says the key is not resolvable. Deduplicated by (kind, key, hash),
		// and data by content hash.
		AssetId Asset(AssetKind kind, std::string_view key, std::span<const std::uint8_t> data = {}, std::uint64_t hash = 0);

		void Event(ReplayEvent event);

		double GetTime() const { return time; }

		// Changes with every ReplayRecorder::Start (asset ids are per recording, so sources
		// that cache them reset when it changes).
		std::uint64_t GetSession() const { return session; }

	  private:
		friend class ReplayRecorder;

		ReplayRecorder* recorder = nullptr;
		double time = 0.0;
		std::uint64_t session = 0;
	};

	// Frame-by-frame scene recorder. Call Capture once per rendered frame with the
	// frame's time; it gathers every source, diffs against the previous frame and
	// streams a keyframe or a delta to disk.
	class ReplayRecorder
	{

	  public:
		ReplayRecorder() = default;

		~ReplayRecorder();

		ReplayRecorder(const ReplayRecorder&) = delete;

		ReplayRecorder& operator=(const ReplayRecorder&) = delete;

		bool Start(const std::filesystem::path& path, ReplayRecorderDesc desc = {});

		// Finishes the file (index and footer). Returns false if any write failed.
		bool Stop();

		bool IsRecording() const { return writer.IsOpen(); }

		void AddSource(IReplayCaptureSource& source);

		void RemoveSource(IReplayCaptureSource& source);

		// Events from code that is not a source (sounds, bursts). Recorded with the next frame.
		void RecordEvent(ReplayEvent event);

		NameId Name(std::string_view name) { return names.Intern(name); }

		AssetId Asset(AssetKind kind, std::string_view key, std::span<const std::uint8_t> data = {}, std::uint64_t hash = 0);

		void Capture(double time);

		struct Stats
		{
			std::size_t Frames = 0;
			std::size_t Keyframes = 0;
			std::size_t Nodes = 0;
			std::size_t Assets = 0;
			std::size_t CachedAssets = 0;
			std::uint64_t CachedBytes = 0;
			std::uint64_t FileBytes = 0;
		};

		const Stats& GetStats() const { return stats; }

	  private:
		friend class ReplayCapture;

		NodeState& TouchNode(NodeId id);

		ReplayRecorderDesc desc;
		ReplayWriter writer;
		NameTable names;
		std::size_t namesWritten = 0;
		std::vector<IReplayCaptureSource*> sources;
		std::vector<NodeState> previous;
		std::map<NodeId, NodeState> current;
		std::vector<ReplayEvent> events;
		std::map<std::tuple<AssetKind, std::string, std::uint64_t>, AssetId> assetLookup;
		std::map<std::uint64_t, AssetId> cachedByHash;
		AssetId nextAsset = 0;
		double lastKeyframe = -1.0e30;
		bool first = true;
		std::uint64_t session = 0;
		Stats stats;
	};

} // namespace Swim::Replay
