#pragma once

#include "Engine/Assets/AssetHandle.h"
#include "Engine/Assets/MeshAsset.h"
#include "Engine/Assets/TextureAsset.h"
#include "Engine/Systems/Renderer/RHI/RhiContracts.h"
#include "Engine/Systems/Renderer/Resources/GpuHandle.h"
#include "Engine/Systems/Renderer/Runtime/ProceduralMeshes.h"

#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Swim::Assets
{

	class AssetSystem;

}

namespace Swim::Render
{

	class AssetResidencyService;
	class GeometryHeap;

} // namespace Swim::Render

namespace Engine
{

	// The built-in procedural meshes (unit sizes, centered on the origin).
	enum class BuiltinMesh : std::uint8_t
	{
		Cube,	  // 1 x 1 x 1.
		Sphere,	  // Diameter 1.
		Plane,	  // 1 x 1 on XZ, facing +Y.
		Cylinder, // Diameter 1, height 1.
		Cone,	  // Diameter 1, height 1.
		Torus,	  // Outer diameter 1.1.
		Capsule,  // Diameter 0.5, height 1.
		Count
	};

	// Meshes and textures as assets (Phase 23): procedural geometry is published into the
	// AssetSystem as ordinary MeshAssets ("Procedural/<Name>") and streamed to the GPU by
	// AssetResidencyService exactly like cooked models, so MeshRenderer, render extraction
	// and residency treat every mesh the same way. Cooked assets are requested by logical
	// path. Owner thread only.
	class MeshLibrary
	{

	  public:

		MeshLibrary(Swim::Assets::AssetSystem& assets, Swim::Render::AssetResidencyService& residency, Swim::Render::GeometryHeap& heap);

		using MeshHandle = Swim::Assets::AssetHandle<Swim::Assets::MeshAsset>;
		using TextureHandle = Swim::Assets::AssetHandle<Swim::Assets::TextureAsset>;

		MeshHandle Get(BuiltinMesh mesh) const { return builtins[static_cast<std::size_t>(mesh)]; }

		// Publishes (or replaces) "Procedural/<name>" and requests residency. A replacement is
		// a new asset; handles to the old one keep working through Resolve (materials, scene
		// meshes, viewports and GetBindlessIndex resolve automatically).
		MeshHandle Register(std::string_view name, const ProceduralMeshes::MeshData& mesh);

		MeshHandle Find(std::string_view name) const;

		// Drops a registered procedural mesh. Its GPU data is retired by the next
		// FlushReleases, after every submission that may still draw it.
		void Unregister(std::string_view name);

		// Retires unregistered meshes once `lastUse` completes (the renderer calls it at frame start).
		void FlushReleases(Swim::Rhi::TimelinePoint lastUse);

		// A cooked mesh already declared in the AssetSystem (for example by the development
		// asset bootstrap); requests residency. Invalid when unknown.
		MeshHandle Load(std::string_view logicalPath);

		// Textures: procedural RGBA8 images (mips generated unless `mips` is false, which keeps
		// thin cut-out details intact at a distance) and cooked ones.
		TextureHandle RegisterTexture(std::string_view name, std::uint32_t width, std::uint32_t height, std::span<const std::uint8_t> rgba8,
			bool srgb = true, bool mips = true);

		TextureHandle FindTexture(std::string_view name) const;

		// Drops a registered procedural texture (its bindless element is freed after the GPU
		// is done with it). Materials still pointing at it sample the fallback.
		void UnregisterTexture(std::string_view name);

		// The current asset for a handle whose name was registered again since (the handle
		// itself otherwise).
		MeshHandle Resolve(MeshHandle mesh) const;

		// Moves the asset ahead of the residency queue (AssetResidencyService::MakeUrgent):
		// for a few small meshes and textures something is waiting on, such as UI previews.
		void MakeUrgent(MeshHandle mesh);

		void MakeUrgent(TextureHandle texture);

		TextureHandle Resolve(TextureHandle texture) const;

		// Bumped whenever a mesh is replaced, so cached resolutions can be refreshed.
		std::uint64_t GetMeshReplacements() const { return meshReplacements; }

