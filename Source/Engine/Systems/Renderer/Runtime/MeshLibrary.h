#pragma once

#include "Engine/Assets/AssetHandle.h"
#include "Engine/Assets/MeshAsset.h"
#include "Engine/Assets/TextureAsset.h"
#include "Engine/Systems/Renderer/Resources/GpuHandle.h"
#include "Engine/Systems/Renderer/Runtime/ProceduralMeshes.h"

#include <array>
#include <cstdint>
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

		// Publishes (or replaces) "Procedural/<name>" and requests residency.
		MeshHandle Register(std::string_view name, const ProceduralMeshes::MeshData& mesh);
		MeshHandle Find(std::string_view name) const;
		// A cooked mesh already declared in the AssetSystem (for example by the development
		// asset bootstrap); requests residency. Invalid when unknown.
		MeshHandle Load(std::string_view logicalPath);

		// Textures: procedural RGBA8 images (mips generated) and cooked ones.
		TextureHandle RegisterTexture(
			std::string_view name, std::uint32_t width, std::uint32_t height, std::span<const std::uint8_t> rgba8, bool srgb = true);
		TextureHandle FindTexture(std::string_view name) const;
		// A two-colour checkerboard (cells x cells squares).
		TextureHandle RegisterChecker(
			std::string_view name, std::uint32_t size, std::uint32_t cells, std::array<std::uint8_t, 4> a, std::array<std::uint8_t, 4> b);

		bool IsResident(MeshHandle mesh) const;

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
	};

	// A full RGBA8 mip chain (box filtered; sRGB images are filtered in linear space).
	Swim::Assets::TextureAsset MakeTextureAsset(std::uint32_t width, std::uint32_t height, std::span<const std::uint8_t> rgba8, bool srgb);
} // namespace Engine
