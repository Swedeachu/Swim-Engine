#pragma once

namespace Swim::Platform
{
	class FileSystem;
}

namespace Swim::Jobs
{
	class JobSystem;
}

namespace Swim::IO
{
	class AsyncIoService;
}

namespace Swim::Assets
{
	class AssetSystem;
}

namespace Swim::Memory
{
	class FrameArena;
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
		Swim::IO::AsyncIoService* IO = nullptr;
		Swim::Assets::AssetSystem* Assets = nullptr;
		Swim::Memory::FrameArena* FrameMemory = nullptr;
		MeshPool* Meshes = nullptr;
		TexturePool* Textures = nullptr;
		MaterialPool* Materials = nullptr;
		FontPool* Fonts = nullptr;

		bool IsValid() const
		{
			return Files && Jobs && IO && Assets && FrameMemory && Meshes && Textures && Materials && Fonts;
		}
	};

}
