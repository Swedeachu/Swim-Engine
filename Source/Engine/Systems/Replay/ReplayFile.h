#pragma once

#include "Engine/Systems/Replay/ReplayTypes.h"

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace Swim::Replay
{

	// Interned strings of one recording. Ids are dense and never reused.
	class NameTable
	{

	  public:
		NameId Intern(std::string_view name);

		std::optional<NameId> Find(std::string_view name) const;

		const std::string& Get(NameId id) const; // Empty for unknown ids.

		std::size_t Size() const { return names.size(); }

		const std::vector<std::string>& All() const { return names; }

	  private:
		std::vector<std::string> names;
		std::unordered_map<std::string, NameId> lookup;
	};

	// One captured frame: every live node (sorted by id) and the events since the
	// previous frame.
	struct ReplayFrame
	{
		double Time = 0.0;
		std::vector<NodeState> Nodes;
		std::vector<ReplayEvent> Events;
	};

	// Binary container, streamed front to back so a crash loses only the tail:
	//   "SWRP" u32 version, metadata string, then chunks [u8 type][u32 size][payload]:
	//   names (new interned strings), asset (record, bytes only when cached),
	//   keyframe (every node), delta (removed ids, changed fields of changed nodes,
	//   events). Close appends an index (frame times and offsets, asset table, names)
	//   and a footer; a file without one is indexed by scanning its chunks.
	inline constexpr std::uint32_t ReplayFormatVersion = 1;

	// Encodes frames into chunks on the caller's thread and writes them on its own
	// thread, so disk stalls never reach the frame.
	class ReplayWriter
	{

	  public:
		~ReplayWriter();

		bool Open(const std::filesystem::path& path, std::string_view metadata);

		bool IsOpen() const { return open; }

		// Names [first, names.Size()) are new since the last call.
		void WriteNames(const NameTable& names, std::size_t first);

		void WriteAsset(AssetId id, const AssetRecord& asset);

		// previous is ignored for keyframes. Both node lists must be sorted by id.
		void WriteFrame(const ReplayFrame& frame, const std::vector<NodeState>* previous, bool keyframe);

		// Writes the index and footer and waits for the disk. Returns false if any write failed.
		bool Close();

		std::uint64_t GetBytesWritten() const { return offset; }

		std::size_t GetFrameCount() const { return frames.size(); }

	  private:
		struct FrameEntry
		{
			double Time;
			std::uint64_t Offset;
			bool Keyframe;
		};

		struct AssetEntry
		{
			AssetId Id;
			AssetRecord Meta; // Without data.
			std::uint64_t DataOffset;
			std::uint64_t DataSize;
		};

		void Enqueue(std::uint8_t type, std::vector<std::uint8_t> payload);

		void Drain();

		std::ofstream file;
		bool open = false;
		std::uint64_t offset = 0;
		std::vector<FrameEntry> frames;
		std::vector<AssetEntry> assets;
		std::vector<std::string> names;

		std::mutex mutex;
		std::condition_variable wake;
		std::deque<std::vector<std::uint8_t>> pending;
		bool closing = false;
		bool failed = false;
		std::thread thread;
	};

	// Random access to a recording.
	class ReplayReader
	{

	  public:
		bool Open(const std::filesystem::path& path, std::string* error = nullptr);

		void Close();

		bool IsOpen() const { return file.is_open(); }

		const std::string& GetMetadata() const { return metadata; }

		const NameTable& GetNames() const { return names; }

		std::size_t GetFrameCount() const { return frames.size(); }

		double GetFrameTime(std::size_t frame) const { return frames[frame].Time; }

		bool IsKeyframe(std::size_t frame) const { return frames[frame].Keyframe; }

		// Last frame at or before time (0 when time is before the first frame).
		std::size_t FindFrame(double time) const;

		// Last keyframe at or before frame.
		std::size_t FindKeyframe(std::size_t frame) const;

		double GetDuration() const { return frames.empty() ? 0.0 : frames.back().Time - frames.front().Time; }

		double GetStartTime() const { return frames.empty() ? 0.0 : frames.front().Time; }

		std::size_t GetAssetCount() const { return assets.size(); }

		// Metadata (kind, key, hash, HasData) without the bytes.
		const AssetRecord* GetAsset(AssetId id) const;

		// The cached bytes of an asset (empty for key-only assets).
		std::vector<std::uint8_t> LoadAssetData(AssetId id);

		// Applies one frame to state (a keyframe replaces it). state must be sorted by
		// id and hold the frame before (unless frame is a keyframe). events receives
		// the frame's events when given.
		bool ApplyFrame(std::size_t frame, std::vector<NodeState>& state, std::vector<ReplayEvent>* events = nullptr);

		// The full node list at a frame: decodes from its keyframe forward.
		bool ReadState(std::size_t frame, std::vector<NodeState>& state);

		// True when the file had no index (written by a recorder that did not close).
		bool WasRecovered() const { return recovered; }

	  private:
		struct FrameEntry
		{
			double Time;
			std::uint64_t Offset;
			bool Keyframe;
		};

		struct AssetEntry
		{
			AssetRecord Meta;
			std::uint64_t DataOffset = 0;
			std::uint64_t DataSize = 0;
		};

		bool ReadIndex(std::uint64_t indexOffset);

		bool Scan(std::uint64_t start);

		bool ReadChunk(std::uint64_t at, std::uint8_t& type, std::vector<std::uint8_t>& payload);

		std::ifstream file;
		std::uint64_t fileSize = 0;
		std::string metadata;
		NameTable names;
		std::vector<FrameEntry> frames;
		std::vector<AssetEntry> assets;
		bool recovered = false;
	};

} // namespace Swim::Replay
