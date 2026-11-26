#pragma once

#include "Engine/Systems/Animation/AnimationMath.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace Swim::Replay
{

	using Vec3 = Animation::Vec3;
	using Quat = Animation::Quat;
	using Vec2 = std::array<float, 2>;
	using Vec4 = std::array<float, 4>;

	// Stable per recording. Sources choose ids (entity ids, hashes of their own keys);
	// ids with the top bit set are reserved for nodes an edit project adds.
	using NodeId = std::uint64_t;
	inline constexpr NodeId NoNode = 0;
	inline constexpr NodeId ProjectNodeBit = 1ull << 63;

	// Index into a recording's asset table (or a project's added assets).
	using AssetId = std::uint32_t;
	inline constexpr AssetId NoAsset = 0xFFFFFFFFu;

	// Interned string (node kinds, property keys, event kinds). Index into the
	// recording's name table.
	using NameId = std::uint32_t;

	enum class AssetKind : std::uint8_t
	{
		Mesh,
		Texture,
		Material,
		Sound,
		Effect,
		Other,
	};

	const char* ToString(AssetKind kind);

	bool ParseAssetKind(std::string_view text, AssetKind& kind);

	// How an asset is stored. A resolvable key (a file path, a pack id, a registered
	// name the game recreates) is stored alone; otherwise the bytes are cached once per
	// content hash. A key may appear several times with different hashes: a texture
	// that changed at runtime is a new asset.
	struct AssetRecord
	{
		AssetKind Kind = AssetKind::Other;
		std::string Key;
		std::uint64_t Hash = 0;			// Content hash, 0 when unknown.
		std::vector<std::uint8_t> Data; // Empty for key-only assets.
		bool HasData = false;

		bool operator==(const AssetRecord&) const = default;
	};

	std::uint64_t HashBytes(std::span<const std::uint8_t> bytes, std::uint64_t seed = 0);

	std::uint64_t HashString(std::string_view text, std::uint64_t seed = 0);

	using Value = std::variant<bool, std::int64_t, float, Vec2, Vec3, Vec4, std::string, AssetId>;

	// Linear blend for float/vector values; other types step at t >= 0.5.
	Value Blend(const Value& a, const Value& b, float t);

	struct Transform
	{
		Vec3 Position{ 0.0f, 0.0f, 0.0f };
		Quat Rotation = Animation::IdentityQuat;
		Vec3 Scale{ 1.0f, 1.0f, 1.0f };

		bool operator==(const Transform&) const = default;
	};

	Transform Blend(const Transform& a, const Transform& b, float t);

	// a then b: b's transform expressed in a's frame (parent * child).
	Transform Compose(const Transform& parent, const Transform& child);

	Transform Inverse(const Transform& t);

	struct Property
	{
		NameId Key = 0;
		Value Data;

		bool operator==(const Property&) const = default;
	};

	// One thing in the scene. Kind says what the presenter makes of it ("mesh",
	// "light", "camera", "emitter", "environment" or any game kind); Assets are its
	// slots (a mesh: [mesh, material, material, ...]); Props carry everything else
	// (light colour, emitter settings, camera fov). Transform is world space unless
	// Parent is set, then it is relative to the parent.
	struct NodeState
	{
		NodeId Id = NoNode;
		NodeId Parent = NoNode;
		NameId Kind = 0;
		bool Visible = true;
		Transform Pose;
		std::vector<AssetId> Assets;
		std::vector<Property> Props; // Sorted by key.

		const Value* Find(NameId key) const;

		void Set(NameId key, Value value);

		bool Erase(NameId key);

		bool operator==(const NodeState&) const = default;
	};

	// A one-shot happening: a sound, a burst, a hit. Played when playback crosses
	// its time going forward.
	struct ReplayEvent
	{
		double Time = 0.0;
		NameId Kind = 0;
		NodeId Node = NoNode;
		AssetId Asset = NoAsset;
		Vec3 Position{ 0.0f, 0.0f, 0.0f };
		std::vector<Property> Props;

		bool operator==(const ReplayEvent&) const = default;
	};

	// Well-known names. A recording interns them like any other string; the engine
	// adapters and the edit project agree on these.
	namespace Names
	{

		inline constexpr std::string_view Mesh = "mesh";
		inline constexpr std::string_view Light = "light";
		inline constexpr std::string_view Camera = "camera";
		inline constexpr std::string_view Emitter = "emitter";
		inline constexpr std::string_view Environment = "environment";
		inline constexpr std::string_view Sound = "sound";

	} // namespace Names

} // namespace Swim::Replay
