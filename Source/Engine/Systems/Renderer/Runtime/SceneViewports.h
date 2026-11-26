#pragma once

#include "Engine/Assets/AssetHandle.h"
#include "Engine/Assets/MeshAsset.h"
#include "Engine/Assets/TextureAsset.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraph.h"
#include "Engine/Systems/Renderer/Resources/BindlessResourceTable.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Swim::Render
{

	struct GeometryGraphResources;
	class GeometryHeap;
	class AssetResidencyService;

} // namespace Swim::Render

namespace Engine
{

	struct SceneViewportHandle
	{
		std::uint32_t Index = 0;
		std::uint32_t Generation = 0; // 0 is never valid.

		explicit operator bool() const { return Generation != 0; }

		bool operator==(const SceneViewportHandle&) const = default;
	};

	// One mesh of a viewport scene. Every submesh is drawn with the item's texture and tint
	// (texels with alpha below AlphaCutoff are discarded; the rest are opaque).
	struct SceneViewportItem
	{
		Swim::Assets::AssetHandle<Swim::Assets::MeshAsset> Mesh;
		Swim::Assets::AssetHandle<Swim::Assets::TextureAsset> Texture; // Invalid: white.
		std::array<float, 16> Transform{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 }; // Row-major world-from-local.
		std::array<float, 4> Tint{ 1, 1, 1, 1 };											// Linear.
		float AlphaCutoff = 0.5f;
		bool PointSampled = true;
		// Lit by the scene's light; false draws the texture colour as is (flat icons).
		bool Lit = true;
	};

	// What one draw renders: a camera, one directional light and a few meshes, into a
	// pixel region of the viewport's texture (all zero: the whole texture). The region is
	// cleared to ClearColor first; the rest of the texture keeps its contents, so icons can
	// be rendered once each into cells of one atlas texture and sampled for as long as needed.
	struct SceneViewportScene
	{
		std::array<float, 16> View{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 }; // Row-major.
		// Row-major, reverse-Z (near maps to 1, far to 0); perspective or orthographic.
		std::array<float, 16> Projection{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, -0.01f, 0.5f, 0, 0, 0, 1 };
		std::array<float, 4> ClearColor{ 0, 0, 0, 0 }; // Premultiplied linear.
		std::array<float, 3> LightDirection{ -0.4f, 0.8f, 0.45f }; // Towards the light (normalized here).
		std::array<float, 3> LightColor{ 0.8f, 0.8f, 0.8f };
		std::array<float, 3> Ambient{ 0.4f, 0.4f, 0.4f };
		std::array<std::uint32_t, 4> Region{ 0, 0, 0, 0 }; // X, Y, width, height in pixels.
		std::vector<SceneViewportItem> Items;
	};

	// A viewport's texture as a UI image (UiImage::Texture / Sampler).
	struct SceneViewportImage
	{
		std::uint32_t Texture = 0;
		std::uint32_t Sampler = 0; // Point (PointFilter) or linear, clamp-to-edge.
		std::uint32_t Width = 0;
		std::uint32_t Height = 0;
	};

	struct SceneViewportDesc
	{
		std::uint32_t Width = 256;
		std::uint32_t Height = 256;
		bool PointFilter = true;
		std::string DebugName = "Scene viewport";
	};

	struct SceneViewportStats
	{
		std::uint32_t Viewports = 0;
		std::uint32_t Queued = 0;	   // Draws waiting for the next frame (or for residency).
		std::uint64_t Drawn = 0;	   // Draws committed over the object's lifetime.
		std::uint32_t LastItems = 0; // Meshes drawn in the last frame.
	};

	// Offscreen scene views for UI: persistent RGBA8 sRGB textures in the shared bindless
	// table, each drawn with its own camera and light from meshes of the GeometryHeap.
	// Draw queues a scene for the next frame (call it every frame for a live view, once for
	// a still such as an icon); a scene whose meshes or textures are not resident yet stays
	// queued until they are, so stills never bake placeholders. The frame renderer records
	// queued draws before the UI. Owner thread only.
	class SceneViewports
	{

	  public:

		struct Program
		{
			Swim::Rhi::GraphicsPipeline* Pipeline = nullptr;
			Swim::Rhi::PipelineLayout* Layout = nullptr;
		};

		SceneViewports(Swim::Rhi::Device& device, Swim::Render::BindlessResourceTable& bindless);

		~SceneViewports();

		SceneViewports(const SceneViewports&) = delete;

		SceneViewports& operator=(const SceneViewports&) = delete;

		// Throws std::invalid_argument for an empty or oversized extent, std::length_error
		// when the bindless table is full and std::runtime_error when the texture fails.
		SceneViewportHandle Create(const SceneViewportDesc& desc);

		// Retires the texture after lastUse; queued draws are dropped.
		bool Destroy(SceneViewportHandle viewport, Swim::Rhi::TimelinePoint lastUse = {});

		bool IsValid(SceneViewportHandle viewport) const;

		// Throws std::invalid_argument for an invalid handle or a region outside the texture.
		void Draw(SceneViewportHandle viewport, SceneViewportScene scene);

		// The texture shows transparent black until the first draw completes.
		SceneViewportImage GetImage(SceneViewportHandle viewport) const;

		// True once every draw queued for the viewport has been rendered.
		bool IsIdle(SceneViewportHandle viewport) const;

		SceneViewportStats GetStats() const;

		// Frame renderer side. Record imports every viewport (ShaderRead after the frame)
		// and draws the ready scenes; it returns the textures for the UI pass to read.
		std::vector<Swim::Render::GraphTexture> Record(Swim::Render::RenderGraph& graph, const Swim::Render::GeometryHeap& geometry,
			const Swim::Render::GeometryGraphResources& pages, const Swim::Render::AssetResidencyService& residency, const Program& program,
			Swim::Rhi::DescriptorTable& bindlessTable, std::uint32_t linearSampler, std::uint32_t pointSampler);

		void CommitFrame();

		void AbortFrame();

		void Collect();

		// Maps item assets to the ones to draw (MeshLibrary::Resolve: names registered again).
		struct AssetResolver
		{
			std::function<Swim::Assets::AssetHandle<Swim::Assets::MeshAsset>(Swim::Assets::AssetHandle<Swim::Assets::MeshAsset>)> Mesh;
			std::function<Swim::Assets::AssetHandle<Swim::Assets::TextureAsset>(Swim::Assets::AssetHandle<Swim::Assets::TextureAsset>)>
				Texture;
		};

		void SetAssetResolver(AssetResolver value) { resolver = std::move(value); }

		void Drain();

		static Swim::Rhi::GraphicsPipelineDesc PipelineDesc(Swim::Rhi::ShaderProgram& program, Swim::Rhi::PipelineLayout& layout);

	  private:

		AssetResolver resolver;
		struct Viewport;
		struct Retired;

		Viewport* Find(SceneViewportHandle handle);

		const Viewport* Find(SceneViewportHandle handle) const;

		Swim::Rhi::Device& device;
		Swim::Render::BindlessResourceTable& bindless;
		std::unique_ptr<Swim::Rhi::Sampler> pointSampler, linearSampler;
		Swim::Render::BindlessSamplerHandle pointHandle, linearHandle;
		std::vector<std::unique_ptr<Viewport>> viewports;
		std::vector<std::unique_ptr<Retired>> retired;
		std::uint64_t drawn = 0;
		std::uint32_t lastItems = 0;

	};

} // namespace Engine
