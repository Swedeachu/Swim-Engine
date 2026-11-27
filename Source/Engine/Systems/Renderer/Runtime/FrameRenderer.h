#pragma once

#include "Engine/Assets/AssetHandle.h"
#include "Engine/Assets/TextureAsset.h"
#include "Engine/Systems/Renderer/Runtime/RenderFeature.h"
#include "Engine/Systems/Renderer/Runtime/RenderSettings.h"
#include "Engine/Systems/Renderer/Runtime/ShaderLibrary.h"
#include "Engine/Systems/Renderer/Shadows/ShadowPlanner.h"

#include "Engine/Systems/Renderer/RHI/RhiContracts.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace Swim::Assets
{

	class AssetSystem;

}

namespace Swim::IO
{

	class AsyncIoService;

}

namespace Swim::Jobs
{

	class JobSystem;

}

namespace Swim::Text
{

	class GlyphAtlas;

}

namespace Swim::UI
{

	class UiDocument;

}

namespace Swim::Render
{

	class AssetResidencyService;
	class BindlessResourceTable;
	class GeometryHeap;
	class GpuLightBuffer;
	class GpuMaterialTable;
	class GpuScene;
	class ParticleSystem;
	class SkinningSystem;
	class TextureResidency;

} // namespace Swim::Render

namespace Engine
{

	class MaterialLibrary;
	class SceneViewports;
	class MeshLibrary;
	class RenderDevice;

	// The view one frame is rendered from. Matrices are row-major (clip = M * v); the
	// projection is the canonical infinite reverse-Z perspective, unjittered (the frame
	// renderer adds the TAA jitter).
	struct RenderCamera
	{
		std::array<float, 16> View{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
		std::array<float, 16> Projection{};
		std::array<float, 3> Position{ 0, 0, 0 };
		std::array<float, 3> Forward{ 0, 0, -1 };
		std::array<float, 3> Right{ 1, 0, 0 };
		std::array<float, 3> Up{ 0, 1, 0 };
		float VerticalFov = 1.0f; // Radians.
		float Aspect = 16.0f / 9.0f;
		float Near = 0.1f;
		float Far = 500.0f; // Bounds clustering and shadows only.
		// Discontinuity (teleport, replay or scene change): drop temporal history (TAA,
		// reflections, motion vectors, auto exposure snaps to the new view) and tell features
		// through RenderFeatureView::Cut.
		bool Cut = false;
	};

	// One UI document to composite over the frame (after tone mapping).
	struct UiDrawItem
	{
		Swim::UI::UiDocument* Document = nullptr; // Laid out this frame.
		float DpiScale = 1.0f;
		float OffsetX = 0.0f; // Screen overlays: framebuffer pixels.
		float OffsetY = 0.0f;
		// World panels and billboards: canvas pixels -> clip (UI::ClipFromCanvas).
		std::optional<std::array<float, 16>> ClipFromCanvas;
		bool DepthTest = false; // World canvases occluded by the scene.
		float Opacity = 1.0f;
		// Draw order: lower layers first, items of one layer in list order. The screen blur
		// can run between layers (RenderSettings::Blur.AfterUiLayer), e.g. HUD at 0 blurred
		// with the scene and menus at 1 drawn sharp on top.
		std::int32_t Layer = 0;
	};

	struct RenderFrameInput
	{
		RenderCamera Camera;
		float DeltaTime = 0.0f;			  // Wall-clock seconds (exposure adaptation).
		float SimulationDeltaTime = 0.0f; // Scaled simulation seconds (particles; 0 while paused).
		// Lights that want a shadow this frame (their GpuLightBuffer rows already carry
		// ShadowIndex = Slot and LightFlags::CastsShadows).
		std::span<const Swim::Render::ShadowCasterDesc> ShadowCasters;
		// Local reflection probes (SceneRenderBridge gathers Engine::ReflectionProbe components).
		std::span<const Swim::Render::ReflectionProbeDesc> ReflectionProbes;
		// What moved this frame: the probe faces that see it are re-captured first.
		std::span<const Swim::Render::ReflectionProbeMover> ReflectionMovers;
		// Planar reflectors (SceneRenderBridge gathers Engine::PlanarReflector components).
		std::span<const Swim::Render::PlanarReflectorDesc> PlanarReflectors;
		std::span<const UiDrawItem> Ui;
		Swim::Text::GlyphAtlas* GlyphAtlas = nullptr; // Required when Ui is not empty (painting adds glyphs).
		bool Capture = false;						  // Read the finished frame back (GetCapture).
	};

