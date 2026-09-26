#pragma once

#include "Engine/Systems/Renderer/GpuScene/GpuInstanceRecord.h"
#include "Engine/Systems/Renderer/GpuScene/RenderObjectFlags.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Engine
{
	// A GPU-skinned mesh instance on an entity (Phase 23). Mesh names a skinned mesh
	// registered with the render bridge (SceneRenderBridge::RegisterSkinnedMesh). Each
	// frame the bridge uploads Palette (JointCount row-major 3x4 matrices: joint model
	// transform x inverse bind) when PoseRevision changed; the SkinningSystem deforms the
	// instance's own output mesh on the GPU and every pass (visibility, shadows, Forward+
	// with motion vectors) draws it like any mesh.
	struct SkinnedMeshRenderer
	{
		std::string Mesh;
		std::uint32_t MaterialSet = Swim::Render::GpuInstanceRecord::InvalidIndex;
		Swim::Render::RenderObjectFlags Flags = Swim::Render::RenderObjectFlags::Default;
		std::vector<std::array<float, 12>> Palette;
		std::uint64_t PoseRevision = 0;
	};
} // namespace Engine
