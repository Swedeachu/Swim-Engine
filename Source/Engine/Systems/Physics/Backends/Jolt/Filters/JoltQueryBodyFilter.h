#pragma once

#include "Engine/Systems/Physics/Backends/Jolt/JoltWorldBackend.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyFilter.h>

namespace Engine
{

	class JoltWorldBackend::QueryBodyFilter final : public JPH::BodyFilter
	{

	public:

		QueryBodyFilter(const JoltWorldBackend& selectedOwner, CollisionLayer selectedFilter)
			: owner(selectedOwner), filter(selectedFilter)
		{}

		bool ShouldCollideLocked(const JPH::Body& body) const override
		{
			return owner.MatchesQuery(body, filter);
		}

	private:

		const JoltWorldBackend& owner;
		CollisionLayer filter{};

	};

} // namespace Engine
