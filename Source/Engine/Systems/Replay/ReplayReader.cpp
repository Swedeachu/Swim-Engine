#include "Engine/Systems/Replay/ReplayFileCodec.h"

namespace Swim::Replay
{

	using namespace ReplayCodec;

	bool ReplayReader::Open(const std::filesystem::path& path, std::string* error)
	{
		Close();
		auto fail = [&](const char* what)
		{
			if (error)
			{
				*error = what;
			}

			Close();
			return false;
		};

		file.open(path, std::ios::binary);

		if (!file)
		{
			return fail("cannot open the recording");
		}

		file.seekg(0, std::ios::end);
		fileSize = static_cast<std::uint64_t>(file.tellg());
		file.seekg(0);
		char magic[4] = {};
		std::uint32_t version = 0;
		file.read(magic, 4);
		file.read(reinterpret_cast<char*>(&version), 4);

		if (!file || std::memcmp(magic, Magic, 4) != 0)
		{
			return fail("not a replay recording");
		}

		if (version != ReplayFormatVersion)
		{
			return fail("unsupported recording version");
		}

		// Metadata string (varint length).
		std::uint64_t length = 0;

		for (int shift = 0; shift < 64; shift += 7)
		{
			const int b = file.get();

			if (b < 0)
			{
				return fail("truncated header");
			}

			length |= static_cast<std::uint64_t>(b & 0x7F) << shift;

			if (!(b & 0x80))
			{
				break;
			}
		}

		if (length > fileSize)
		{
			return fail("bad header");
		}

		metadata.resize(static_cast<std::size_t>(length));
		file.read(metadata.data(), static_cast<std::streamsize>(length));
		const std::uint64_t firstChunk = static_cast<std::uint64_t>(file.tellg());

		if (fileSize >= firstChunk + FooterSize)
		{
			char footerMagic[4] = {};
			std::uint64_t indexOffset = 0;
			file.seekg(static_cast<std::streamoff>(fileSize - FooterSize));
			file.read(reinterpret_cast<char*>(&indexOffset), 8);
			file.read(footerMagic, 4);

			if (file && std::memcmp(footerMagic, FooterMagic, 4) == 0 && ReadIndex(indexOffset))
			{
				return true;
			}

			file.clear();
		}

		recovered = true;
		names = {};
		frames.clear();
		assets.clear();

		if (!Scan(firstChunk))
		{
			return fail("unreadable recording");
		}

		return true;
	}

	void ReplayReader::Close()
	{
		if (file.is_open())
		{
			file.close();
		}

		file.clear();
		metadata.clear();
		names = {};
		frames.clear();
		assets.clear();
		recovered = false;
		fileSize = 0;
	}

	bool ReplayReader::ReadChunk(std::uint64_t at, std::uint8_t& type, std::vector<std::uint8_t>& payload)
	{
		if (at + 5 > fileSize)
		{
			return false;
		}

		file.clear();
		file.seekg(static_cast<std::streamoff>(at));
		std::uint32_t size = 0;
		type = static_cast<std::uint8_t>(file.get());
		file.read(reinterpret_cast<char*>(&size), 4);

		if (!file || size > MaxChunk || at + 5 + size > fileSize)
		{
			return false;
		}

		payload.resize(size);
		file.read(reinterpret_cast<char*>(payload.data()), size);
		return static_cast<bool>(file);
	}

	bool ReplayReader::ReadIndex(std::uint64_t indexOffset)
	{
		std::uint8_t type = 0;
		std::vector<std::uint8_t> payload;

		if (!ReadChunk(indexOffset, type, payload) || type != ChunkIndex)
		{
			return false;
		}

		Reader r(payload.data(), payload.size());
		const std::uint64_t frameCount = r.Varint();

		for (std::uint64_t i = 0; i < frameCount && r.Ok(); ++i)
		{
			FrameEntry f;
			f.Time = r.Raw<double>();
			f.Offset = r.Raw<std::uint64_t>();
			f.Keyframe = r.U8() != 0;
			frames.push_back(f);
		}

		const std::uint64_t assetCount = r.Varint();

		for (std::uint64_t i = 0; i < assetCount && r.Ok(); ++i)
		{
			const auto id = static_cast<AssetId>(r.Varint());
			AssetEntry a;
			a.Meta.Kind = static_cast<AssetKind>(r.U8());
			a.Meta.Key = r.String();
			a.Meta.Hash = r.Raw<std::uint64_t>();
			a.Meta.HasData = r.U8() != 0;
			a.DataOffset = r.Raw<std::uint64_t>();
			a.DataSize = r.Raw<std::uint64_t>();

			if (id >= assets.size())
			{
				assets.resize(static_cast<std::size_t>(id) + 1);
			}

			assets[id] = std::move(a);
		}

		const std::uint64_t nameCount = r.Varint();

		for (std::uint64_t i = 0; i < nameCount && r.Ok(); ++i)
		{
			names.Intern(r.String());
		}

		if (!r.Ok())
		{
			names = {};
			frames.clear();
			assets.clear();
			return false;
		}

		return true;
	}

