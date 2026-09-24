#pragma once
#include "Engine/Systems/Renderer/ForwardPlus/ForwardPlusRecords.h"
#include "Engine/Systems/Renderer/RenderGraph/GraphHandle.h"

#include <cstdint>

namespace Swim::Render
{
	// What one ForwardPlusRenderer::Record scheduled. SortedCommands/SortedCounts
	// are the transparent bin's draws after the back-to-front sort (per page slot:
	// Capacity commands from slot * Capacity, one count), useful for diagnostics.
	struct ForwardPlusGraphResources
	{
		GraphBuffer View;			// One ForwardViewRecord.
		GraphBuffer SortScratch;	// ForwardSortEntry per slot * SortSize.
		GraphBuffer SortedCommands; // DrawIndexedIndirectCommand per slot * TransparentCapacity.
		GraphBuffer SortedCounts;	// uint per page slot.
		GraphTexture Velocity;		// ForwardPlusTargets::Velocity, or the transient one used in its place.
		GraphTexture Normal;		// ForwardPlusTargets::Normal, or the transient one.
		GraphTexture Indirect;		// ForwardPlusTargets::Indirect, or the transient one.
		GraphTexture Reflectance;	// ForwardPlusTargets::Reflectance, or the transient one.
		GraphTexture Specular;		// ForwardPlusTargets::Specular, or the transient one.
		GraphPass OpaquePass;
		GraphPass SortPass;
		GraphPass TransparentPass;
		ForwardViewRecord ViewRecord;
		std::uint32_t TransparentCapacity = 0; // Per page slot.
		std::uint32_t SortSize = 0;			   // Next power of two of TransparentCapacity.
		bool EnvironmentFallback = false;	   // No environment: 1x1 zero stand-ins were bound.
		bool ShadowFallback = false;		   // No shadows: a 1x1 atlas and an empty record were bound.
	};
} // namespace Swim::Render
