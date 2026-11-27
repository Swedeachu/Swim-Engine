#pragma once
#include "Engine/Systems/Replay/ReplayFile.h"

#include <algorithm>
#include <cstring>

// The replay file's byte codec, shared by ReplayFile.cpp (names and the writer) and
// ReplayReader.cpp.
namespace Swim::Replay::ReplayCodec
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

	inline void WriteValue(Writer& w, const Value& value)
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

	inline Value ReadValue(Reader& r)
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

	inline void WriteEvents(Writer& w, const std::vector<ReplayEvent>& events)
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

	inline void ReadEvents(Reader& r, std::vector<ReplayEvent>* out)
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
	inline void WriteNodeDelta(Writer& w, const NodeState& base, const NodeState& node)
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

	inline void ReadNodeDelta(Reader& r, NodeState& node)
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

	inline auto FindNode(std::vector<NodeState>& nodes, NodeId id)
	{
		return std::lower_bound(nodes.begin(), nodes.end(), id,
			[](const NodeState& n, NodeId k)
			{
				return n.Id < k;
			});
	}

} // namespace Swim::Replay::ReplayCodec
