#pragma once

#include <cstdint>

namespace Engine
{

	// Counters for one Extract call (and running totals where noted).
	struct RenderExtractionStats
	{
		std::uint32_t EntitiesChanged = 0;	 // MeshRenderer constructed/updated.
		std::uint32_t EntitiesDestroyed = 0; // MeshRenderer or entity removed.
		std::uint32_t ObjectsCreated = 0;
		std::uint32_t ObjectsDestroyed = 0;
		std::uint32_t ObjectsUpdated = 0;	 // Existing parts reconciled with a changed component.
		std::uint32_t TransformsWritten = 0; // Parts whose transform was pushed to the GPU Scene.
		std::uint32_t MeshesResolved = 0;
		std::uint32_t PendingMeshes = 0;	// Parts still waiting for a resident mesh (after this call).
		std::uint32_t CapacityFailures = 0; // Parts that found the GPU Scene full; retried next call.
		std::uint32_t LiveObjects = 0;		// Running total after this call.
		std::uint32_t TrackedEntities = 0;	// Running total after this call.
	};

} // namespace Engine
