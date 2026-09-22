#pragma once
#include "Engine/Systems/Renderer/RenderGraph/GraphHandle.h"
#include "Engine/Systems/Renderer/Resources/GpuHandle.h"
#include <vector>

namespace Swim::Render
{
	// Textures uploaded by one TextureResidency::Import. Each is imported once
	// (Undefined), fully written and exported to ShaderRead, so passes later in
	// the same graph may sample it through these handles. Resident textures from
	// earlier graphs are not imported; renderers import them as ShaderRead.
	struct TextureGraphResources
	{
		struct Upload
		{
			GpuTextureHandle Texture;
			GraphTexture Graph;
			GraphPass Pass;
		};

		std::vector<Upload> Uploads;
		std::uint64_t RecordedBytes = 0;
	};
} // namespace Swim::Render
