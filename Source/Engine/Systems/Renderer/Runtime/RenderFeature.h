#pragma once

#include "Engine/Systems/Renderer/RenderGraph/RenderGraph.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Engine
{
	struct RenderSettings;
	struct RuntimeComputeProgram;

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

		// A world-space direction's position on screen: UV (0..1, top-left origin) in xy,
		// and w > 0 when it is in front of the camera.
		std::array<float, 3> ProjectDirection(const std::array<float, 3>& direction) const;
	};

	class RenderFeatureContext;

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
		virtual RenderFeatureStage GetStage() const = 0;

		virtual int GetOrder() const { return 0; }

		virtual void Record(RenderFeatureContext& context) = 0;

		bool Enabled = true;
	};
} // namespace Engine
