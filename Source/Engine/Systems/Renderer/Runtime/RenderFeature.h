#pragma once

#include "Engine/Systems/Renderer/ForwardPlus/ExternalGeometry.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraph.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Engine
{

	struct RenderSettings;
	struct RuntimeComputeProgram;
	struct RuntimeGraphicsProgram;

	// Where a render feature runs in the frame. Every stage sees the scene color the
	// earlier passes produced and may replace it with a texture of its own.
	enum class RenderFeatureStage : std::uint32_t
	{
		// Linear HDR (RGBA16Float, viewport-sized) after Forward+, particles and the
		// screen-space effects, before temporal anti-aliasing: noisy, dithered effects
		// (volumetric clouds, ray-marched fog) are resolved by TAA afterwards.
		BeforeTemporal = 0,
		// Linear HDR after TAA, before exposure, bloom, grading and tone mapping: light
		// that should bloom and be exposed with the scene (sun shafts, lens flares).
		BeforePostProcess = 1,
		// The display-referred frame (RGBA8Unorm sRGB-encoded values, viewport-sized) after
		// tone mapping, before the UI: overlays, vignettes, screen fades.
		AfterPostProcess = 2,
	};

	inline constexpr std::uint32_t RenderFeatureStageCount = 3;

	// The view a frame is rendered from, as features need it. Matrices are row-major
	// (clip = M * v) and unjittered; the projection is infinite reverse-Z.
	struct RenderFeatureView
	{
		std::array<float, 16> View{};
		std::array<float, 16> Projection{};
		std::array<float, 16> ViewProjection{};
		std::array<float, 3> Position{};
		std::array<float, 3> Forward{ 0, 0, -1 };
		std::array<float, 3> Right{ 1, 0, 0 };
		std::array<float, 3> Up{ 0, 1, 0 };
		float TanHalfFovX = 1.0f; // The view ray of an NDC point is Forward + x * TanHalfFovX * Right + y * TanHalfFovY * Up.
		float TanHalfFovY = 1.0f;
		std::uint32_t Width = 0; // Viewport pixels.
		std::uint32_t Height = 0;
		// The sun: the procedural sky's direction (toward the sun, normalized) and radiance.
		std::array<float, 3> SunDirection{ 0, 1, 0 };
		std::array<float, 3> SunColor{ 1, 1, 1 };
		float Time = 0.0f;		 // Seconds since the renderer started (wall clock).
		float DeltaTime = 0.0f;	 // Wall-clock seconds of this frame.
		std::uint32_t Frame = 0; // Frame counter (noise and dither seeds).
		// Last frame's unjittered view-projection (this frame's on a cut), for motion and
		// reprojection.
		std::array<float, 16> PreviousViewProjection{};
		// This frame's TAA jitter in NDC (zero with TAA off): the engine's scene passes draw
		// with clip.xy += Jitter * clip.w.
		std::array<float, 2> Jitter{};
		// Temporal history is dropped this frame (a camera cut, the first frame): a feature
		// that keeps its own history resets it.
		bool Cut = false;
		// TAA resolves this frame. Without it a per-frame noise seed only crawls: dither with
		// a fixed seed instead (Temporal ? Frame : 0).
		bool Temporal = true;

		// A world-space direction's position on screen: UV (0..1, top-left origin) in xy,
		// and w > 0 when it is in front of the camera.
		std::array<float, 3> ProjectDirection(const std::array<float, 3>& direction) const;
	};

	// The engine geometry passes a feature can draw into (RenderFeature::DrawGeometry).
	using RenderGeometryPass = Swim::Render::ExternalGeometryPass;

	constexpr std::uint32_t RenderGeometryPassBit(RenderGeometryPass pass)
	{
		return Swim::Render::ExternalGeometryPassBit(pass);
	}

	inline constexpr std::uint32_t RenderGeometryAllPasses = RenderGeometryPassBit(RenderGeometryPass::DepthPrepass) |
		RenderGeometryPassBit(RenderGeometryPass::Opaque) | RenderGeometryPassBit(RenderGeometryPass::Transparent) |
		RenderGeometryPassBit(RenderGeometryPass::Shadow);

	// A shadow-atlas tile this frame (RenderFeatureContext::ShadowViews): Index is the row
	// of the shadow views buffer the Shadow pass draws it with (RenderFeatureDrawContext::
	// ShadowView). Rendered is false for a cached tile that keeps last frame's depth.
	struct RenderFeatureShadowView
	{
		std::uint32_t Index = 0;
		std::array<float, 16> ViewProjection{}; // Row-major, reverse-Z.
		bool Rendered = true;
	};

	// The main view's hierarchical depth (HZB), built from this frame's depth prepass (the
	// engine's opaque draws and every feature's prepass draws) for RenderFeature::CullGeometry.
	// Pyramid mip i holds, per texel, the farthest depth of the 2^(i+1) x 2^(i+1) depth texels
	// it covers (reverse-Z: the minimum). Test bounds with HzbOccluded (Features/HzbOcclusion.slang).
	// MipCount is 0 when no prepass ran this frame: nothing is occluded (Pyramid is a 1x1 stand-in).
	struct RenderFeatureOcclusion
	{
		Swim::Render::GraphTexture Pyramid; // Texture2D<float>, every mip, R32Float.
		std::uint32_t Width = 0;			// Depth size (pyramid level 0).
		std::uint32_t Height = 0;
		std::uint32_t MipCount = 0;
		std::array<float, 16> ViewProjection{}; // Row-major, unjittered, reverse-Z: the view the depth was drawn from.
	};

	// What PrepareGeometry returns: every graph resource the feature's draws read, declared
	// in each pass it draws into (buffers and textures as shader reads, indirect buffers as
	// draw arguments and counts).
	struct RenderGeometryResources
	{
		std::vector<Swim::Render::GraphBuffer> Buffers;
		std::vector<Swim::Render::GraphBuffer> IndirectBuffers;
		std::vector<Swim::Render::GraphTexture> Textures;
	};

	// Fixed-function state of a feature draw; the rest comes from the pass (targets, depth
	// test and writes, blending). Winding is counter-clockwise.
	struct RenderGeometryState
	{
		Swim::Rhi::CullMode Cull = Swim::Rhi::CullMode::None;
		// Opaque pass of a feature that also draws in the depth prepass: these draws were not
		// in the prepass (the late phase of occlusion culling), so they test and write depth
		// like a frame without a prepass instead of matching the prepass depth.
		bool SkipsPrepass = false;

		bool operator==(const RenderGeometryState&) const = default;
	};

	// A graphics pipeline of a feature program for one pass (cached by the renderer).
	struct RenderFeatureGeometryPipeline
	{
		Swim::Rhi::GraphicsPipeline* Pipeline = nullptr;
		const RuntimeGraphicsProgram* Program = nullptr;
	};

	class RenderFeatureContext;
	class RenderFeatureDrawContext;

	// A compute pass built from a program's reflected parameter names: resources are
	// bound by the names the Slang source gives them, their graph states are declared
	// automatically, and the dispatch covers a width x height grid with the program's
	// thread-group size. Created by RenderFeatureContext::Compute; Dispatch records it.
	//
	//   context.Compute("SunShafts")
	//       .Texture("Color", context.Color())    // Texture2D (sampled)
	//       .Texture("Depth", context.Depth())    // Texture2D<float> (depth aspect)
	//       .Sampler("LinearClamp")               // SamplerState
	//       .Storage("Output", output)            // RWTexture2D (written)
	//       .Constants(constants)                 // [[vk::push_constant]]
	//       .Dispatch(width, height);
	class RenderFeatureComputePass
	{

	  public:

		RenderFeatureComputePass& Texture(std::string_view name, Swim::Render::GraphTexture texture);

		RenderFeatureComputePass& Storage(std::string_view name, Swim::Render::GraphTexture texture);

		RenderFeatureComputePass& Buffer(std::string_view name, Swim::Render::GraphBuffer buffer);

		RenderFeatureComputePass& StorageBuffer(std::string_view name, Swim::Render::GraphBuffer buffer);

		// A RWStructuredBuffer the pass writes without reading first (declared as a write, so
		// a buffer created this frame may start here: counters reset, arguments generated).
		RenderFeatureComputePass& WriteBuffer(std::string_view name, Swim::Render::GraphBuffer buffer);

		// "LinearClamp" (the default for any name), "LinearRepeat" or "PointClamp".
		RenderFeatureComputePass& Sampler(std::string_view name, std::string_view kind = "LinearClamp");

		template <typename T> RenderFeatureComputePass& Constants(const T& value)
		{
			const auto bytes = std::as_bytes(std::span(&value, 1));
			constants.assign(bytes.begin(), bytes.end());
			return *this;
		}

		// Records the pass; throws std::invalid_argument when a reflected binding of the
		// program was not given, a name does not exist, or the kinds do not match.
		Swim::Render::GraphPass Dispatch(std::uint32_t width, std::uint32_t height, std::uint32_t depth = 1);

	  private:

		friend class RenderFeatureContext;
		enum class Kind
		{
			Texture,
			Storage,
			Buffer,
			StorageBuffer,
			Sampler,
			WriteBuffer,
		};

		struct Binding
		{
			std::string Name;
			Kind Type = Kind::Texture;
			Swim::Render::GraphTexture TextureHandle{};
			Swim::Render::GraphBuffer BufferHandle{};
			std::string SamplerKind;
		};

		RenderFeatureComputePass(RenderFeatureContext& context, std::string program, const RuntimeComputeProgram& compiled);

		RenderFeatureContext& context;
		std::string programName;
		const RuntimeComputeProgram& compiled;
		std::vector<Binding> bindings;
		std::vector<std::byte> constants;

	};

	// What a feature's Record sees: the graph, the view, the current scene color and
	// depth, texture helpers and compute passes over runtime shader programs.
	class RenderFeatureContext
	{

	  public:

		struct Services
		{
			std::function<const RuntimeComputeProgram&(std::string_view)> LoadCompute; // Cached per program.
			std::function<Swim::Rhi::Sampler&(std::string_view)> GetSampler;
			Swim::Rhi::Device* Device = nullptr;
			std::span<const RenderFeatureShadowView> ShadowViews;
			std::function<RenderFeatureGeometryPipeline(std::string_view, RenderGeometryPass, bool, const RenderGeometryState&)>
				GetGeometryPipeline;
			// Rewrites a compute pass's push constants before it is recorded (tweak variables).
			std::function<void(std::string_view, const RuntimeComputeProgram&, std::span<std::byte>)> OverrideConstants;
		};

		RenderFeatureContext(Swim::Render::RenderGraph& graph, RenderFeatureStage stage, const RenderFeatureView& view,
			const RenderSettings& settings, Swim::Render::GraphTexture color, Swim::Render::GraphTexture depth, Services services);

		Swim::Render::RenderGraph& Graph() const { return graph; }

		RenderFeatureStage Stage() const { return stage; }

		const RenderFeatureView& View() const { return view; }

		const RenderSettings& Settings() const { return settings; }

		// The scene color at this stage (see RenderFeatureStage for its format).
		Swim::Render::GraphTexture Color() const { return color; }

		// Replaces the scene color for later features and the rest of the frame; the
		// texture must have Color()'s format and size and be Sampled + Storage.
		void SetColor(Swim::Render::GraphTexture texture);

		// The frame's reverse-Z depth (D32Float; 0 = sky), jittered like the scene.
		Swim::Render::GraphTexture Depth() const { return depth; }

		// A transient texture (Sampled | Storage); size 0 = the viewport's.
		Swim::Render::GraphTexture CreateTexture(
			Swim::Rhi::Format format, std::uint32_t width = 0, std::uint32_t height = 0, std::string_view name = "Feature texture");

		// A texture like Color() (format and size) for writing a replacement.
		Swim::Render::GraphTexture CreateColorTarget(std::string_view name = "Feature color");

		// A compute pass over a runtime shader program (<Name>.spv in the runtime set, see
		// swim_add_runtime_shader in cmake/Shaders.cmake).
		RenderFeatureComputePass Compute(std::string_view program);

		// A transient buffer for this frame (Storage | Indirect | TransferDestination usage).
		Swim::Render::GraphBuffer CreateBuffer(std::uint64_t size, std::string_view name = "Feature buffer");

		// The device, for persistent resources a feature owns (import them every frame with
		// Graph().ImportBuffer / ImportTexture). Null in contexts without one.
		Swim::Rhi::Device* Device() const { return services.Device; }

		// PrepareGeometry only: this frame's shadow tiles (empty without shadows).
		std::span<const RenderFeatureShadowView> ShadowViews() const { return services.ShadowViews; }

		const Services& GetServices() const { return services; }

	  private:

		friend class RenderFeatureComputePass;
		Swim::Render::RenderGraph& graph;
		RenderFeatureStage stage;
		const RenderFeatureView& view;
		const RenderSettings& settings;
		Swim::Render::GraphTexture color;
		Swim::Render::GraphTexture depth;
		Services services;

	};

	// One draw of a feature program inside an engine pass, its resources bound by their
	// reflected names. The renderer binds the pass's frame table (space 0: ForwardFrame.slang
	// or ShadowFrame.slang) and the bindless table (space 1); every other reflected space
	// belongs to the feature and every parameter in it must be given here.
	//
	//   context.Program(context.Pass() == RenderGeometryPass::Shadow ? "BoxFieldShadow" : "BoxField")
	//       .Buffer("Boxes", boxes)                      // StructuredBuffer (declared in PrepareGeometry)
	//       .Texture("Albedo", albedoArray)              // Texture2DArray (a persistent view)
	//       .Sampler("AlbedoSampler", "LinearRepeat")
	//       .Constants(constants)                        // [[vk::push_constant]]
	//       .DrawIndirectCount(arguments, 0, count, 0, maxDraws);
	class RenderFeatureDraw
	{

	  public:

		RenderFeatureDraw& Buffer(std::string_view name, Swim::Render::GraphBuffer buffer);

		RenderFeatureDraw& Texture(std::string_view name, Swim::Render::GraphTexture texture);

		// A view the feature owns (texture arrays, persistent atlases); it must stay alive and
		// in ShaderRead until the frame completes.
		RenderFeatureDraw& Texture(std::string_view name, Swim::Rhi::TextureView& view);

		// "LinearClamp" (the default), "LinearRepeat" or "PointClamp".
		RenderFeatureDraw& Sampler(std::string_view name, std::string_view kind = "LinearClamp");

		RenderFeatureDraw& Sampler(std::string_view name, Swim::Rhi::Sampler& sampler);

		template <typename T> RenderFeatureDraw& Constants(const T& value)
		{
			const auto bytes = std::as_bytes(std::span(&value, 1));
			constants.assign(bytes.begin(), bytes.end());
			return *this;
		}

		void Draw(std::uint32_t vertexCount, std::uint32_t instanceCount = 1, std::uint32_t firstVertex = 0, std::uint32_t firstInstance = 0);

		// Swim::Rhi::DrawIndirectCommand records (vertex pulling: no vertex or index buffers).
		void DrawIndirect(Swim::Render::GraphBuffer arguments, std::uint64_t offset, std::uint32_t drawCount,
			std::uint32_t stride = sizeof(Swim::Rhi::DrawIndirectCommand));

		// The count (a uint at countOffset) is read on the GPU, clamped to maxDrawCount.
		void DrawIndirectCount(Swim::Render::GraphBuffer arguments, std::uint64_t offset, Swim::Render::GraphBuffer count,
			std::uint64_t countOffset, std::uint32_t maxDrawCount, std::uint32_t stride = sizeof(Swim::Rhi::DrawIndirectCommand));

	  private:

		friend class RenderFeatureDrawContext;
		enum class Kind
		{
			Buffer,
			Texture,
			View,
			Sampler,
			SamplerObject,
		};

		struct Binding
		{
			std::string Name;
			Kind Type = Kind::Buffer;
			Swim::Render::GraphBuffer BufferHandle{};
			Swim::Render::GraphTexture TextureHandle{};
			Swim::Rhi::TextureView* ViewObject = nullptr;
			Swim::Rhi::Sampler* SamplerObject = nullptr;
			std::string SamplerKind;
		};

		RenderFeatureDraw(RenderFeatureDrawContext& context, std::string program, RenderFeatureGeometryPipeline pipeline);

		void Bind();

		RenderFeatureDrawContext& context;
		std::string programName;
		RenderFeatureGeometryPipeline pipeline;
		std::vector<Binding> bindings;
		std::vector<std::byte> constants;

	};

	// What DrawGeometry sees inside an engine pass: rendering has begun with the pass's
	// targets, the viewport is set (the shadow tile in the Shadow pass) and the engine's
	// own draws are recorded. Draws are added through Program(...).
	class RenderFeatureDrawContext
	{

	  public:

		RenderFeatureDrawContext(Swim::Render::ExternalDrawContext& draw, const RenderFeatureView& view,
			const RenderFeatureContext::Services& services);

		RenderGeometryPass Pass() const { return draw.Pass; }

		// Opaque only: the depth prepass already holds this frame's depth for the feature's
		// geometry (it drew into DepthPrepass too). The pipeline then tests equal-or-nearer
		// without writing depth, so the vertex stage must produce the prepass's exact depths.
		bool DepthPrepassed() const { return draw.DepthPrepassed; }

		// Shadow only: the shadow views buffer row of this tile (ShadowFrame.slang Views[...])
		// and its view-projection (row-major, reverse-Z).
		std::uint32_t ShadowView() const { return draw.ShadowView; }

		const std::array<float, 16>& ShadowViewProjection() const { return draw.ShadowViewProjection; }

		const RenderFeatureView& View() const { return view; }

		// A draw with a program compiled for this pass: forward passes need a program that
		// includes ForwardFrame.slang (the depth prepass and Shadow ones may write no
		// colour), Shadow a program that includes ShadowFrame.slang.
		RenderFeatureDraw Program(std::string_view name, const RenderGeometryState& state = {});

		// The pass's command context, for anything the binder does not cover.
		Swim::Render::RenderCommandContext& Commands() const { return draw.Context; }

	  private:

		friend class RenderFeatureDraw;
		Swim::Rhi::DescriptorTable& FrameTable(Swim::Rhi::PipelineLayout& layout);

		Swim::Render::ExternalDrawContext& draw;
		const RenderFeatureView& view;
		const RenderFeatureContext::Services& services;
		std::vector<std::pair<Swim::Rhi::PipelineLayout*, Swim::Rhi::DescriptorTable*>> frameTables;

	};

	// A self-contained piece of the frame added from gameplay code:
	//
	//   auto clouds = std::make_shared<Engine::VolumetricClouds>();
	//   clouds->Settings.Coverage = 0.45f;
	//   renderer.AddFeature(clouds);
	//
	// Record runs once per rendered frame at GetStage(), in ascending GetOrder() within a
	// stage (ties keep the order they were added), and only while Enabled. Settings live on
	// the feature object and may be changed at any time from the owner thread.
	class RenderFeature
	{

	  public:

		virtual ~RenderFeature() = default;

		virtual std::string_view GetName() const = 0;

		virtual RenderFeatureStage GetStage() const { return RenderFeatureStage::BeforeTemporal; }

		virtual int GetOrder() const { return 0; }

		// A camera look effect (depth of field, lens, film): skipped while
		// RenderSettings::CinematicEffects is off (competitive presets).
		virtual bool IsCinematic() const { return false; }

		virtual void Record(RenderFeatureContext& context) { (void)context; }

		// Geometry: a feature can also draw into the engine's own geometry passes (depth
		// prepass, opaque, transparent, shadow tiles), lit by Forward+ and casting shadows
		// like the scene. GetGeometryPasses() is a RenderGeometryPassBit mask; when it is not
		// zero, PrepareGeometry runs once per frame before the shadow and visibility passes
		// (culling, uploads: compute passes that write the indirect arguments) with the
		// camera in View() and this frame's ShadowViews(), and returns the resources the
		// draws read. DrawGeometry then runs at execution, inside every pass in the mask (once
		// per rendered shadow tile). Opaque draws turn the deferred local lights off for the
		// frame. Main view only: reflection captures do not see feature geometry.
		virtual std::uint32_t GetGeometryPasses() const { return 0; }

		virtual RenderGeometryResources PrepareGeometry(RenderFeatureContext& context)
		{
			(void)context;
			return {};
		}

		// Two-phase occlusion culling: return true to have CullGeometry called after the depth
		// prepass with the HZB of this frame's prepass depth. Typical use: PrepareGeometry
		// culls into prepass draws only what was visible last frame (everything on
		// View().Cut), CullGeometry tests the rest against the HZB into a second argument
		// buffer and records what is visible for the next frame, and the opaque pass draws
		// that second set with RenderGeometryState::SkipsPrepass. Buffers CullGeometry writes
		// must already be in PrepareGeometry's resources (and written there first when they
		// are transient: reset the counts there).
		virtual bool UsesOcclusion() const { return false; }

		virtual void CullGeometry(RenderFeatureContext& context, const RenderFeatureOcclusion& occlusion)
		{
			(void)context;
			(void)occlusion;
		}

		virtual void DrawGeometry(RenderFeatureDrawContext& context) { (void)context; }

		// Environment contribution: a feature that draws into the sky (volumetric clouds)
		// can also draw into the image-based lighting cube, so reflections and ambient
		// light see it. When ContributesToEnvironment() is true the renderer re-records the
		// environment every RenderSettings::EnvironmentRefreshSeconds with the view's camera
		// as the probe, and calls RecordEnvironment for an overlay of
		// EnvironmentBuilder::OverlayDesc(faceSize) (face f of the cube in rows
		// f * faceSize.., premultiplied rgb in the cube's radiance units - lighting
		// multiplies it by EnvironmentIntensity - and alpha = transmittance). The context's
		// Color() and Depth() are not valid there. Returning nothing skips the overlay.
		virtual bool ContributesToEnvironment() const { return false; }

		virtual std::optional<Swim::Render::GraphTexture> RecordEnvironment(RenderFeatureContext& context, std::uint32_t faceSize)
		{
			(void)context;
			(void)faceSize;
			return std::nullopt;
		}

		bool Enabled = true;

	};

} // namespace Engine
