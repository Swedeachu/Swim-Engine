#pragma once

#include <compare>
#include <cstdint>

namespace Engine
{

	class SceneSystem;

	class SceneId
	{

	public:

		SceneId() = default;

		bool IsValid() const { return value != 0; }
		explicit operator bool() const { return IsValid(); }

		std::uint64_t GetValue() const { return value; }

		auto operator<=>(const SceneId&) const = default;

	private:

		explicit SceneId(std::uint64_t value)
			: value(value)
		{
		}

		std::uint64_t value = 0;

		friend class SceneSystem;

	};

}
