#include "Engine/Systems/Replay/ReplayFile.h"

#include <algorithm>
#include <cstring>

namespace Swim::Replay
{

	namespace
	{

		constexpr char Magic[4] = { 'S', 'W', 'R', 'P' };
		constexpr char FooterMagic[4] = { 'S', 'W', 'R', 'I' };
		constexpr std::uint64_t FooterSize = 12;
		constexpr std::uint32_t MaxChunk = 1u << 30;

		enum ChunkType : std::uint8_t
		{
			ChunkNames = 1,
			ChunkAsset = 2,
			ChunkKeyframe = 3,
			ChunkDelta = 4,
			ChunkIndex = 5,
		};

		// Node field mask in delta chunks.
		enum FieldBits : std::uint8_t
		{
			FieldParent = 1 << 0,
			FieldKind = 1 << 1,
			FieldVisible = 1 << 2,
			FieldPosition = 1 << 3,
			FieldRotation = 1 << 4,
			FieldScale = 1 << 5,
			FieldAssets = 1 << 6,
			FieldProps = 1 << 7,
		};

		class Writer
		{

		  public:
			std::vector<std::uint8_t> Bytes;

			void U8(std::uint8_t v) { Bytes.push_back(v); }

			void Varint(std::uint64_t v)
			{
				while (v >= 0x80)
				{
					Bytes.push_back(static_cast<std::uint8_t>(v | 0x80));
					v >>= 7;
				}

				Bytes.push_back(static_cast<std::uint8_t>(v));
			}

			void Signed(std::int64_t v) { Varint((static_cast<std::uint64_t>(v) << 1) ^ static_cast<std::uint64_t>(v >> 63)); }

			template <typename T> void Raw(const T& v)
			{
				const auto* p = reinterpret_cast<const std::uint8_t*>(&v);
				Bytes.insert(Bytes.end(), p, p + sizeof(T));
			}

			void Floats(const float* v, std::size_t n)
			{
				for (std::size_t i = 0; i < n; ++i)
				{
					Raw(v[i]);
				}
			}

			void String(std::string_view s)
			{
				Varint(s.size());
				Bytes.insert(Bytes.end(), s.begin(), s.end());
			}
		};

		class Reader
		{

		  public:
			Reader(const std::uint8_t* data, std::size_t size) : data(data), size(size) {}

			bool Ok() const { return ok; }

			bool AtEnd() const { return at >= size; }

			std::uint8_t U8()
			{
				if (at >= size)
				{
					ok = false;
					return 0;
				}

				return data[at++];
			}

			std::uint64_t Varint()
			{
				std::uint64_t v = 0;

				for (int shift = 0; shift < 64; shift += 7)
				{
					const std::uint8_t b = U8();
					v |= static_cast<std::uint64_t>(b & 0x7F) << shift;

					if (!(b & 0x80))
					{
						return v;
					}
				}

				ok = false;
				return 0;
			}

			std::int64_t Signed()
			{
				const std::uint64_t v = Varint();
				return static_cast<std::int64_t>(v >> 1) ^ -static_cast<std::int64_t>(v & 1);
			}

			template <typename T> T Raw()
			{
				T v{};

				if (at + sizeof(T) > size)
				{
					ok = false;
					return v;
				}

				std::memcpy(&v, data + at, sizeof(T));
				at += sizeof(T);
				return v;
			}

			void Floats(float* v, std::size_t n)
			{
				for (std::size_t i = 0; i < n; ++i)
				{
					v[i] = Raw<float>();
				}
			}

			std::string String()
			{
				const std::uint64_t n = Varint();

				if (!ok || at + n > size)
				{
					ok = false;
					return {};
				}

				std::string s(reinterpret_cast<const char*>(data + at), static_cast<std::size_t>(n));
				at += static_cast<std::size_t>(n);
				return s;
			}

			std::size_t Position() const { return at; }

