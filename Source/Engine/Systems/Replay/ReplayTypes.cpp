#include "Engine/Systems/Replay/ReplayTypes.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Swim::Replay
{

	const char* ToString(AssetKind kind)
	{
		switch (kind)
		{
		case AssetKind::Mesh:
			return "mesh";
		case AssetKind::Texture:
			return "texture";
		case AssetKind::Material:
			return "material";
		case AssetKind::Sound:
			return "sound";
		case AssetKind::Effect:
			return "effect";
		default:
			return "other";
		}
	}

	bool ParseAssetKind(std::string_view text, AssetKind& kind)
	{
		for (int i = 0; i <= static_cast<int>(AssetKind::Other); ++i)
		{
			if (text == ToString(static_cast<AssetKind>(i)))
			{
				kind = static_cast<AssetKind>(i);
				return true;
			}
		}

		return false;
	}

	namespace
	{

		constexpr std::uint64_t Prime1 = 0x9E3779B185EBCA87ull;
		constexpr std::uint64_t Prime2 = 0xC2B2AE3D27D4EB4Full;

		std::uint64_t Mix(std::uint64_t h)
		{
			h ^= h >> 33;
			h *= Prime2;
			h ^= h >> 29;
			h *= Prime1;
			h ^= h >> 32;
			return h;
		}

	} // namespace

	std::uint64_t HashBytes(std::span<const std::uint8_t> bytes, std::uint64_t seed)
	{
		std::uint64_t h = seed ^ (bytes.size() * Prime1);
		std::size_t i = 0;

		for (; i + 8 <= bytes.size(); i += 8)
		{
			std::uint64_t word;
			std::memcpy(&word, bytes.data() + i, 8);
			h = (h ^ Mix(word)) * Prime1 + Prime2;
		}

		std::uint64_t tail = 0;

		for (std::size_t k = 0; i + k < bytes.size(); ++k)
		{
			tail |= static_cast<std::uint64_t>(bytes[i + k]) << (8 * k);
		}

		h = (h ^ Mix(tail)) * Prime1;
		const std::uint64_t result = Mix(h);
		return result == 0 ? 1 : result; // 0 means "no hash".
	}

	std::uint64_t HashString(std::string_view text, std::uint64_t seed)
	{
		return HashBytes({ reinterpret_cast<const std::uint8_t*>(text.data()), text.size() }, seed);
	}

	Value Blend(const Value& a, const Value& b, float t)
	{
		if (a.index() != b.index())
		{
			return t < 0.5f ? a : b;
		}

		if (const auto* fa = std::get_if<float>(&a))
		{
			return *fa + (std::get<float>(b) - *fa) * t;
		}

		auto blend = [t](const auto& x, const auto& y)
		{
			auto out = x;

			for (std::size_t i = 0; i < out.size(); ++i)
			{
				out[i] = x[i] + (y[i] - x[i]) * t;
			}

			return out;
		};

		if (const auto* v = std::get_if<Vec2>(&a))
		{
			return blend(*v, std::get<Vec2>(b));
		}

		if (const auto* v = std::get_if<Vec3>(&a))
		{
			return blend(*v, std::get<Vec3>(b));
		}

		if (const auto* v = std::get_if<Vec4>(&a))
		{
			return blend(*v, std::get<Vec4>(b));
		}

		return t < 0.5f ? a : b;
	}

	Transform Blend(const Transform& a, const Transform& b, float t)
	{
		Transform out;
		out.Position = Animation::Lerp(a.Position, b.Position, t);
		out.Rotation = Animation::Slerp(a.Rotation, b.Rotation, t);
		out.Scale = Animation::Lerp(a.Scale, b.Scale, t);
		return out;
	}

	Transform Compose(const Transform& parent, const Transform& child)
	{
		Transform out;
		out.Position = Animation::Add(parent.Position, Animation::Rotate(parent.Rotation, Animation::Mul(parent.Scale, child.Position)));
		out.Rotation = Animation::Normalize(Animation::Multiply(parent.Rotation, child.Rotation));
		out.Scale = Animation::Mul(parent.Scale, child.Scale);
		return out;
	}

	Transform Inverse(const Transform& t)
	{
		auto safe = [](float s)
		{
			return std::abs(s) > 1.0e-8f ? 1.0f / s : 0.0f;
		};
		Transform out;
		out.Rotation = Animation::Inverse(t.Rotation);
		out.Scale = { safe(t.Scale[0]), safe(t.Scale[1]), safe(t.Scale[2]) };
		out.Position = Animation::Mul(out.Scale, Animation::Rotate(out.Rotation, Animation::Scale(t.Position, -1.0f)));
		return out;
	}

	const Value* NodeState::Find(NameId key) const
	{
		const auto found = std::lower_bound(Props.begin(), Props.end(), key,
			[](const Property& p, NameId k)
			{
				return p.Key < k;
			});
		return found != Props.end() && found->Key == key ? &found->Data : nullptr;
	}

	void NodeState::Set(NameId key, Value value)
	{
		const auto found = std::lower_bound(Props.begin(), Props.end(), key,
			[](const Property& p, NameId k)
			{
				return p.Key < k;
			});

		if (found != Props.end() && found->Key == key)
		{
			found->Data = std::move(value);
		}
		else
		{
			Props.insert(found, Property{ key, std::move(value) });
		}
	}

	bool NodeState::Erase(NameId key)
	{
		const auto found = std::lower_bound(Props.begin(), Props.end(), key,
			[](const Property& p, NameId k)
			{
				return p.Key < k;
			});

		if (found == Props.end() || found->Key != key)
		{
			return false;
		}

		Props.erase(found);
		return true;
	}

} // namespace Swim::Replay
