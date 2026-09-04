#pragma once

#include <glm/glm.hpp>
#include <glm/ext/matrix_clip_space.hpp>

namespace Engine
{

	enum class WorldHandedness
	{
		RightHanded
	};

	enum class ClipSpaceDepthRange
	{
		ZeroToOne
	};

	enum class ClipSpaceYAxis
	{
		Up
	};

	enum class UiCoordinateOrigin
	{
		BottomLeft
	};

	// Canonical engine-space conventions. Backends adapt presentation/WSI
	// differences at their own boundary instead of changing Camera/Transform math.
	inline constexpr WorldHandedness CanonicalWorldHandedness = WorldHandedness::RightHanded;
	inline constexpr ClipSpaceDepthRange CanonicalClipSpaceDepthRange = ClipSpaceDepthRange::ZeroToOne;
	inline constexpr ClipSpaceYAxis CanonicalClipSpaceYAxis = ClipSpaceYAxis::Up;
	inline constexpr UiCoordinateOrigin CanonicalUiCoordinateOrigin = UiCoordinateOrigin::BottomLeft;

	inline glm::mat4 BuildCanonicalScreenProjection(float width, float height)
	{
		// Screen-space transforms store depth directly in [0,1], where smaller is
		// closer. orthoRH_ZO normally expects RH view-space Z to point toward -Z,
		// so a 0 -> -1 near/far pair makes positive component Z map directly to
		// canonical 0..1 clip depth without a backend-specific projection.
		return glm::orthoRH_ZO(0.0f, width, 0.0f, height, 0.0f, -1.0f);
	}

}