			void Skip(std::size_t n)
			{
				if (at + n > size)
				{
					ok = false;
					at = size;
				}
				else
				{
					at += n;
				}
			}

		  private:
			const std::uint8_t* data;
			std::size_t size;
			std::size_t at = 0;
			bool ok = true;
		};

		void WriteValue(Writer& w, const Value& value)
		{
			w.U8(static_cast<std::uint8_t>(value.index()));
			std::visit(
				[&](const auto& v)
				{
					using T = std::decay_t<decltype(v)>;

					if constexpr (std::is_same_v<T, bool>)
					{
						w.U8(v ? 1 : 0);
					}
					else if constexpr (std::is_same_v<T, std::int64_t>)
					{
						w.Signed(v);
					}
					else if constexpr (std::is_same_v<T, float>)
					{
						w.Raw(v);
					}
					else if constexpr (std::is_same_v<T, std::string>)
					{
						w.String(v);
					}
					else if constexpr (std::is_same_v<T, AssetId>)
					{
						w.Varint(v);
					}
					else
					{
						w.Floats(v.data(), v.size());
					}
				},
				value);
		}

		Value ReadValue(Reader& r)
		{
			switch (r.U8())
			{
			case 0:
				return r.U8() != 0;
			case 1:
				return r.Signed();
			case 2:
				return r.Raw<float>();
			case 3:
			{
				Vec2 v;
				r.Floats(v.data(), 2);
				return v;
			}
			case 4:
			{
				Vec3 v;
				r.Floats(v.data(), 3);
				return v;
			}
			case 5:
			{
				Vec4 v;
				r.Floats(v.data(), 4);
				return v;
			}
			case 6:
				return r.String();
			case 7:
				return static_cast<AssetId>(r.Varint());
			default:
				return false;
			}
		}

		void WriteEvents(Writer& w, const std::vector<ReplayEvent>& events)
		{
			w.Varint(events.size());

			for (const auto& e : events)
			{
				w.Raw(e.Time);
				w.Varint(e.Kind);
				w.Varint(e.Node);
				w.Varint(static_cast<std::uint64_t>(e.Asset) + 1); // NoAsset -> 0.
				w.Floats(e.Position.data(), 3);
				w.Varint(e.Props.size());

				for (const auto& p : e.Props)
				{
					w.Varint(p.Key);
					WriteValue(w, p.Data);
				}
			}
		}

		void ReadEvents(Reader& r, std::vector<ReplayEvent>* out)
		{
			const std::uint64_t count = r.Varint();

			for (std::uint64_t i = 0; i < count && r.Ok(); ++i)
			{
				ReplayEvent e;
				e.Time = r.Raw<double>();
				e.Kind = static_cast<NameId>(r.Varint());
				e.Node = r.Varint();
				e.Asset = static_cast<AssetId>(r.Varint() - 1);
				r.Floats(e.Position.data(), 3);
				const std::uint64_t props = r.Varint();

				for (std::uint64_t k = 0; k < props && r.Ok(); ++k)
				{
					Property p;
					p.Key = static_cast<NameId>(r.Varint());
					p.Data = ReadValue(r);
					e.Props.push_back(std::move(p));
				}

				if (out)
				{
					out->push_back(std::move(e));
				}
			}
		}

