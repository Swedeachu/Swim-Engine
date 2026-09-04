#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace Engine
{

	template<typename Tag>
	struct PhysicsHandle
	{
		static constexpr std::uint32_t InvalidIndex = std::numeric_limits<std::uint32_t>::max();

		std::uint32_t Index = InvalidIndex;
		std::uint32_t Generation = 0;

		bool IsValid() const
		{
			return Index != InvalidIndex;
		}

		explicit operator bool() const
		{
			return IsValid();
		}

		auto operator<=>(const PhysicsHandle&) const = default;
	};

	struct BodyHandleTag {};
	struct ShapeHandleTag {};
	struct PhysicsMaterialHandleTag {};
	struct ConstraintHandleTag {};
	struct CharacterHandleTag {};

	using BodyHandle = PhysicsHandle<BodyHandleTag>;
	using ShapeHandle = PhysicsHandle<ShapeHandleTag>;
	using PhysicsMaterialHandle = PhysicsHandle<PhysicsMaterialHandleTag>;
	using ConstraintHandle = PhysicsHandle<ConstraintHandleTag>;
	using CharacterHandle = PhysicsHandle<CharacterHandleTag>;

	template<typename Handle>
	struct PhysicsHandleHash
	{
		std::size_t operator()(const Handle& handle) const noexcept
		{
			const std::uint64_t packed = (static_cast<std::uint64_t>(handle.Generation) << 32u)
				| static_cast<std::uint64_t>(handle.Index);
			return static_cast<std::size_t>(packed ^ (packed >> 33u));
		}
	};


}
