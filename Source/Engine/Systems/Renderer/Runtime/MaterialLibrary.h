#pragma once

#include "Engine/Assets/AssetHandle.h"
#include "Engine/Assets/TextureAsset.h"
#include "Engine/Systems/Renderer/Materials/StandardPbr.h"
#include "Engine/Systems/Renderer/Resources/GpuHandle.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Swim::Render
{
	class AssetResidencyService;
	class GpuMaterialTable;
	class MaterialInstance;
	class MaterialTemplate;
} // namespace Swim::Render

namespace Engine
{
	enum class MaterialBlend : std::uint8_t
	{
		Opaque,
		Masked,		 // Alpha-tested at AlphaCutoff.
		Transparent, // Alpha-blended, sorted back to front, casts no shadow.
	};

	// A standard metallic-roughness material as gameplay code describes it. Colors are
	// linear; Emissive is radiance (values above 1 bloom).
	struct MaterialDesc
	{
		std::string Name;
		std::array<float, 4> BaseColor{ 1, 1, 1, 1 };
		float Metallic = 0.0f;
		float Roughness = 0.5f;
		std::array<float, 3> Emissive{ 0, 0, 0 };
		MaterialBlend Blend = MaterialBlend::Opaque;
		float AlphaCutoff = 0.5f;
		bool DoubleSided = false;
		float OcclusionStrength = 1.0f;
		float NormalScale = 1.0f;
		Swim::Assets::AssetHandle<Swim::Assets::TextureAsset> BaseColorTexture;
		Swim::Assets::AssetHandle<Swim::Assets::TextureAsset> MetallicRoughnessTexture;
		Swim::Assets::AssetHandle<Swim::Assets::TextureAsset> NormalTexture;
		Swim::Assets::AssetHandle<Swim::Assets::TextureAsset> EmissiveTexture;
		Swim::Assets::AssetHandle<Swim::Assets::TextureAsset> OcclusionTexture;
	};

	// Material sets for GPU Scene objects (MeshRendererPart::MaterialSet): one
	// GpuMaterialTable row per material, textures resolved to bindless indices as they
	// become resident (the row is re-uploaded then), and every set routed to the right
	// visibility bins (Forward+ opaque/transparent, shadow opaque/masked/excluded)
	// through the routing callback. Owner thread only.
	class MaterialLibrary
	{
	  public:
		using Router = std::function<void(std::uint32_t materialSet, const Swim::Render::StandardPbr::Parameters& parameters)>;

		MaterialLibrary(Swim::Render::GpuMaterialTable& table, std::shared_ptr<const Swim::Render::MaterialTemplate> materialTemplate,
			Swim::Render::AssetResidencyService& residency, std::uint32_t samplerIndex);
		~MaterialLibrary();

		// The router is called for every existing and future set (and again on changes).
		void SetRouter(Router router);

		// Returns the material set index (the GPU Scene's MaterialSet).
		std::uint32_t Create(const MaterialDesc& desc);
		// The set named desc.Name when one exists (scenes re-run Init on reload), else Create.
		std::uint32_t GetOrCreate(const MaterialDesc& desc);
		bool Update(std::uint32_t materialSet, const MaterialDesc& desc);
		bool Release(std::uint32_t materialSet);
		const MaterialDesc* Find(std::uint32_t materialSet) const;
		// The set created with this name (0, the fallback row, when none).
		std::uint32_t FindByName(std::string_view name) const;

		std::uint32_t GetDefault() const { return defaultSet; }

		std::uint32_t GetCount() const { return static_cast<std::uint32_t>(entries.size()); }

		// Resolves textures that became resident since the last call.
		void Update();

		static Swim::Render::StandardPbr::Parameters ToParameters(const MaterialDesc& desc);

	  private:
		struct Entry
		{
			MaterialDesc Desc;
			std::shared_ptr<Swim::Render::MaterialInstance> Instance;
			Swim::Render::GpuMaterialHandle Handle;
			std::array<std::uint32_t, 5> TextureIndices{};
		};

		void Write(Entry& entry, bool force);

		Swim::Render::GpuMaterialTable& table;
		std::shared_ptr<const Swim::Render::MaterialTemplate> materialTemplate;
		Swim::Render::AssetResidencyService& residency;
		std::uint32_t sampler = 0;
		Router router;
		std::unordered_map<std::uint32_t, Entry> entries;
		std::uint32_t defaultSet = 0;
	};
} // namespace Engine