		// Writes the fields of node that differ from base (a default node for new ones).
		void WriteNodeDelta(Writer& w, const NodeState& base, const NodeState& node)
		{
			std::uint8_t mask = 0;
			mask |= node.Parent != base.Parent ? FieldParent : 0;
			mask |= node.Kind != base.Kind ? FieldKind : 0;
			mask |= node.Visible != base.Visible ? FieldVisible : 0;
			mask |= node.Pose.Position != base.Pose.Position ? FieldPosition : 0;
			mask |= node.Pose.Rotation != base.Pose.Rotation ? FieldRotation : 0;
			mask |= node.Pose.Scale != base.Pose.Scale ? FieldScale : 0;
			mask |= node.Assets != base.Assets ? FieldAssets : 0;
			mask |= node.Props != base.Props ? FieldProps : 0;
			w.Varint(node.Id);
			w.U8(mask);

			if (mask & FieldParent)
			{
				w.Varint(node.Parent);
			}

			if (mask & FieldKind)
			{
				w.Varint(node.Kind);
			}

			if (mask & FieldVisible)
			{
				w.U8(node.Visible ? 1 : 0);
			}

			if (mask & FieldPosition)
			{
				w.Floats(node.Pose.Position.data(), 3);
			}

			if (mask & FieldRotation)
			{
				w.Floats(node.Pose.Rotation.data(), 4);
			}

			if (mask & FieldScale)
			{
				w.Floats(node.Pose.Scale.data(), 3);
			}

			if (mask & FieldAssets)
			{
				w.Varint(node.Assets.size());

				for (const AssetId a : node.Assets)
				{
					w.Varint(static_cast<std::uint64_t>(a) + 1);
				}
			}

			if (mask & FieldProps)
			{
				// Changed or added properties, then removed keys (both lists sorted).
				std::vector<const Property*> changed;
				std::vector<NameId> removed;
				auto b = base.Props.begin();
				auto n = node.Props.begin();

				while (b != base.Props.end() || n != node.Props.end())
				{
					if (n == node.Props.end() || (b != base.Props.end() && b->Key < n->Key))
					{
						removed.push_back((b++)->Key);
					}
					else if (b == base.Props.end() || n->Key < b->Key)
					{
						changed.push_back(&*n++);
					}
					else
					{
						if (!(b->Data == n->Data))
						{
							changed.push_back(&*n);
						}

						++b;
						++n;
					}
				}

				w.Varint(changed.size());

				for (const Property* p : changed)
				{
					w.Varint(p->Key);
					WriteValue(w, p->Data);
				}

				w.Varint(removed.size());

				for (const NameId k : removed)
				{
					w.Varint(k);
				}
			}
		}

		void ReadNodeDelta(Reader& r, NodeState& node)
		{
			const std::uint8_t mask = r.U8();

			if (mask & FieldParent)
			{
				node.Parent = r.Varint();
			}

			if (mask & FieldKind)
			{
				node.Kind = static_cast<NameId>(r.Varint());
			}

			if (mask & FieldVisible)
			{
				node.Visible = r.U8() != 0;
			}

			if (mask & FieldPosition)
			{
				r.Floats(node.Pose.Position.data(), 3);
			}

			if (mask & FieldRotation)
			{
				r.Floats(node.Pose.Rotation.data(), 4);
			}

			if (mask & FieldScale)
			{
				r.Floats(node.Pose.Scale.data(), 3);
			}

			if (mask & FieldAssets)
			{
				node.Assets.resize(static_cast<std::size_t>(std::min<std::uint64_t>(r.Varint(), 1u << 16)));

				for (auto& a : node.Assets)
				{
					a = static_cast<AssetId>(r.Varint() - 1);
				}
			}

			if (mask & FieldProps)
			{
				const std::uint64_t changed = r.Varint();

				for (std::uint64_t i = 0; i < changed && r.Ok(); ++i)
				{
					const auto key = static_cast<NameId>(r.Varint());
					node.Set(key, ReadValue(r));
				}

				const std::uint64_t removed = r.Varint();

				for (std::uint64_t i = 0; i < removed && r.Ok(); ++i)
				{
					node.Erase(static_cast<NameId>(r.Varint()));
				}
			}
		}

		auto FindNode(std::vector<NodeState>& nodes, NodeId id)
		{
			return std::lower_bound(nodes.begin(), nodes.end(), id,
				[](const NodeState& n, NodeId k)
				{
					return n.Id < k;
				});
		}

	} // namespace

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
