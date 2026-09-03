#pragma once

namespace Swim::Platform
{
	class FileSystem;
}

namespace Swim::Jobs
{
	class JobSystem;
}

namespace Engine
{

	class MeshPool;
	class TexturePool;
	class MaterialPool;
	class FontPool;

	// Transitional runtime services for the legacy renderer/assets while the
	// Phase 4 asset system is built. Ownership remains in SwimEngine; renderers
	// and scenes receive only non-owning references.
	struct RendererRuntimeServices
	{
		Swim::Platform::FileSystem* Files = nullptr;
		Swim::Jobs::JobSystem* Jobs = nullptr;
		MeshPool* Meshes = nullptr;
		TexturePool* Textures = nullptr;
		MaterialPool* Materials = nullptr;
		FontPool* Fonts = nullptr;

		bool IsValid() const
		{
			return Files && Jobs && Meshes && Textures && Materials && Fonts;
		}
	};

}