	struct FrameRendererDesc
	{
		std::filesystem::path ShaderRoot;
		std::uint32_t MaxObjects = 16384;
		std::uint32_t MaxMaterials = 1024;
		std::uint32_t MaxLocalLights = 16384;
		std::uint32_t MaxDirectionalLights = 4;
		std::uint32_t MaxParticles = 1u << 18;
		std::uint32_t MaxEmitters = 128;
		std::uint32_t BindlessTextures = 1024;
		std::uint32_t BindlessSamplers = 16;
		std::uint64_t VertexPageSize = 64ull << 20;
		std::uint64_t IndexPageSize = 32ull << 20;
		// Draw visibility bins with DrawIndexedIndirect over zero-filled commands even when
		// the device has IndirectCount (automatic on SwiftShader).
		bool ForceIndirectFallback = false;
	};

	// The modern runtime renderer (Phase 23), replacing the retired VulkanRenderer and its
	// MeshPool/TexturePool/MaterialPool/FontPool. It owns every persistent GPU subsystem:
	//
	//   residency     AssetResidencyService over a GeometryHeap + TextureResidency (+ the
	//                 shared bindless table), MeshLibrary (procedural + cooked meshes and
	//                 textures) and MaterialLibrary (GpuMaterialTable rows)
	//   scene         GpuScene (fed by render extraction), GpuLightBuffer, ParticleSystem,
	//                 SkinningSystem
	//   frame         GPU visibility (main + shadow casters), clustered light assignment,
	//                 shadow atlas (ShadowPlanner + ShadowRenderer), environment (procedural
	//                 sky -> prefiltered cube + SH irradiance + BRDF LUT, rebuilt when the sky
	//                 changes), sky background, Clustered Forward+, GPU particles, screen-space
	//                 AO/reflections/fog, TAA, post (exposure, bloom, grading, tone mapping),
	//                 UI (screen and world canvases) and presentation
	//
	// Per frame: BeginFrame (collects retired resources and advances residency without
	// waiting for the GPU), then the scene bridge updates the GPU scene/lights/emitters,
	// then Render waits for the previous submission, records one render graph, submits and
	// presents it. Gameplay and extraction therefore overlap the previous frame's GPU work.
	// Owner thread only.
	class FrameRenderer
	{

	  public:

		// Throws std::runtime_error when a shader program is missing.
		FrameRenderer(RenderDevice& device, Swim::Assets::AssetSystem& assets, Swim::IO::AsyncIoService& io, Swim::Jobs::JobSystem* jobs,
			const FrameRendererDesc& desc);

		~FrameRenderer();

		FrameRenderer(const FrameRenderer&) = delete;

		FrameRenderer& operator=(const FrameRenderer&) = delete;

		RenderSettings& GetSettings() { return settings; }

		const RenderSettings& GetSettings() const { return settings; }

		const RenderStats& GetStats() const { return stats; }

		// The RenderSettings::MsaaSamples values the device supports, as a mask of the counts
		// (bit 1 = off, then 2, 4, 8, 16): a count the device lacks is lowered to the next one below.
		std::uint32_t GetSupportedMsaaSamples() const;

		RenderDevice& GetRenderDevice() const { return device; }

		MeshLibrary& GetMeshes() const;

		MaterialLibrary& GetMaterials() const;

		Swim::Render::GpuScene& GetScene() const;

		Swim::Render::GpuLightBuffer& GetLights() const;

		Swim::Render::ParticleSystem& GetParticles() const;

		Swim::Render::SkinningSystem& GetSkinning() const;

		Swim::Render::BindlessResourceTable& GetBindless() const;

