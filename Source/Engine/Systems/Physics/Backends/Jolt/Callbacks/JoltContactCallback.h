#pragma once

#include "Engine/Systems/Physics/Backends/Jolt/JoltWorldBackend.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/ContactListener.h>

namespace Engine
{

	class JoltWorldBackend::ContactCallback final : public JPH::ContactListener
	{

	public:

		explicit ContactCallback(JoltWorldBackend& selectedOwner)
			: owner(selectedOwner)
		{}

		void OnContactAdded(
			const JPH::Body& bodyA,
			const JPH::Body& bodyB,
			const JPH::ContactManifold& manifold,
			JPH::ContactSettings& settings) override
		{
			owner.RecordContact(bodyA, bodyB, manifold, settings, false);
		}

		void OnContactPersisted(
			const JPH::Body& bodyA,
			const JPH::Body& bodyB,
			const JPH::ContactManifold& manifold,
			JPH::ContactSettings& settings) override
		{
			owner.RecordContact(bodyA, bodyB, manifold, settings, true);
		}

		void OnContactRemoved(const JPH::SubShapeIDPair& pair) override
		{
			owner.RemoveContact(pair);
		}

	private:

		JoltWorldBackend& owner;

	};

} // namespace Engine
