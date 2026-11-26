#include "Engine/Systems/Replay/ReplayRecorder.h"

#include <algorithm>

namespace Swim::Replay
{

	bool DefaultIsResolvable(const AssetRecord& asset)
	{
		switch (asset.Kind)
		{
		case AssetKind::Sound:
			return true;
		case AssetKind::Mesh:
		case AssetKind::Texture:
			return !asset.Key.empty() && !asset.Key.starts_with("Procedural/");
		default:
			return false;
		}
	}

	NameId ReplayCapture::Name(std::string_view name)
	{
		return recorder->Name(name);
	}

	NodeState& ReplayCapture::Node(NodeId id)
	{
		return recorder->TouchNode(id);
	}

	void ReplayCapture::Keep(NodeId id)
	{
		recorder->TouchNode(id);
	}

	AssetId ReplayCapture::Asset(AssetKind kind, std::string_view key, std::span<const std::uint8_t> data, std::uint64_t hash)
	{
		return recorder->Asset(kind, key, data, hash);
	}

	void ReplayCapture::Event(ReplayEvent event)
	{
		event.Time = time;
		recorder->events.push_back(std::move(event));
	}

	ReplayRecorder::~ReplayRecorder()
	{
		Stop();
	}

	bool ReplayRecorder::Start(const std::filesystem::path& path, ReplayRecorderDesc recorderDesc)
	{
		Stop();
		desc = std::move(recorderDesc);
		names = {};
		namesWritten = 0;
		previous.clear();
		current.clear();
		events.clear();
		assetLookup.clear();
		cachedByHash.clear();
		nextAsset = 0;
		lastKeyframe = -1.0e30;
		first = true;
		stats = {};
		++session;
		return writer.Open(path, desc.Metadata);
	}

	bool ReplayRecorder::Stop()
	{
		if (!writer.IsOpen())
		{
			return true;
		}

		writer.WriteNames(names, namesWritten);
		namesWritten = names.Size();
		const bool ok = writer.Close();
		stats.FileBytes = writer.GetBytesWritten();
		return ok;
	}

	void ReplayRecorder::AddSource(IReplayCaptureSource& source)
	{
		if (std::find(sources.begin(), sources.end(), &source) == sources.end())
		{
			sources.push_back(&source);
		}
	}

	void ReplayRecorder::RemoveSource(IReplayCaptureSource& source)
	{
		std::erase(sources, &source);
	}

	void ReplayRecorder::RecordEvent(ReplayEvent event)
	{
		if (writer.IsOpen())
		{
			events.push_back(std::move(event));
		}
	}

	NodeState& ReplayRecorder::TouchNode(NodeId id)
	{
		const auto found = current.find(id);

		if (found != current.end())
		{
			return found->second;
		}

		const auto before = std::lower_bound(previous.begin(), previous.end(), id,
			[](const NodeState& n, NodeId k)
			{
				return n.Id < k;
			});

		if (before != previous.end() && before->Id == id)
		{
			return current.emplace(id, *before).first->second;
		}

		NodeState node;
		node.Id = id;
		return current.emplace(id, std::move(node)).first->second;
	}

	AssetId ReplayRecorder::Asset(AssetKind kind, std::string_view key, std::span<const std::uint8_t> data, std::uint64_t hash)
	{
		if (!data.empty() && hash == 0)
		{
			hash = HashBytes(data);
		}

		auto lookupKey = std::make_tuple(kind, std::string(key), hash);
		const auto found = assetLookup.find(lookupKey);

		if (found != assetLookup.end())
		{
			return found->second;
		}

		AssetRecord record;
		record.Kind = kind;
		record.Key = std::string(key);
		record.Hash = hash;
		const bool resolvable = data.empty() || (desc.IsResolvable ? desc.IsResolvable(record) : DefaultIsResolvable(record));

		if (!resolvable && !data.empty())
		{
			// The same bytes under another key are the same asset.
			const std::uint64_t contentKey = hash ^ (static_cast<std::uint64_t>(kind) << 56);
			const auto cached = cachedByHash.find(contentKey);

			if (cached != cachedByHash.end())
			{
				assetLookup.emplace(std::move(lookupKey), cached->second);
				return cached->second;
			}

			record.Data.assign(data.begin(), data.end());
			record.HasData = true;
			cachedByHash.emplace(contentKey, nextAsset);
			++stats.CachedAssets;
			stats.CachedBytes += data.size();
		}

		const AssetId id = nextAsset++;
		assetLookup.emplace(std::move(lookupKey), id);
		++stats.Assets;

		if (writer.IsOpen())
		{
			writer.WriteAsset(id, record);
		}

		return id;
	}

	void ReplayRecorder::Capture(double time)
	{
		if (!writer.IsOpen())
		{
			return;
		}

		current.clear();
		ReplayCapture capture;
		capture.recorder = this;
		capture.time = time;
		capture.session = session;

		for (IReplayCaptureSource* source : sources)
		{
			source->Capture(capture);
		}

		ReplayFrame frame;
		frame.Time = time;
		frame.Nodes.reserve(current.size());

		for (auto& [id, node] : current)
		{
			frame.Nodes.push_back(std::move(node));
		}

		current.clear();
		frame.Events.swap(events);
		const bool keyframe = first || time - lastKeyframe >= desc.KeyframeInterval;
		writer.WriteNames(names, namesWritten);
		namesWritten = names.Size();
		writer.WriteFrame(frame, first ? nullptr : &previous, keyframe);

		if (keyframe)
		{
			lastKeyframe = time;
			++stats.Keyframes;
		}

		first = false;
		++stats.Frames;
		stats.Nodes = frame.Nodes.size();
		stats.FileBytes = writer.GetBytesWritten();
		previous = std::move(frame.Nodes);
	}

} // namespace Swim::Replay
