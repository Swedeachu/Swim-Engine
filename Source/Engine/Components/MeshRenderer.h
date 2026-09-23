#pragma once

#include "Engine/Assets/AssetHandle.h"
#include "Engine/Assets/MeshAsset.h"
#include "Engine/Systems/Renderer/GpuScene/GpuInstanceRecord.h"
#include "Engine/Systems/Renderer/GpuScene/RenderObjectFlags.h"

#include <cstdint>
#include <vector>

namespace Engine
{

	// One drawable piece of an entity: a compiled mesh and the material set its
	// submeshes use. Its index in MeshRenderer::Parts is the render sub-object id.
	struct MeshRendererPart
	{
		Swim::Assets::AssetHandle<Swim::Assets::MeshAsset> Mesh;
		std::uint32_t MaterialSet = Swim::Render::GpuInstanceRecord::InvalidIndex;

		bool operator==(const MeshRendererPart&) const = default;
	};

	// Modern scene render binding consumed by RenderExtractor (critical-path item
	// 47). Every part becomes one persistent GPU Scene RenderObject that follows
	// the entity's world transform. Change it through registry.patch/replace or
	// emplace_or_replace so the extractor observes the update; plain writes via
	// registry.get are not seen. The legacy Material component is unaffected.
	struct MeshRenderer
	{
		std::vector<MeshRendererPart> Parts;
		Swim::Render::RenderObjectFlags Flags = Swim::Render::RenderObjectFlags::Default;
		float LodBias = 0.0f;
		std::uint32_t SkinIndex = Swim::Render::GpuInstanceRecord::InvalidIndex;
	};

} // namespace Engine
