#include "Engine/Systems/Replay/ReplayFile.h"
#include "Engine/Systems/Replay/ReplayFileCodec.h"

#include <algorithm>
#include <cstring>

namespace Swim::Replay
{

	using namespace ReplayCodec;

	NameId NameTable::Intern(std::string_view name)
	{
		const auto found = lookup.find(std::string(name));

		if (found != lookup.end())
		{
			return found->second;
		}

		const auto id = static_cast<NameId>(names.size());
		names.emplace_back(name);
		lookup.emplace(names.back(), id);
		return id;
	}

	std::optional<NameId> NameTable::Find(std::string_view name) const
	{
		const auto found = lookup.find(std::string(name));
		return found == lookup.end() ? std::nullopt : std::optional<NameId>(found->second);
	}

	const std::string& NameTable::Get(NameId id) const
	{
		static const std::string empty;
		return id < names.size() ? names[id] : empty;
	}

	// ---- Writer ------------------------------------------------------------------

	ReplayWriter::~ReplayWriter()
	{
		Close();
	}

	bool ReplayWriter::Open(const std::filesystem::path& path, std::string_view metadata)
	{
		Close();
		file.open(path, std::ios::binary | std::ios::trunc);

		if (!file)
		{
			return false;
		}

		open = true;
		closing = false;
		failed = false;
		offset = 0;
		frames.clear();
		assets.clear();
		names.clear();
		Writer header;
		header.Bytes.insert(header.Bytes.end(), Magic, Magic + 4);
		header.Raw(ReplayFormatVersion);
		header.String(metadata);
		offset = header.Bytes.size();
		pending.push_back(std::move(header.Bytes));
		thread = std::thread(
			[this]
			{
				Drain();
			});
		return true;
	}

	void ReplayWriter::Enqueue(std::uint8_t type, std::vector<std::uint8_t> payload)
	{
		Writer chunk;
		chunk.U8(type);
		chunk.Raw(static_cast<std::uint32_t>(payload.size()));
		chunk.Bytes.insert(chunk.Bytes.end(), payload.begin(), payload.end());
		offset += chunk.Bytes.size();

		{
			std::lock_guard lock(mutex);
			pending.push_back(std::move(chunk.Bytes));
		}

		wake.notify_one();
	}

	void ReplayWriter::Drain()
	{
		while (true)
		{
			std::vector<std::uint8_t> bytes;

			{
				std::unique_lock lock(mutex);
				wake.wait(lock,
					[this]
					{
						return closing || !pending.empty();
					});

				if (pending.empty())
				{
					return;
				}

				bytes = std::move(pending.front());
				pending.pop_front();
			}

			file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));

			if (!file)
			{
				std::lock_guard lock(mutex);
				failed = true;
			}
		}
	}

	void ReplayWriter::WriteNames(const NameTable& table, std::size_t first)
	{
		if (!open || first >= table.Size())
		{
			return;
		}

		Writer w;
		w.Varint(first);
		w.Varint(table.Size() - first);

		for (std::size_t i = first; i < table.Size(); ++i)
		{
			w.String(table.All()[i]);
			names.push_back(table.All()[i]);
		}

		Enqueue(ChunkNames, std::move(w.Bytes));
	}

	void ReplayWriter::WriteAsset(AssetId id, const AssetRecord& asset)
	{
		if (!open)
		{
			return;
		}

		Writer w;
		w.Varint(id);
		w.U8(static_cast<std::uint8_t>(asset.Kind));
		w.String(asset.Key);
		w.Raw(asset.Hash);
		w.U8(asset.HasData ? 1 : 0);
		w.Varint(asset.HasData ? asset.Data.size() : 0);
		const std::size_t headerSize = w.Bytes.size();

		if (asset.HasData)
		{
			w.Bytes.insert(w.Bytes.end(), asset.Data.begin(), asset.Data.end());
		}

		AssetEntry entry;
		entry.Id = id;
		entry.Meta = asset;
		entry.Meta.Data.clear();
		entry.DataOffset = offset + 5 + headerSize; // Chunk header is 5 bytes.
		entry.DataSize = asset.HasData ? asset.Data.size() : 0;
		assets.push_back(std::move(entry));
		Enqueue(ChunkAsset, std::move(w.Bytes));
	}

	void ReplayWriter::WriteFrame(const ReplayFrame& frame, const std::vector<NodeState>* previous, bool keyframe)
	{
		if (!open)
		{
			return;
		}

		Writer w;
		w.Raw(frame.Time);
		static const std::vector<NodeState> none;
		const auto& base = keyframe || !previous ? none : *previous;

		if (keyframe || !previous)
		{
			keyframe = true;
			w.Varint(frame.Nodes.size());
			const NodeState empty;

			for (const auto& node : frame.Nodes)
			{
				WriteNodeDelta(w, empty, node);
			}
		}
		else
		{
			// Merge walk over the two sorted lists.
			std::vector<NodeId> removed;
			Writer changed;
			std::size_t changedCount = 0;
			auto b = base.begin();
			auto n = frame.Nodes.begin();
			const NodeState empty;

			while (b != base.end() || n != frame.Nodes.end())
			{
				if (n == frame.Nodes.end() || (b != base.end() && b->Id < n->Id))
				{
					removed.push_back((b++)->Id);
				}
				else if (b == base.end() || n->Id < b->Id)
				{
					WriteNodeDelta(changed, empty, *n++);
					++changedCount;
				}
				else
				{
					if (!(*b == *n))
					{
						WriteNodeDelta(changed, *b, *n);
						++changedCount;
					}

					++b;
					++n;
				}
			}

			w.Varint(removed.size());

			for (const NodeId id : removed)
			{
				w.Varint(id);
			}

			w.Varint(changedCount);
			w.Bytes.insert(w.Bytes.end(), changed.Bytes.begin(), changed.Bytes.end());
		}

		WriteEvents(w, frame.Events);
		frames.push_back({ frame.Time, offset, keyframe });
		Enqueue(keyframe ? ChunkKeyframe : ChunkDelta, std::move(w.Bytes));
	}

	bool ReplayWriter::Close()
	{
		if (!open)
		{
			return !failed;
		}

		Writer w;
		w.Varint(frames.size());

		for (const auto& f : frames)
		{
			w.Raw(f.Time);
			w.Raw(f.Offset);
			w.U8(f.Keyframe ? 1 : 0);
		}

		w.Varint(assets.size());

		for (const auto& a : assets)
		{
			w.Varint(a.Id);
			w.U8(static_cast<std::uint8_t>(a.Meta.Kind));
			w.String(a.Meta.Key);
			w.Raw(a.Meta.Hash);
			w.U8(a.Meta.HasData ? 1 : 0);
			w.Raw(a.DataOffset);
			w.Raw(a.DataSize);
		}

		w.Varint(names.size());

		for (const auto& name : names)
		{
			w.String(name);
		}

		const std::uint64_t indexOffset = offset;
		Enqueue(ChunkIndex, std::move(w.Bytes));
		Writer footer;
		footer.Raw(indexOffset);
		footer.Bytes.insert(footer.Bytes.end(), FooterMagic, FooterMagic + 4);
		offset += footer.Bytes.size();

		{
			std::lock_guard lock(mutex);
			pending.push_back(std::move(footer.Bytes));
			closing = true;
		}

		wake.notify_one();

		if (thread.joinable())
		{
			thread.join();
		}

		file.flush();
		failed = failed || !file;
		file.close();
		open = false;
		return !failed;
	}

	// ---- Reader ------------------------------------------------------------------

} // namespace Swim::Replay