	bool ReplayReader::Scan(std::uint64_t at)
	{
		std::vector<std::uint8_t> payload;

		while (at + 5 <= fileSize)
		{
			std::uint8_t type = 0;

			if (!ReadChunk(at, type, payload))
			{
				break; // A torn last chunk: keep everything before it.
			}

			Reader r(payload.data(), payload.size());

			if (type == ChunkNames)
			{
				const std::uint64_t first = r.Varint();
				const std::uint64_t count = r.Varint();

				for (std::uint64_t i = 0; i < count && r.Ok(); ++i)
				{
					const std::string name = r.String();

					if (first + i == names.Size())
					{
						names.Intern(name);
					}
				}
			}
			else if (type == ChunkAsset)
			{
				const auto id = static_cast<AssetId>(r.Varint());
				AssetEntry a;
				a.Meta.Kind = static_cast<AssetKind>(r.U8());
				a.Meta.Key = r.String();
				a.Meta.Hash = r.Raw<std::uint64_t>();
				a.Meta.HasData = r.U8() != 0;
				a.DataSize = r.Varint();
				a.DataOffset = at + 5 + r.Position();

				if (id >= assets.size())
				{
					assets.resize(static_cast<std::size_t>(id) + 1);
				}

				assets[id] = std::move(a);
			}
			else if (type == ChunkKeyframe || type == ChunkDelta)
			{
				frames.push_back({ r.Raw<double>(), at, type == ChunkKeyframe });
			}
			else if (type == ChunkIndex)
			{
				break;
			}

			at += 5 + payload.size();
		}

		// A recording must start from a keyframe to be playable.
		while (!frames.empty() && !frames.front().Keyframe)
		{
			frames.erase(frames.begin());
		}

		return true;
	}

	std::size_t ReplayReader::FindFrame(double time) const
	{
		if (frames.empty())
		{
			return 0;
		}

		const auto found = std::upper_bound(frames.begin(), frames.end(), time,
			[](double t, const FrameEntry& f)
			{
				return t < f.Time;
			});
		return found == frames.begin() ? 0 : static_cast<std::size_t>(found - frames.begin() - 1);
	}

	std::size_t ReplayReader::FindKeyframe(std::size_t frame) const
	{
		frame = std::min(frame, frames.empty() ? 0 : frames.size() - 1);

		while (frame > 0 && !frames[frame].Keyframe)
		{
			--frame;
		}

		return frame;
	}

	const AssetRecord* ReplayReader::GetAsset(AssetId id) const
	{
		return id < assets.size() ? &assets[id].Meta : nullptr;
	}

	std::vector<std::uint8_t> ReplayReader::LoadAssetData(AssetId id)
	{
		std::vector<std::uint8_t> data;

		if (id >= assets.size() || !assets[id].Meta.HasData || assets[id].DataOffset + assets[id].DataSize > fileSize)
		{
			return data;
		}

		data.resize(static_cast<std::size_t>(assets[id].DataSize));
		file.clear();
		file.seekg(static_cast<std::streamoff>(assets[id].DataOffset));
		file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));

		if (!file)
		{
			data.clear();
		}

		return data;
	}

	bool ReplayReader::ApplyFrame(std::size_t frame, std::vector<NodeState>& state, std::vector<ReplayEvent>* events)
	{
		if (frame >= frames.size())
		{
			return false;
		}

		std::uint8_t type = 0;
		std::vector<std::uint8_t> payload;

		if (!ReadChunk(frames[frame].Offset, type, payload) || (type != ChunkKeyframe && type != ChunkDelta))
		{
			return false;
		}

		Reader r(payload.data(), payload.size());
		r.Raw<double>();

		if (type == ChunkKeyframe)
		{
			state.clear();
			const std::uint64_t count = r.Varint();

			for (std::uint64_t i = 0; i < count && r.Ok(); ++i)
			{
				NodeState node;
				node.Id = r.Varint();
				ReadNodeDelta(r, node);
				state.push_back(std::move(node));
			}

			std::sort(state.begin(), state.end(),
				[](const NodeState& a, const NodeState& b)
				{
					return a.Id < b.Id;
				});
		}
		else
		{
			const std::uint64_t removed = r.Varint();

			for (std::uint64_t i = 0; i < removed && r.Ok(); ++i)
			{
				const NodeId id = r.Varint();
				const auto found = FindNode(state, id);

				if (found != state.end() && found->Id == id)
				{
					state.erase(found);
				}
			}

			const std::uint64_t changed = r.Varint();

			for (std::uint64_t i = 0; i < changed && r.Ok(); ++i)
			{
				const NodeId id = r.Varint();
				auto found = FindNode(state, id);

				if (found == state.end() || found->Id != id)
				{
					NodeState node;
					node.Id = id;
					found = state.insert(found, std::move(node));
				}

				ReadNodeDelta(r, *found);
			}
		}

		ReadEvents(r, events);
		return r.Ok();
	}

	bool ReplayReader::ReadState(std::size_t frame, std::vector<NodeState>& state)
	{
		if (frame >= frames.size())
		{
			return false;
		}

		const std::size_t key = FindKeyframe(frame);

		for (std::size_t f = key; f <= frame; ++f)
		{
			if (!ApplyFrame(f, state))
			{
				return false;
			}
		}

		return true;
	}

} // namespace Swim::Replay
