#include "Engine/Systems/Renderer/Resources/TextureArray.h"

#include "Engine/Math/AlphaCoverage.h"
#include "Engine/Math/ColorMath.h"
#include "Engine/Assets/Ktx2Container.h"
#include "Engine/Systems/Renderer/RHI/RhiFormatInfo.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderCommandContext.h"

#if SWIM_ENABLE_DEV_ASSET_AUTOCOOK
#include "Tools/AssetCompiler/SourceImageTextureCompiler.h"
#endif

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace Swim::Render
{

	namespace
	{

		using S = Rhi::ResourceState;
		using F = Rhi::Format;

		bool IsBc7(F format)
		{
			return format == F::BC7Unorm || format == F::BC7UnormSrgb;
		}

		bool IsRgba8(F format)
		{
			return format == F::RGBA8Unorm || format == F::RGBA8UnormSrgb;
		}

		bool IsSrgb(F format)
		{
			return format == F::RGBA8UnormSrgb || format == F::BC7UnormSrgb;
		}

		std::uint32_t MipExtent(std::uint32_t size, std::uint32_t mip)
		{
			return std::max(size >> mip, 1u);
		}

		std::uint64_t AlignUp(std::uint64_t value, std::uint64_t alignment)
		{
			return (value + alignment - 1) / alignment * alignment;
		}

		// A BC7 mode 6 block whose two endpoints are the colour and whose indices are all
		// zero, so every texel decodes to exactly the colour.
		std::array<std::byte, 16> SolidBc7Block(const std::array<std::uint8_t, 4>& colour)
		{
			std::array<std::uint8_t, 16> bits{};
			std::uint32_t at = 0;
			const auto put = [&](std::uint32_t value, std::uint32_t count)
			{
				for (std::uint32_t bit = 0; bit < count; ++bit, ++at)
				{
					if ((value >> bit) & 1u)
					{
						bits[at / 8] |= static_cast<std::uint8_t>(1u << (at % 8));
					}
				}
			};

			put(1u << 6, 7);

			for (const auto channel : colour)
			{
				put(channel >> 1, 7);
				put(channel >> 1, 7);
			}

			// One p-bit per endpoint; the alpha's low bit decides (the colour channels'
			// low bits then follow it, an error of at most one step).
			put(colour[3] & 1u, 1);
			put(colour[3] & 1u, 1);

			std::array<std::byte, 16> block{};
			std::memcpy(block.data(), bits.data(), block.size());
			return block;
		}

		const std::array<float, 256>& SrgbToLinear()
		{
			static const auto table = []
			{
				std::array<float, 256> values{};

				for (int i = 0; i < 256; ++i)
				{
					const float c = static_cast<float>(i) / 255.0f;
					values[i] = ::Engine::SrgbToLinear(c);
				}

				return values;
			}();
			return table;
		}

		std::uint8_t LinearToSrgb(float value)
		{
			return ::Engine::LinearToSrgb8(value);
		}

		std::vector<std::byte> Downsample(
			const std::vector<std::byte>& source, std::uint32_t width, std::uint32_t height, bool srgb, bool maximumAlpha = false)
		{
			const auto outWidth = std::max(width / 2, 1u);
			const auto outHeight = std::max(height / 2, 1u);
			std::vector<std::byte> out(std::size_t(outWidth) * outHeight * 4);
			const auto* in = reinterpret_cast<const std::uint8_t*>(source.data());
			auto* to = reinterpret_cast<std::uint8_t*>(out.data());
			const auto& toLinear = SrgbToLinear();

			for (std::uint32_t y = 0; y < outHeight; ++y)
			{
				const std::uint32_t rows[2] = { std::min(y * 2, height - 1), std::min(y * 2 + 1, height - 1) };

				for (std::uint32_t x = 0; x < outWidth; ++x)
				{
					const std::uint32_t columns[2] = { std::min(x * 2, width - 1), std::min(x * 2 + 1, width - 1) };

					// sRGB colour is averaged by alpha, so clear texels (often white or black) never
					// bleed into cut-out edges at a distance; data (linear, whose alpha may be
					// anything) and alpha take the plain average.
					float colour[3] = {};
					float plain[3] = {};
					float alpha = 0.0f;
					float largest = 0.0f;

					for (const auto row : rows)
					{
						for (const auto column : columns)
						{
							const auto* texel = in + (std::size_t(row) * width + column) * 4;
							const float a = static_cast<float>(texel[3]) / 255.0f;

							for (int c = 0; c < 3; ++c)
							{
								const float value = srgb ? toLinear[texel[c]] : static_cast<float>(texel[c]) / 255.0f;
								colour[c] += value * a;
								plain[c] += value;
							}

							alpha += a;
							largest = std::max(largest, a);
						}
					}

					auto* out = to + (std::size_t(y) * outWidth + x) * 4;

					for (int c = 0; c < 3; ++c)
					{
						const float value = (srgb || maximumAlpha) && alpha > 0.0f ? colour[c] / alpha : plain[c] * 0.25f;
						out[c] = srgb ? LinearToSrgb(value) : static_cast<std::uint8_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
					}

					out[3] = static_cast<std::uint8_t>(std::clamp(maximumAlpha ? largest : alpha * 0.25f, 0.0f, 1.0f) * 255.0f + 0.5f);
				}
			}

			return out;
		}

		std::vector<std::byte> Resample(const std::vector<std::byte>& source, std::uint32_t width, std::uint32_t height, std::uint32_t outWidth,
			std::uint32_t outHeight)
		{
			std::vector<std::byte> out(std::size_t(outWidth) * outHeight * 4);

			for (std::uint32_t y = 0; y < outHeight; ++y)
			{
				const auto sy = std::min<std::uint32_t>(static_cast<std::uint32_t>((std::uint64_t(y) * height + height / 2) / outHeight), height - 1);

				for (std::uint32_t x = 0; x < outWidth; ++x)
				{
					const auto sx = std::min<std::uint32_t>(static_cast<std::uint32_t>((std::uint64_t(x) * width + width / 2) / outWidth), width - 1);
					std::memcpy(out.data() + (std::size_t(y) * outWidth + x) * 4, source.data() + (std::size_t(sy) * width + sx) * 4, 4);
				}
			}

			return out;
		}

	} // namespace

	TextureArray::TextureArray(Rhi::Device& device, TextureArrayDesc descValue) : desc(std::move(descValue))
	{
		if (!IsBc7(desc.Format) && !IsRgba8(desc.Format))
		{
			throw std::invalid_argument(desc.DebugName + ": a texture array is RGBA8 or BC7");
		}

		if (desc.Width == 0 || desc.Height == 0 || desc.Layers == 0 || (IsBc7(desc.Format) && (desc.Width % 4 != 0 || desc.Height % 4 != 0)))
		{
			throw std::invalid_argument(desc.DebugName + ": empty size, no layers, or a BC7 size that is not a multiple of 4");
		}

		// Beyond the device's layer limit the image is invalid (some drivers create it anyway
		// and the upper layers read back wrong): split the layers over several arrays.
		if (const auto limit = device.GetAdapterInfo().Capabilities.MaxTextureArrayLayers; limit != 0 && desc.Layers > limit)
		{
			throw std::invalid_argument(desc.DebugName + ": " + std::to_string(desc.Layers) + " layers, more than the device's " +
				std::to_string(limit));
		}

		std::uint32_t fullChain = 1;

		while ((std::max(desc.Width, desc.Height) >> fullChain) > 0)
		{
			++fullChain;
		}

		mipLevels = desc.MipLevels == 0 ? fullChain : std::min(desc.MipLevels, fullChain);

		Rhi::TextureDesc textureDesc;
		textureDesc.Dimension = Rhi::TextureDimension::Texture2D;
		textureDesc.Extent = { desc.Width, desc.Height, 1 };
		textureDesc.PixelFormat = desc.Format;
		textureDesc.Usage = Rhi::TextureUsage::Sampled | Rhi::TextureUsage::TransferDestination | Rhi::TextureUsage::TransferSource;
		textureDesc.MipLevels = mipLevels;
		textureDesc.ArrayLayers = desc.Layers;
		textureDesc.DebugName = desc.DebugName;
		texture = device.CreateTexture(textureDesc);

		if (!texture)
		{
			throw std::runtime_error(desc.DebugName + ": the texture could not be created");
		}

		Rhi::TextureViewDesc viewDesc;
		viewDesc.Dimension = Rhi::TextureViewDimension::Texture2DArray;
		viewDesc.PixelFormat = desc.Format;
		viewDesc.MipLevelCount = mipLevels;
		viewDesc.ArrayLayerCount = desc.Layers;
		viewDesc.DebugName = desc.DebugName;
		view = device.CreateTextureView(*texture, viewDesc);
		FillPlaceholder(device);

		layers.resize(desc.Layers);
		stats.Layers = desc.Layers;

		for (std::uint32_t mip = 0; mip < mipLevels; ++mip)
		{
			stats.MemoryBytes += Rhi::GetTransferRegionBytes(desc.Format, MipExtent(desc.Width, mip), MipExtent(desc.Height, mip), 1) * desc.Layers;
		}

		decoder = &TextureArray::DecodeFile;
		worker = std::thread([this] { Work(); });
	}

	void TextureArray::FillPlaceholder(Rhi::Device& device)
	{
		// Every layer and mip holds the placeholder and is shader-readable from creation, so a
		// view sampled before the first Import never reads an undefined image. Without a timeline
		// (headless mocks) the first Import fills it in the graph instead.
		const auto solid = MakeSolid();
		std::vector<std::uint64_t> offsets;
		std::uint64_t bytes = 0;

		for (const auto& mip : solid.Mips)
		{
			offsets.push_back(bytes);
			bytes = AlignUp(bytes + mip.size(), 16);
		}

		const std::string name = desc.DebugName + " placeholder";
		auto staging = device.CreateBuffer({ bytes, Rhi::BufferUsage::TransferSource, Rhi::MemoryPreference::CpuToGpu, name });
		auto pool = device.CreateCommandPool(Rhi::QueueType::Graphics);
		// A timeline, not a fence: with the submission thread the submit happens on another thread,
		// and a host wait on a timeline may start before it.
		std::shared_ptr<Rhi::Timeline> done(device.CreateTimeline(0));
		auto commands = pool ? pool->CreateCommandList() : nullptr;

		if (!staging || !done || !commands)
		{
			return;
		}

		for (std::uint32_t mip = 0; mip < mipLevels; ++mip)
		{
			staging->Write(offsets[mip], std::as_bytes(std::span(solid.Mips[mip])));
		}

		commands->Begin();
		commands->Transition(*texture, S::Undefined, S::CopyDestination);

		for (std::uint32_t layer = 0; layer < desc.Layers; ++layer)
		{
			for (std::uint32_t mip = 0; mip < mipLevels; ++mip)
			{
				commands->CopyBufferToTexture(*staging, *texture,
					{ offsets[mip], { mip, layer }, {}, { MipExtent(desc.Width, mip), MipExtent(desc.Height, mip), 1 } });
			}
		}

		commands->Transition(*texture, S::CopyDestination, S::ShaderRead);
		commands->End();
		Rhi::CommandList* const lists[] = { commands.get() };
		Rhi::SubmitDesc submit;
		submit.CommandLists = lists;
		const Rhi::TimelinePoint signal{ done.get(), 1 };
		submit.SignalTimelines = { &signal, 1 };
		device.GetQueue(Rhi::QueueType::Graphics).Submit(submit);

		if (!done->Wait(1))
		{
			throw std::runtime_error(desc.DebugName + ": the placeholder fill did not complete");
		}

		initialized = true;
	}

	TextureArray::~TextureArray()
	{
		{
			std::lock_guard lock(mutex);
			stopping = true;
		}

		wake.notify_all();
		worker.join();
	}

	void TextureArray::Check(std::uint32_t layer) const
	{
		if (layer >= layers.size())
		{
			throw std::out_of_range(desc.DebugName + ": layer out of range");
		}
	}

	void TextureArray::Validate(const TextureLayerData& data) const
	{
		const auto fail = [&](const char* why)
		{
			throw std::invalid_argument(desc.DebugName + ": " + why);
		};

		if (data.Mips.empty() || data.Width == 0 || data.Height == 0)
		{
			fail("layer data has no pixels");
		}

		if (IsBc7(desc.Format) != IsBc7(data.Format) || (!IsBc7(data.Format) && !IsRgba8(data.Format)))
		{
			fail("layer data is not in the array's format family");
		}

		if (IsRgba8(data.Format))
		{
			if (data.Mips[0].size() != std::size_t(data.Width) * data.Height * 4)
			{
				fail("RGBA8 mip 0 does not match its size");
			}

			// Resized data keeps only mip 0, so its other mips are not checked.
			if (data.Width == desc.Width && data.Height == desc.Height)
			{
				for (std::uint32_t mip = 1; mip < std::min<std::size_t>(data.Mips.size(), mipLevels); ++mip)
				{
					if (data.Mips[mip].size() != std::size_t(MipExtent(desc.Width, mip)) * MipExtent(desc.Height, mip) * 4)
					{
						fail("an RGBA8 mip does not match its size");
					}
				}
			}

			return;
		}

		if (data.Width != desc.Width || data.Height != desc.Height || data.Mips.size() < mipLevels)
		{
			fail("BC7 layer data must match the array's size and carry every mip");
		}

		for (std::uint32_t mip = 0; mip < mipLevels; ++mip)
		{
			if (data.Mips[mip].size() !=
				Rhi::GetTransferRegionBytes(desc.Format, MipExtent(desc.Width, mip), MipExtent(desc.Height, mip), 1))
			{
				fail("a BC7 mip does not match its size");
			}
		}
	}

	bool TextureArray::NeedsWork(const TextureLayerData& data) const
	{
		return IsRgba8(data.Format) && (data.Width != desc.Width || data.Height != desc.Height || data.Mips.size() < mipLevels);
	}

	void TextureArray::Normalize(TextureLayerData& data) const
	{
		Validate(data);

		if (data.Mips.size() > mipLevels)
		{
			data.Mips.resize(mipLevels);
		}

		if (IsRgba8(data.Format))
		{
			if (data.Width != desc.Width || data.Height != desc.Height)
			{
				data.Mips[0] = Resample(data.Mips[0], data.Width, data.Height, desc.Width, desc.Height);
				data.Mips.resize(1);
				data.Width = desc.Width;
				data.Height = desc.Height;
			}

			const std::size_t given = data.Mips.size();
			const bool maximum = data.AlphaMips == AlphaMipFilter::Maximum;

			while (data.Mips.size() < mipLevels)
			{
				const auto mip = static_cast<std::uint32_t>(data.Mips.size()) - 1;
				data.Mips.push_back(
					Downsample(data.Mips[mip], MipExtent(desc.Width, mip), MipExtent(desc.Height, mip), IsSrgb(desc.Format), maximum));
			}

			const float cutoff = data.AlphaMips == AlphaMipFilter::Array ? desc.AlphaCoverageCutoff
				: data.AlphaMips == AlphaMipFilter::Coverage ? (data.AlphaCutoff > 0.0f ? data.AlphaCutoff : desc.AlphaCoverageCutoff)
															 : 0.0f;

			if (cutoff > 0.0f && given < data.Mips.size())
			{
				const float scale = data.AlphaMips == AlphaMipFilter::Coverage ? std::clamp(data.CoverageScale, 0.0f, 1.0f) : 1.0f;
				const float coverage = ::Engine::AlphaCoverage(data.Mips[0], cutoff) * scale;

				for (std::size_t mip = given; mip < data.Mips.size(); ++mip)
				{
					::Engine::PreserveAlphaCoverage(data.Mips[mip], cutoff, coverage);
				}
			}

			return;
		}
	}

	TextureLayerData TextureArray::MakeSolid() const
	{
		TextureLayerData data;
		data.Format = desc.Format;
		data.Width = desc.Width;
		data.Height = desc.Height;

		for (std::uint32_t mip = 0; mip < mipLevels; ++mip)
		{
			const auto width = MipExtent(desc.Width, mip);
			const auto height = MipExtent(desc.Height, mip);
			auto& bytes = data.Mips.emplace_back(Rhi::GetTransferRegionBytes(desc.Format, width, height, 1));
			const auto block = SolidBc7Block(desc.Placeholder);

			for (std::size_t at = 0; at < bytes.size();)
			{
				if (IsBc7(desc.Format))
				{
					std::memcpy(bytes.data() + at, block.data(), block.size());
					at += block.size();
				}
				else
				{
					std::memcpy(bytes.data() + at, desc.Placeholder.data(), 4);
					at += 4;
				}
			}
		}

		return data;
	}

	void TextureArray::Queue(std::uint32_t layer, std::shared_ptr<const TextureLayerData> data, bool placeholder)
	{
		auto& slot = layers[layer];
		const bool queued = slot.Pending != nullptr;
		slot.Whole = slot.Whole || slot.HasImage || (queued && slot.NextMip < mipLevels);
		slot.Pending = std::move(data);
		slot.PendingPlaceholder = placeholder;
		slot.NextMip = mipLevels;

		if (!queued)
		{
			uploads.push_back(layer);
		}
	}

	void TextureArray::DropRequests(std::uint32_t layer)
	{
		const auto removed = std::erase_if(requests,
			[&](const LoadRequest& request)
			{
				return request.Layer == layer;
			});

		if (removed > 0)
		{
			std::make_heap(requests.begin(), requests.end(),
				[](const LoadRequest& a, const LoadRequest& b)
				{
					return a.Priority != b.Priority ? a.Priority < b.Priority : a.Order > b.Order;
				});
		}
	}

	void TextureArray::Request(std::uint32_t layer, std::filesystem::path path, int priority)
	{
		auto& slot = layers[layer];

		{
			std::lock_guard lock(mutex);
			DropRequests(layer);
			slot.Request = nextId++;
			requests.push_back({ layer, slot.Request, priority, nextOrder++, std::move(path) });
			std::push_heap(requests.begin(), requests.end(),
				[](const LoadRequest& a, const LoadRequest& b)
				{
					return a.Priority != b.Priority ? a.Priority < b.Priority : a.Order > b.Order;
				});
		}

		slot.InFlight = true;
		wake.notify_one();
	}

	void TextureArray::SetLayer(std::uint32_t layer, TextureLayerData data)
	{
		Check(layer);
		auto& slot = layers[layer];

		if (desc.GenerateMipsInBackground && NeedsWork(data))
		{
			Validate(data);
			slot.Source.clear();
			slot.Error.clear();

			{
				std::lock_guard lock(mutex);
				DropRequests(layer);
				slot.Request = nextId++;
				LoadRequest request{ layer, slot.Request, 0, nextOrder++, {} };
				request.Data = std::make_shared<TextureLayerData>(std::move(data));
				requests.push_back(std::move(request));
				std::push_heap(requests.begin(), requests.end(),
					[](const LoadRequest& a, const LoadRequest& b)
					{
						return a.Priority != b.Priority ? a.Priority < b.Priority : a.Order > b.Order;
					});
			}

			slot.InFlight = true;
			wake.notify_one();
			return;
		}

		Normalize(data);
		SetLayer(layer, std::make_shared<const TextureLayerData>(std::move(data)));
	}

	std::shared_ptr<const TextureLayerData> TextureArray::PrepareLayer(TextureLayerData data) const
	{
		Normalize(data);
		return std::make_shared<const TextureLayerData>(std::move(data));
	}

	void TextureArray::SetLayer(std::uint32_t layer, std::shared_ptr<const TextureLayerData> prepared)
	{
		Check(layer);

		if (!prepared || IsBc7(prepared->Format) != IsBc7(desc.Format) || prepared->Width != desc.Width ||
			prepared->Height != desc.Height || prepared->Mips.size() != mipLevels)
		{
			throw std::invalid_argument(desc.DebugName + ": SetLayer data was not prepared by this array");
		}

		auto& slot = layers[layer];

		if (slot.Request != 0 || slot.InFlight)
		{
			std::lock_guard lock(mutex);
			DropRequests(layer);
		}

		slot.Request = 0;
		slot.InFlight = false;
		slot.Source.clear();
		slot.Error.clear();
		Queue(layer, std::move(prepared), false);
	}

	void TextureArray::LoadLayer(std::uint32_t layer, std::filesystem::path path, int priority)
	{
		Check(layer);
		layers[layer].Source = path;
		layers[layer].Stamp = {};
		Request(layer, std::move(path), priority);
	}

	void TextureArray::CancelLayer(std::uint32_t layer)
	{
		Check(layer);
		auto& slot = layers[layer];

		{
			std::lock_guard lock(mutex);
			DropRequests(layer);
		}

		slot.Request = 0;
		slot.InFlight = false;

		if (slot.Pending && slot.NextMip == mipLevels)
		{
			slot.Whole = false;
			slot.Pending.reset();
			std::erase(uploads, layer);
		}
	}

	void TextureArray::ClearLayer(std::uint32_t layer)
	{
		CancelLayer(layer);
		auto& slot = layers[layer];
		slot.Source.clear();
		slot.Error.clear();
		slot.HasImage = false;
		Queue(layer, std::make_shared<const TextureLayerData>(MakeSolid()), true);
	}

	TextureLayerState TextureArray::GetLayerState(std::uint32_t layer) const
	{
		Check(layer);
		const auto& slot = layers[layer];

		if (slot.InFlight)
		{
			return TextureLayerState::Loading;
		}

		if (slot.Pending && !slot.PendingPlaceholder)
		{
			return TextureLayerState::Uploading;
		}

		if (!slot.Error.empty())
		{
			return TextureLayerState::Failed;
		}

		return slot.HasImage ? TextureLayerState::Resident : TextureLayerState::Placeholder;
	}

	const std::string& TextureArray::GetLayerError(std::uint32_t layer) const
	{
		Check(layer);
		return layers[layer].Error;
	}

	float TextureArray::GetMinLod(std::uint32_t layer) const
	{
		Check(layer);
		return layers[layer].MinLod;
	}

	void TextureArray::SetHotReload(bool enabled, double pollSecondsValue)
	{
		hotReload = enabled;
		pollSeconds = std::max(pollSecondsValue, 0.05);
		nextPoll = {};
	}

	void TextureArray::SetDecoder(TextureLayerDecoder decoderValue)
	{
		std::lock_guard lock(mutex);
		decoder = decoderValue ? std::move(decoderValue) : TextureLayerDecoder(&TextureArray::DecodeFile);
	}

	std::optional<TextureLayerData> TextureArray::DecodeFile(
		std::span<const std::byte> bytes, const std::filesystem::path& path, const TextureArrayDesc& desc, std::string& error)
	{
		(void)path;
		const bool wantBc7 = IsBc7(desc.Format);

		if (const auto parsed = Swim::Assets::ParseKtx2Metadata(bytes))
		{
			const auto& metadata = parsed.Metadata;
			using P = Swim::Assets::TexturePayloadFormat;
			TextureLayerData data;

			switch (metadata.PayloadFormat)
			{
			case P::BC7UNorm: data.Format = F::BC7Unorm; break;
			case P::BC7SRgb: data.Format = F::BC7UnormSrgb; break;
			case P::RGBA8UNorm: data.Format = F::RGBA8Unorm; break;
			case P::RGBA8SRgb: data.Format = F::RGBA8UnormSrgb; break;
			default: error = "unsupported KTX2 format (BC7 or RGBA8 only)"; return std::nullopt;
			}

			if (metadata.Supercompression != Swim::Assets::TextureSupercompression::None || metadata.Dimension != Swim::Assets::TextureDimension::Texture2D ||
				metadata.ArrayLayers != 1 || metadata.FaceCount != 1 || metadata.Mips.empty())
			{
				error = "KTX2 must be one uncompressed 2D image";
				return std::nullopt;
			}

			if (IsBc7(data.Format) != wantBc7)
			{
				error = wantBc7 ? "the array is BC7 but the file is RGBA8" : "the array is RGBA8 but the file is BC7";
				return std::nullopt;
			}

			data.Width = metadata.Width;
			data.Height = metadata.Height;

			for (const auto& mip : metadata.Mips)
			{
				const auto* first = bytes.data() + mip.OffsetBytes;
				data.Mips.emplace_back(first, first + mip.SizeBytes);
			}

			return data;
		}

#if SWIM_ENABLE_DEV_ASSET_AUTOCOOK
		namespace A = Swim::AssetCompiler;
		const auto mime = A::DetectSourceImageMimeType(bytes);

		if (mime == A::SourceImageMimeType::Png || mime == A::SourceImageMimeType::Jpeg)
		{
			auto compiled = A::CompileSourceImageTexture(bytes, mime,
				IsSrgb(desc.Format) ? Swim::Assets::TextureColorSpace::SRgb : Swim::Assets::TextureColorSpace::Linear, Swim::Assets::TextureSemantic::Color,
				wantBc7 ? A::CookedTextureEncoding::Bc7 : A::CookedTextureEncoding::Rgba8, desc.AlphaCoverageCutoff);

			if (!compiled)
			{
				error = compiled.Error.Message.empty() ? "the image could not be decoded" : compiled.Error.Message;
				return std::nullopt;
			}

			using P = Swim::Assets::TexturePayloadFormat;

			for (auto& payload : compiled.Asset.Payloads)
			{
				TextureLayerData data;

				switch (payload.Format)
				{
				case P::BC7UNorm: data.Format = F::BC7Unorm; break;
				case P::BC7SRgb: data.Format = F::BC7UnormSrgb; break;
				case P::RGBA8UNorm: data.Format = F::RGBA8Unorm; break;
				case P::RGBA8SRgb: data.Format = F::RGBA8UnormSrgb; break;
				default: continue;
				}

				if (IsBc7(data.Format) != wantBc7 || payload.Supercompression != Swim::Assets::TextureSupercompression::None || payload.Mips.empty())
				{
					continue;
				}

				data.Width = payload.Mips.front().Width;
				data.Height = payload.Mips.front().Height;

				for (const auto& mip : payload.Mips)
				{
					const auto* first = payload.Bytes.data() + mip.OffsetBytes;
					data.Mips.emplace_back(first, first + mip.SizeBytes);
				}

				return data;
			}

			error = "the image compiler produced no payload in the array's format";
			return std::nullopt;
		}
#endif

		error = "not a KTX2 file (PNG and JPEG need a development build)";
		return std::nullopt;
	}

	void TextureArray::Work()
	{
		const auto order = [](const LoadRequest& a, const LoadRequest& b)
		{
			return a.Priority != b.Priority ? a.Priority < b.Priority : a.Order > b.Order;
		};

		for (;;)
		{
			LoadRequest request;
			TextureLayerDecoder decode;

			{
				std::unique_lock lock(mutex);
				wake.wait(lock,
					[&]
					{
						return stopping || !requests.empty();
					});

				if (stopping)
				{
					return;
				}

				std::pop_heap(requests.begin(), requests.end(), order);
				request = std::move(requests.back());
				requests.pop_back();
				decode = decoder;
			}

			LoadResult result;
			result.Layer = request.Layer;
			result.Id = request.Id;

			if (request.Data)
			{
				try
				{
					Normalize(*request.Data);
					result.Data = std::move(*request.Data);
				}
				catch (const std::exception& exception)
				{
					result.Error = exception.what();
				}

				std::lock_guard lock(mutex);
				results.push_back(std::move(result));
				continue;
			}

			std::error_code stampError;
			result.Stamp = std::filesystem::last_write_time(request.Path, stampError);

			try
			{
				std::ifstream file(request.Path, std::ios::binary);

				if (!file)
				{
					result.Error = "cannot open " + request.Path.string();
				}
				else
				{
					std::vector<std::byte> bytes;
					file.seekg(0, std::ios::end);
					bytes.resize(static_cast<std::size_t>(file.tellg()));
					file.seekg(0, std::ios::beg);
					file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));

					if (!file)
					{
						result.Error = "cannot read " + request.Path.string();
					}
					else
					{
						result.Data = decode(bytes, request.Path, desc, result.Error);

						if (result.Data)
						{
							Normalize(*result.Data);
						}
						else if (result.Error.empty())
						{
							result.Error = "the decoder rejected the file";
						}
					}
				}
			}
			catch (const std::exception& exception)
			{
				result.Data.reset();
				result.Error = exception.what();
			}

			std::lock_guard lock(mutex);
			results.push_back(std::move(result));
		}
	}

	void TextureArray::PollReloads()
	{
		const auto now = std::chrono::steady_clock::now();

		if (!hotReload || now < nextPoll)
		{
			return;
		}

		// A bounded number of file checks per frame; one full sweep per poll interval.
		constexpr std::uint32_t ChecksPerFrame = 64;

		for (std::uint32_t checked = 0; checked < ChecksPerFrame && pollCursor < layers.size(); ++checked, ++pollCursor)
		{
			auto& slot = layers[pollCursor];

			if (slot.Source.empty() || slot.InFlight)
			{
				continue;
			}

			std::error_code error;
			const auto stamp = std::filesystem::last_write_time(slot.Source, error);

			if (!error && stamp != slot.Stamp)
			{
				Request(pollCursor, slot.Source, 0);
				++stats.Reloads;
			}
		}

		if (pollCursor >= layers.size())
		{
			pollCursor = 0;
			nextPoll = now + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(pollSeconds));
		}
	}

	GraphTexture TextureArray::Import(RenderGraph& graph, std::uint64_t uploadBudget)
	{
		std::vector<LoadResult> finished;

		{
			std::lock_guard lock(mutex);
			finished.swap(results);
		}

		for (auto& result : finished)
		{
			auto& slot = layers[result.Layer];

			if (result.Id != slot.Request)
			{
				continue;
			}

			slot.InFlight = false;
			slot.Stamp = result.Stamp;

			if (result.Data)
			{
				slot.Error.clear();
				Queue(result.Layer, std::make_shared<const TextureLayerData>(std::move(*result.Data)), false);
			}
			else
			{
				slot.Error = std::move(result.Error);
			}
		}

		PollReloads();

		const auto imported = graph.ImportTexture(*texture, initialized ? S::ShaderRead : S::Undefined);

		if (!initialized)
		{
			// Every subresource starts as the placeholder, copied from one solid mip 0.
			const auto solid = std::make_shared<const TextureLayerData>(MakeSolid());
			const std::string fillName = desc.DebugName + " placeholder";
			const auto staging = graph.CreateUpload({ solid->Mips[0].size(), Rhi::BufferUsage::TransferSource, 16, fillName },
				[solid](std::span<std::byte> bytes)
				{
					std::memcpy(bytes.data(), solid->Mips[0].data(), solid->Mips[0].size());
				});
			graph.AddPass(
				fillName, Rhi::QueueType::Transfer,
				[&](RenderGraphBuilder& b)
				{
					b.Read(staging, S::CopySource);
					b.Write(imported, S::CopyDestination);
				},
				[imported, staging, layerCount = desc.Layers, mips = mipLevels, width = desc.Width, height = desc.Height](RenderCommandContext& c)
				{
					const auto source = c.GetRange(staging);
					auto& target = c.Get(imported);

					for (std::uint32_t layer = 0; layer < layerCount; ++layer)
					{
						for (std::uint32_t mip = 0; mip < mips; ++mip)
						{
							c.Commands().CopyBufferToTexture(
								*source.Buffer, target, { source.Offset, { mip, layer }, {}, { MipExtent(width, mip), MipExtent(height, mip), 1 } });
						}
					}
				});
			graph.Export(imported, S::ShaderRead);
			initialized = true;
			graph.OnAbandoned(
				[this]
				{
					initialized = false;
				});
		}

		struct Copy
		{
			std::shared_ptr<const TextureLayerData> Data;
			std::uint32_t Layer = 0;
			std::uint32_t Mip = 0;
			std::uint64_t StagingOffset = 0;
		};

		auto copies = std::make_shared<std::vector<Copy>>();
		std::uint64_t staged = 0;
		stats.UploadedBytes = 0;

		// The layers the uploads below take, as they were, to put back if the graph never runs.
		struct Taken
		{
			std::uint32_t Index = 0;
			Layer Before;
			bool Dequeued = false;
		};

		auto taken = std::make_shared<std::vector<Taken>>();

		while (!uploads.empty())
		{
			const auto layer = uploads.front();
			auto& slot = layers[layer];
			const auto last = slot.Whole ? 0u : slot.NextMip - 1;
			std::uint64_t need = 0;

			for (auto mip = slot.NextMip; mip-- > last;)
			{
				need = AlignUp(need + slot.Pending->Mips[mip].size(), 16);
			}

			if (!copies->empty() && staged + need > uploadBudget)
			{
				break;
			}

			taken->push_back({ layer, slot, false });

			for (auto mip = slot.NextMip; mip-- > last;)
			{
				const auto size = slot.Pending->Mips[mip].size();
				copies->push_back({ slot.Pending, layer, mip, staged });
				staged = AlignUp(staged + size, 16);
				stats.UploadedBytes += size;
			}

			slot.MinLod = static_cast<float>(last);
			slot.NextMip = last;

			if (last == 0)
			{
				slot.HasImage = !slot.PendingPlaceholder;
				slot.Whole = false;
				slot.Pending.reset();
				taken->back().Dequeued = true;
				uploads.pop_front();
			}
		}

		if (!taken->empty())
		{
			graph.OnAbandoned(
				[this, taken]
				{
					for (auto entry = taken->rbegin(); entry != taken->rend(); ++entry)
					{
						auto& slot = layers[entry->Index];
						slot.Pending = entry->Before.Pending;
						slot.PendingPlaceholder = entry->Before.PendingPlaceholder;
						slot.NextMip = entry->Before.NextMip;
						slot.Whole = entry->Before.Whole;
						slot.MinLod = entry->Before.MinLod;
						slot.HasImage = entry->Before.HasImage;

						if (entry->Dequeued)
						{
							uploads.push_front(entry->Index);
						}
					}
				});
		}

		if (!copies->empty())
		{
			// The barriers cover only the layers the copies touch, as contiguous runs (or the
			// whole array past a run limit). A debug layer checks each transitioned
			// subresource against the texture's other subresources: two whole-array
			// transitions of a 1435-layer, 5-mip array measured about 230 ms there, a few
			// layers well under 1 ms. Without a debug layer the cost is the same either way.
			std::vector<std::uint32_t> touched;
			touched.reserve(copies->size());

			for (const auto& copy : *copies)
			{
				touched.push_back(copy.Layer);
			}

			std::sort(touched.begin(), touched.end());
			touched.erase(std::unique(touched.begin(), touched.end()), touched.end());
			std::vector<Rhi::TextureSubresourceRange> runs;

			for (const auto layer : touched)
			{
				if (!runs.empty() && runs.back().BaseArrayLayer + runs.back().ArrayLayerCount == layer)
				{
					++runs.back().ArrayLayerCount;
				}
				else
				{
					Rhi::TextureSubresourceRange range;
					range.BaseArrayLayer = layer;
					range.ArrayLayerCount = 1;
					runs.push_back(range);
				}
			}

			constexpr std::size_t maxRuns = 64; // Declaring a use per run costs per covered cell.

			if (runs.size() > maxRuns)
			{
				runs.assign(1, Rhi::TextureSubresourceRange{});
			}

			const std::string uploadName = desc.DebugName + " upload";
			const auto staging = graph.CreateUpload({ staged, Rhi::BufferUsage::TransferSource, 16, uploadName },
				[copies](std::span<std::byte> bytes)
				{
					for (const auto& copy : *copies)
					{
						const auto& mip = copy.Data->Mips[copy.Mip];
						std::memcpy(bytes.data() + copy.StagingOffset, mip.data(), mip.size());
					}
				});
			graph.AddPass(
				uploadName, Rhi::QueueType::Transfer,
				[&](RenderGraphBuilder& b)
				{
					// One use per run of layers, not per copy: per-copy subresource uses cost O(n^2)
					// to declare with thousands of copies in a pass. The transition keeps contents.
					b.Read(staging, S::CopySource);

					for (const auto& range : runs)
					{
						b.Write(imported, S::CopyDestination, range);
					}
				},
				[imported, staging, copies, width = desc.Width, height = desc.Height](RenderCommandContext& c)
				{
					const auto source = c.GetRange(staging);

					for (const auto& copy : *copies)
					{
						// Each copy asks for exactly its own subresource, which one declared run holds.
						Rhi::TextureSubresourceRange cell;
						cell.BaseMipLevel = copy.Mip;
						cell.MipLevelCount = 1;
						cell.BaseArrayLayer = copy.Layer;
						cell.ArrayLayerCount = 1;
						auto& target = c.Get(imported, cell);
						c.Commands().CopyBufferToTexture(*source.Buffer, target,
							{ source.Offset + copy.StagingOffset, { copy.Mip, copy.Layer }, {}, { MipExtent(width, copy.Mip), MipExtent(height, copy.Mip), 1 } });
					}
				});
			// Back to ShaderRead right after the copies, so draws that sample the array
			// through GetView without declaring a graph read never see it as a copy target.
			graph.AddPass(
				desc.DebugName + " ready", Rhi::QueueType::Graphics,
				[&](RenderGraphBuilder& b)
				{
					for (const auto& range : runs)
					{
						b.Read(imported, S::ShaderRead, range);
					}

					b.SideEffect();
				},
				[](RenderCommandContext&)
				{
				});
		}

		stats.Resident = stats.Loading = stats.Uploading = stats.Failed = 0;
		stats.PendingUploadBytes = 0;

		for (std::uint32_t layer = 0; layer < layers.size(); ++layer)
		{
			switch (GetLayerState(layer))
			{
			case TextureLayerState::Resident: ++stats.Resident; break;
			case TextureLayerState::Loading: ++stats.Loading; break;
			case TextureLayerState::Uploading: ++stats.Uploading; break;
			case TextureLayerState::Failed: ++stats.Failed; break;
			default: break;
			}

			if (const auto& pending = layers[layer].Pending)
			{
				for (std::uint32_t mip = 0; mip < layers[layer].NextMip; ++mip)
				{
					stats.PendingUploadBytes += pending->Mips[mip].size();
				}
			}
		}

		return imported;
	}

	GraphBuffer TextureArray::ImportMinLod(RenderGraph& graph) const
	{
		std::vector<float> values(layers.size());

		for (std::size_t layer = 0; layer < layers.size(); ++layer)
		{
			values[layer] = layers[layer].MinLod;
		}

		return graph.CreateUpload(std::as_bytes(std::span(values)), desc.DebugName + " min lod", Rhi::BufferUsage::Storage, 16);
	}

} // namespace Swim::Render
