#pragma once

#include "Engine/Assets/AssetId.h"

#include <compare>
#include <cstdint>

namespace Swim::Assets
{

	class AssetSystem;

	template<typename T>
	class AssetHandle
	{
	public:

		AssetHandle() = default;

		bool IsValid() const { return id.IsValid() && generation != 0; }
		explicit operator bool() const { return IsValid(); }

		AssetId GetId() const { return id; }
		std::uint32_t GetGeneration() const { return generation; }

		auto operator<=>(const AssetHandle&) const = default;

	private:

		AssetHandle(AssetId id, std::uint32_t generation)
			: id(id), generation(generation)
		{
		}

		AssetId id{};
		std::uint32_t generation = 0;

		friend class AssetSystem;
	};

}
