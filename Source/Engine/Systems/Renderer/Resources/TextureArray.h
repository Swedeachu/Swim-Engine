#pragma once
#include "Engine/Systems/Renderer/RenderGraph/RenderGraph.h"

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace Swim::Render
{

	enum class TextureLayerState : std::uint8_t
	{
		Placeholder, // Never set, cleared, or its load was cancelled before anything landed.
		Loading,	 // A file is being read and decoded on the loader thread.
		Uploading,	 // Decoded; mips are going up smallest first (GetMinLod falls as they land).
		Resident,	 // Every mip of the current image is on the GPU.
		Failed,		 // The last load failed (GetLayerError); the previous image stays.
	};

	struct TextureArrayDesc
	{
		std::uint32_t Width = 256;
		std::uint32_t Height = 256;
		std::uint32_t Layers = 1;
		std::uint32_t MipLevels = 0; // 0: the full chain.
		// RGBA8Unorm, RGBA8UnormSrgb, BC7Unorm or BC7UnormSrgb.
		Rhi::Format Format = Rhi::Format::RGBA8UnormSrgb;
		std::array<std::uint8_t, 4> Placeholder{ 255, 0, 255, 255 }; // What empty layers show.
		// Above 0: the alpha-test cutoff of the layers. Generated RGBA8 mips (and PNG / JPEG
		// cooked on load) scale their alpha to keep mip 0's coverage at it, so cutouts stay
		// filled in with distance instead of thinning away.
		float AlphaCoverageCutoff = 0.0f;
		// SetLayer RGBA8 data that needs work (missing mips or a different size) is checked at
		// once but resized and given its mips on the loader thread: the layer is Loading until
		// then, and the previous image or the placeholder stays.
		bool GenerateMipsInBackground = false;
		std::string DebugName = "Texture array";
	};

	// How the mips generated for a layer (those its data leaves out) filter alpha. Colour is
	// averaged by alpha either way, so clear texels never bleed into what stays visible, and
	// each layer is filtered on its own, clamped at its edges (nothing wraps or crosses layers).
	enum class AlphaMipFilter : std::uint8_t
	{
		// The array's choice: coverage at TextureArrayDesc::AlphaCoverageCutoff when that is
		// above 0, else the average.
		Array,
		// The box-filtered average: translucent layers (water, stained glass, ice).
		Average,
		// The average, scaled per mip so the share of texels passing the cutoff stays mip 0's
		// (Castano, "Computing Alpha Mipmaps"): scattered cut-outs (leaves, grass, flowers)
		// stay filled in with distance instead of thinning away.
		Coverage,
		// The largest alpha of each 2 x 2: thin opaque strokes (frames, bars, rails, wire) stay
		// one solid texel wide at every mip, so at a distance they draw as unbroken one-pixel
		// lines instead of breaking into dots (coverage alone cannot keep a line connected).
		Maximum,
	};

	// One layer's pixels: Mips[0] is Width x Height, each next mip half the size (at least
	// 1), tightly packed rows of RGBA8 texels or rows of 4 x 4 BC7 blocks. RGBA8 data may
	// stop after any mip (the rest are generated) and is resized when its size differs from
	// the array's; BC7 data must match the array's size and carry every mip.
	struct TextureLayerData
	{
		Rhi::Format Format = Rhi::Format::RGBA8Unorm;
		std::uint32_t Width = 0;
		std::uint32_t Height = 0;
		std::vector<std::vector<std::byte>> Mips;
		// RGBA8: how generated mips filter alpha.
		AlphaMipFilter AlphaMips = AlphaMipFilter::Array;
		// Coverage: the alpha-test cutoff (0: the array's AlphaCoverageCutoff), and the share of
		// mip 0's coverage the generated mips keep (below 1 thins far cut-outs).
		float AlphaCutoff = 0.0f;
		float CoverageScale = 1.0f;
	};

	// Turns a file's bytes into layer data; empty (with error set) when it cannot.
	using TextureLayerDecoder = std::function<std::optional<TextureLayerData>(
		std::span<const std::byte> bytes, const std::filesystem::path& path, const TextureArrayDesc& desc, std::string& error)>;

	struct TextureArrayStats
	{
		std::uint32_t Layers = 0;
		std::uint32_t Resident = 0;
		std::uint32_t Loading = 0;
		std::uint32_t Uploading = 0;
		std::uint32_t Failed = 0;
		std::uint64_t MemoryBytes = 0;		  // The whole array, every mip.
		std::uint64_t PendingUploadBytes = 0; // Decoded, not yet sent.
		std::uint64_t UploadedBytes = 0;	  // Last Import.
		std::uint32_t Reloads = 0;			  // Hot reloads started since creation.
	};

	// A Texture2DArray built at run time: a fixed number of same-size layers, each replaced
	// whole from memory (SetLayer) or from a file loaded and decoded on a background thread
	// (LoadLayer: priorities, cancellation, the previous image or the placeholder stays
	// until the new one lands). Import, once per frame on the render thread, sends decoded
	// mips smallest first within an upload budget; GetMinLod / ImportMinLod give the finest
	// mip of each layer's current image, for sampling that never reads a mip that has not
	// arrived (TextureArraySample in Resources/TextureArray.slang). With hot reload on, files
	// whose modification time changes are loaded again; the texture, its view and every
	// pipeline stay the same. The whole array is allocated up front (GetStats().MemoryBytes).
	class TextureArray
	{

	  public:

		TextureArray(Rhi::Device& device, TextureArrayDesc desc);

		~TextureArray();

		TextureArray(const TextureArray&) = delete;

		TextureArray& operator=(const TextureArray&) = delete;

		const TextureArrayDesc& GetDesc() const { return desc; }

		std::uint32_t GetMipLevels() const { return mipLevels; }

		// Replaces a layer from memory (validated now; throws std::invalid_argument). See
		// TextureArrayDesc::GenerateMipsInBackground.
		void SetLayer(std::uint32_t layer, TextureLayerData data);

		// Validates and finishes data once (resample, mips) for SetLayer(layer, prepared).
		// Throws std::invalid_argument.
		std::shared_ptr<const TextureLayerData> PrepareLayer(TextureLayerData data) const;

		// Replaces a layer with data from PrepareLayer: no copy, allocation or mip work, so
		// frames prepared once can be swapped in every frame (flipbooks).
		void SetLayer(std::uint32_t layer, std::shared_ptr<const TextureLayerData> prepared);

		// Loads a layer from a file in the background; a newer request for the same layer
		// replaces an older one. Higher priorities decode first.
		void LoadLayer(std::uint32_t layer, std::filesystem::path path, int priority = 0);

		// Drops the layer's pending load, and its upload unless one has started (a started
		// upload finishes so the layer stays whole).
		void CancelLayer(std::uint32_t layer);

		// Back to the placeholder (and forgets the layer's file).
		void ClearLayer(std::uint32_t layer);

		TextureLayerState GetLayerState(std::uint32_t layer) const;

		const std::string& GetLayerError(std::uint32_t layer) const;

		// The finest mip of the layer's current image that is on the GPU.
		float GetMinLod(std::uint32_t layer) const;

		void SetHotReload(bool enabled, double pollSeconds = 0.5);

		// Replaces the file decoder (DecodeFile by default).
		void SetDecoder(TextureLayerDecoder decoder);

		// The built-in decoder: KTX2 without supercompression in the array's format family
		// (BC7 or RGBA8, one layer, every mip), plus PNG and JPEG in development builds
		// (compiled to the array's format by the asset compiler).
		static std::optional<TextureLayerData> DecodeFile(
			std::span<const std::byte> bytes, const std::filesystem::path& path, const TextureArrayDesc& desc, std::string& error);

		// Imports the texture (ShaderRead outside this frame's copies) and records this
		// frame's uploads. Call once per frame, before the passes that sample it; return the
		// handle in RenderGeometryResources::Textures (or read it in your passes).
		GraphTexture Import(RenderGraph& graph, std::uint64_t uploadBudget);

		// A buffer of one float per layer, GetMinLod for this frame (StructuredBuffer<float>).
		GraphBuffer ImportMinLod(RenderGraph& graph) const;

		Rhi::Texture& GetTexture() const { return *texture; }

		// A Texture2DArray view of every layer and mip.
		Rhi::TextureView& GetView() const { return *view; }

		// The same view as a weak reference that expires with the array.
		std::weak_ptr<Rhi::TextureView> GetSharedView() const { return view; }

		const TextureArrayStats& GetStats() const { return stats; }

	  private:

		struct Layer
		{
			std::filesystem::path Source;
			std::filesystem::file_time_type Stamp{};
			std::uint64_t Request = 0; // The live request; results of older ones are dropped.
			bool InFlight = false;
			bool HasImage = false;
			std::string Error;
			std::shared_ptr<const TextureLayerData> Pending;
			bool PendingPlaceholder = false;
			std::uint32_t NextMip = 0; // Pending's mips below this are still to send.
			// Replaces a started or resident image: every mip goes up in one frame, so a layer
			// set again before its upload finished (flipbooks) cannot restart forever short of mip 0.
			bool Whole = false;
			float MinLod = 0.0f;
		};

		struct LoadRequest
		{
			std::uint32_t Layer = 0;
			std::uint64_t Id = 0;
			int Priority = 0;
			std::uint64_t Order = 0;
			std::filesystem::path Path;
			std::shared_ptr<TextureLayerData> Data; // SetLayer data to finish instead of a file.
		};

		struct LoadResult
		{
			std::uint32_t Layer = 0;
			std::uint64_t Id = 0;
			std::optional<TextureLayerData> Data;
			std::filesystem::file_time_type Stamp{};
			std::string Error;
		};

		void Check(std::uint32_t layer) const;

		// Throws std::invalid_argument for data Normalize cannot use.
		void Validate(const TextureLayerData& data) const;

		// Validates, then resizes RGBA8 data and generates its missing mips.
		void Normalize(TextureLayerData& data) const;

		bool NeedsWork(const TextureLayerData& data) const;

		TextureLayerData MakeSolid() const;

		void Queue(std::uint32_t layer, std::shared_ptr<const TextureLayerData> data, bool placeholder);

		void Request(std::uint32_t layer, std::filesystem::path path, int priority);

		void DropRequests(std::uint32_t layer);

		void Work();

		void FillPlaceholder(Rhi::Device& device);

		void PollReloads();

		TextureArrayDesc desc;
		std::uint32_t mipLevels = 1;
		std::unique_ptr<Rhi::Texture> texture;
		std::shared_ptr<Rhi::TextureView> view;
		bool initialized = false;
		std::vector<Layer> layers;
		std::deque<std::uint32_t> uploads;
		TextureArrayStats stats;

		bool hotReload = false;
		double pollSeconds = 0.5;
		std::chrono::steady_clock::time_point nextPoll{};
		std::uint32_t pollCursor = 0;

		mutable std::mutex mutex;
		std::condition_variable wake;
		std::vector<LoadRequest> requests; // A max-heap by priority, then age.
		std::vector<LoadResult> results;
		TextureLayerDecoder decoder;
		std::uint64_t nextId = 1;
		std::uint64_t nextOrder = 0;
		bool stopping = false;
		std::thread worker;

	};

} // namespace Swim::Render
