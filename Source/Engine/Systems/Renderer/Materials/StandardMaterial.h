#pragma once
#include "Engine/Systems/Renderer/Materials/MaterialInstance.h"
#include "Engine/Systems/Renderer/Materials/StandardPbr.h"

#include <memory>

namespace Swim::Render
{
	// The engine's built-in metallic-roughness material (items 58-60). Its record is
	// Shaders/Slang/Materials/StandardMaterialParameters.slang; the tests prove this
	// hand-written layout equals the compiled shader's reflection, so the runtime
	// does not need the shader compiler to create it.
	inline constexpr std::uint32_t StandardMaterialRecordSize = 80;

	MaterialTemplateDesc StandardMaterialTemplateDesc();

	// A shared template with the glTF defaults: base color (1, 1, 1, 1), metallic 1,
	// roughness 1, normal scale 1, occlusion strength 1, alpha cutoff 0.5, no
	// emission; every texture/sampler index is the bindless fallback (0).
	std::shared_ptr<const MaterialTemplate> CreateStandardMaterialTemplate();

	struct StandardMaterialTextures
	{
		std::uint32_t BaseColor = 0;
		std::uint32_t MetallicRoughness = 0;
		std::uint32_t Normal = 0;
		std::uint32_t Occlusion = 0;
		std::uint32_t Emissive = 0;
		std::uint32_t Sampler = 0;
	};

	// Decodes an instance of the standard template (throws for other layouts).
	StandardPbr::Parameters ReadStandardParameters(const MaterialInstance& instance);
	StandardMaterialTextures ReadStandardTextures(const MaterialInstance& instance);
} // namespace Swim::Render