		struct Stats
		{
			std::uint32_t Meshes = 0; // Registered and loaded names.
			std::uint32_t Textures = 0;
			std::uint32_t ResidentMeshes = 0;
			std::uint32_t ResidentTextures = 0;
			std::uint32_t PendingReleases = 0; // Retired assets waiting for FlushReleases.
			std::uint64_t Replacements = 0;	   // Names registered again (meshes and textures).
		};

		Stats GetStats() const;

		// A two-colour checkerboard (cells x cells squares).
		TextureHandle RegisterChecker(
			std::string_view name, std::uint32_t size, std::uint32_t cells, std::array<std::uint8_t, 4> a, std::array<std::uint8_t, 4> b);

		bool IsResident(MeshHandle mesh) const;

		bool IsResident(TextureHandle texture) const;

		// The texture's bindless index (for UI images and custom draws); the fallback element until resident.
		std::uint32_t GetBindlessIndex(TextureHandle texture) const;

		// The (index page, vertex page) of every resident mesh this library requested, in
		// first-seen index-page order; one entry per index page.
		struct PageSlot
		{
			std::uint32_t IndexPage = 0;
			std::uint32_t VertexPage = 0;
		};

		std::vector<PageSlot> CollectPageSlots(bool* conflict = nullptr) const;

		// GPU meshes created outside residency (skinned outputs) that draws may use; their
		// pages join CollectPageSlots.
		void TrackGpuMesh(Swim::Render::GpuMeshHandle mesh);

		void UntrackGpuMesh(Swim::Render::GpuMeshHandle mesh);

		// The library path a handle was registered or loaded under ("Procedural/<name>" or the
		// cooked logical path); empty when unknown. Recorders use it as the asset key.
		std::string_view FindName(MeshHandle mesh) const;

		std::string_view FindTextureName(TextureHandle texture) const;

		// Sees every procedural mesh and texture registration with its source data, so a
		// scene recorder can store runtime content it cannot find again by name.
		struct SourceObserver
		{
			std::function<void(MeshHandle, std::string_view path, const ProceduralMeshes::MeshData&)> Mesh;
			std::function<void(TextureHandle, std::string_view path, std::uint32_t width, std::uint32_t height,
				std::span<const std::uint8_t> rgba8, bool srgb, bool mips)>
				Texture;
		};

		void SetSourceObserver(SourceObserver observer) { sourceObserver = std::move(observer); }

		std::uint32_t GetRequestedMeshCount() const { return static_cast<std::uint32_t>(meshes.size()); }

		std::uint32_t GetResidentMeshCount() const;

		std::uint32_t GetRequestedTextureCount() const { return static_cast<std::uint32_t>(textures.size()); }

		std::uint32_t GetResidentTextureCount() const;

	  private:

		Swim::Assets::AssetSystem& assets;
		Swim::Render::AssetResidencyService& residency;
		Swim::Render::GeometryHeap& heap;
		std::array<MeshHandle, static_cast<std::size_t>(BuiltinMesh::Count)> builtins{};
		std::unordered_map<std::string, MeshHandle> meshes;		 // By logical path.
		std::unordered_map<std::string, TextureHandle> textures; // By logical path.
		std::vector<Swim::Render::GpuMeshHandle> extraMeshes;
		std::unordered_map<std::uint64_t, std::string> names; // Asset id -> library path (meshes and textures).
		SourceObserver sourceObserver;
		std::vector<MeshHandle> released;
		std::vector<TextureHandle> releasedTextures;
		std::unordered_map<std::uint64_t, std::string> replaced; // Replaced asset id -> its library path.
		std::uint64_t revision = 0;
		std::uint64_t meshReplacements = 0;
		std::uint64_t replacements = 0;
	};

	// A full RGBA8 mip chain (box filtered; sRGB images are filtered in linear space).
	Swim::Assets::TextureAsset MakeTextureAsset(
		std::uint32_t width, std::uint32_t height, std::span<const std::uint8_t> rgba8, bool srgb, bool mips = true);

} // namespace Engine
