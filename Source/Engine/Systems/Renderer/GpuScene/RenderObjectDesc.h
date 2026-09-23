#pragma once
#include "Engine/Systems/Renderer/GpuScene/GpuInstanceRecord.h"
#include "Engine/Systems/Renderer/GpuScene/RenderAffine.h"
#include "Engine/Systems/Renderer/GpuScene/RenderBounds.h"
#include "Engine/Systems/Renderer/GpuScene/RenderObjectFlags.h"
#include "Engine/Systems/Renderer/Resources/GpuHandle.h"

namespace Swim::Render
{
	// Initial state of a render object. Every field can change later through the
	// GpuScene setters; an invalid Mesh leaves the object undrawable until SetMesh.
	struct RenderObjectDesc
	{
		RenderAffine Transform{};
		GpuMeshHandle Mesh{};
		RenderBounds LocalBounds = RenderBounds::Infinite();
		std::uint32_t MaterialSet = GpuInstanceRecord::InvalidIndex;
		std::uint32_t ObjectId = 0;
		RenderObjectFlags Flags = RenderObjectFlags::Default; // Producer bits only.
		std::uint32_t SkinIndex = GpuInstanceRecord::InvalidIndex;
		float LodBias = 0.0f;
	};
} // namespace Swim::Render