		// The glint texture RenderSettings::Glint scrolls in Texture mode (nullptr: none; Texture
		// mode then falls back to Procedural). The view must stay alive until it is replaced
		// and the frames using it have completed (GetLastCompletion at that point).
		void SetGlintTexture(Swim::Rhi::TextureView* view);
		// The same from a texture asset (e.g. MeshLibrary::RegisterTexture): requested urgently
		// and used once resident (Procedural until then). The asset's owner keeps it alive; an
		// invalid handle clears it.
		void SetGlintTexture(Swim::Assets::AssetHandle<Swim::Assets::TextureAsset> texture);

		Swim::Render::AssetResidencyService& GetResidency() const;

		Swim::Render::GeometryHeap& GetGeometry() const;

		// Offscreen scene views shown as UI images (paper dolls, rendered icons).
		SceneViewports& GetViewports() const;

		// The last submitted frame (the lastUse of anything released now).
		Swim::Rhi::TimelinePoint GetLastCompletion() const { return lastCompletion; }

		void BeginFrame();

		// False when the frame was skipped (minimized, swapchain rebuilding, zero size).
		bool Render(const RenderFrameInput& input);

		// A sky background from a cube map instead of the procedural sky, drawn behind
		// everything (the main view, multisampled or not, reflection probes and planar
		// captures) while RenderSettings::SkyBackground is on, scaled by SkyCubemapIntensity.
		// Fog and the screen-space passes treat it like the procedural sky; image-based
		// lighting stays procedural. faces: +X, -X, +Y, -Y, +Z, -Z, each size x size tightly
		// packed sRGB RGBA8, top row first, as seen from inside the cube. The cube is
		// left-handed around the default view: +Z is straight ahead (world -Z), -Z behind,
		// +X to the right (world +X), +Y up. The pixels are copied and uploaded with the next
		// frame. Throws std::invalid_argument for a zero size or a face of the wrong size.
		void SetSkyCubemap(std::uint32_t size, const std::array<std::span<const std::byte>, 6>& faces);

		// Back to the procedural sky.
		void ClearSkyCubemap();

		bool HasSkyCubemap() const;

		// The last captured frame: tightly packed sRGB RGBA8, top row first.
		const std::vector<std::uint8_t>& GetCapture() const { return capture; }

		std::uint32_t GetCaptureWidth() const { return captureWidth; }

		std::uint32_t GetCaptureHeight() const { return captureHeight; }

		// Writes the last capture as a binary PPM (P6); false without one.
		bool WriteCapture(const std::filesystem::path& path) const;

		// Render features (RenderFeature.h): gameplay-owned passes recorded every frame at
		// their stage. Adding the same object twice is ignored; programs load on first use.
		void AddFeature(std::shared_ptr<RenderFeature> feature);

		bool RemoveFeature(const RenderFeature* feature);

		const std::vector<std::shared_ptr<RenderFeature>>& GetFeatures() const { return features; }

		// Replaces a runtime program's artifacts (a recompiled shader). Applied at the start of
		// the next Render: loaded feature compute and geometry programs and their pipelines are
		// rebuilt from the new artifacts, and the old ones destroyed once the GPU finished the
		// frames that used them. When that fails the old ones stay and done gets the error
		// (empty on success). A program not loaded yet uses the new artifacts when first
		// loaded; one an engine pass loaded keeps its program until restart (done says so).
		void ReplaceProgram(RuntimeShaderDesc desc, std::function<void(std::string_view error)> done = {});

		// Called before every feature compute dispatch of a program with [Tweak] push-constant
		// fields, with the program and its push constants to rewrite. Empty: none.
		void SetConstantsOverride(std::function<void(std::string_view, const RuntimeComputeProgram&, std::span<std::byte>)> callback);

		template <typename T> T* FindFeature() const
		{
			for (const auto& feature : features)
			{
				if (auto* typed = dynamic_cast<T*>(feature.get()))
				{
					return typed;
				}
			}

			return nullptr;
		}

	  private:

		void GatherTimings(); // Waits for the previous submission and reads its pass timings.

		struct Impl;
		RenderDevice& device;
		RenderSettings settings;
		RenderStats stats;
		std::unique_ptr<Impl> impl;
		std::vector<std::shared_ptr<RenderFeature>> features;
		Swim::Rhi::TimelinePoint lastCompletion{};
		std::vector<std::uint8_t> capture;
		std::uint32_t captureWidth = 0;
		std::uint32_t captureHeight = 0;

	};

} // namespace Engine
