#include "Engine/Systems/Renderer/Residency/TextureResidency.h"
#include "Engine/Systems/Renderer/RHI/RhiFormatInfo.h"

#include <algorithm>
#include <cstring>

namespace Swim::Render
{

	namespace
	{

		std::optional<Rhi::Format> ToRhiFormat(Assets::TexturePayloadFormat format)
		{
			using F = Assets::TexturePayloadFormat;

			switch (format)
			{
			case F::R8UNorm:
				return Rhi::Format::R8Unorm;
			case F::RG8UNorm:
				return Rhi::Format::RG8Unorm;
			case F::RGBA8UNorm:
				return Rhi::Format::RGBA8Unorm;
			case F::RGBA8SRgb:
				return Rhi::Format::RGBA8UnormSrgb;
			case F::RGBA16Float:
				return Rhi::Format::RGBA16Float;
			case F::BC1UNorm:
				return Rhi::Format::BC1RGBAUnorm;
			case F::BC1SRgb:
				return Rhi::Format::BC1RGBAUnormSrgb;
			case F::BC3UNorm:
				return Rhi::Format::BC3Unorm;
			case F::BC3SRgb:
				return Rhi::Format::BC3UnormSrgb;
			case F::BC5UNorm:
				return Rhi::Format::BC5Unorm;
			case F::BC7UNorm:
				return Rhi::Format::BC7Unorm;
			case F::BC7SRgb:
				return Rhi::Format::BC7UnormSrgb;
			default:
				return std::nullopt;
			}
		}

		bool IsComplete(const Rhi::TimelinePoint& point)
		{
			return !point.Semaphore || point.Semaphore->GetCompletedValue() >= point.Value;
		}

	} // namespace

	TextureResidency::TextureResidency(Rhi::Device& device, const TextureResidencyDesc& desc)
		: device(device), name(desc.DebugName.empty() ? "TextureResidency" : desc.DebugName),
		  textures({ desc.MaxTextures, "TextureResidency textures" }), blockCompression(device.GetAdapterInfo().Capabilities.BcTextureCompression)
	{
	}

	TextureResidency::~TextureResidency()
	{
		try
		{
			Drain();
		}
		catch (...)
		{
		}
	}

	std::optional<TexturePayloadSelection> TextureResidency::SelectPayload(const Assets::TextureAsset& texture, bool blockCompression)
	{
		if (texture.Dimension != Assets::TextureDimension::Texture2D || texture.Depth != 1 || texture.ArrayLayers != 1)
		{
			return std::nullopt;
		}

		for (std::size_t i = 0; i < texture.Payloads.size(); ++i)
		{
			const auto& payload = texture.Payloads[i];

			if (payload.Container != Assets::TextureContainerFormat::NativeMipData ||
				payload.Supercompression != Assets::TextureSupercompression::None)
			{
				continue;
			}

			const auto format = ToRhiFormat(payload.Format);

			if (format && (blockCompression || !Rhi::IsBlockCompressed(*format)))
			{
				return TexturePayloadSelection{ i, *format };
			}
		}

		return std::nullopt;
	}

	GpuTextureHandle TextureResidency::CreateTexture(const Assets::TextureAsset& texture, std::string_view debugName)
	{
		return CreateTexture(texture, debugName, nullptr);
	}

	GpuTextureHandle TextureResidency::CreateTexture(Assets::TextureAsset&& texture, std::string_view debugName)
	{
		return CreateTexture(texture, debugName, &texture);
	}

	GpuTextureHandle TextureResidency::CreateTexture(
		const Assets::TextureAsset& texture, std::string_view debugName, Assets::TextureAsset* adopt)
	{
		const auto selection = SelectPayload(texture, blockCompression);

		if (!selection)
		{
			throw std::invalid_argument(name + ": texture has no native-mip 2D payload this device can sample");
		}

		const auto& payload = texture.Payloads[selection->Payload];
		const auto block = Rhi::GetTransferBlockInfo(selection->Format);

		if (!texture.Width || !texture.Height || payload.Mips.empty())
		{
			throw std::invalid_argument(name + ": texture needs an extent and at least one mip");
		}

		std::uint32_t maxMips = 1;

		for (auto extent = std::max(texture.Width, texture.Height); extent > 1; extent >>= 1)
		{
			++maxMips;
		}

		if (payload.Mips.size() > maxMips)
		{
			throw std::invalid_argument(name + ": texture has more mips than its extent allows");
		}

		Internal::TextureRecord record;
		record.Alignment = std::max<std::uint64_t>(4, block.Bytes);
		// Validate the chain and lay it out for upload. A payload already in that layout
		// (mips in order at aligned offsets, as the cooker writes them) is uploaded from
		// its own bytes: adopted when the caller hands the asset over, else copied once.
		bool inPlace = true;
		std::uint64_t packed = 0;

		for (std::uint32_t mip = 0; mip < payload.Mips.size(); ++mip)
		{
			const auto& source = payload.Mips[mip];
			const std::uint32_t width = std::max(1u, texture.Width >> mip);
			const std::uint32_t height = std::max(1u, texture.Height >> mip);
			const std::uint64_t size = Rhi::GetTransferRegionBytes(selection->Format, width, height, 1);

			if (source.Width != width || source.Height != height || source.Depth != 1 || source.SizeBytes != size ||
				source.OffsetBytes > payload.Bytes.size() || size > payload.Bytes.size() - source.OffsetBytes)
			{
				throw std::invalid_argument(name + ": texture mip " + std::to_string(mip) + " is not a tightly packed chain level");
			}

			const std::uint64_t offset = (packed + record.Alignment - 1) / record.Alignment * record.Alignment;
			inPlace = inPlace && source.OffsetBytes == offset;
			packed = offset + size;
			record.Mips.push_back({ { width, height, 1 }, offset, size });
			record.TexelBytes += size;
		}

		const auto stats = textures.GetStats();

		if (!stats.FreeSlots && stats.SlotHighWater >= stats.MaxSlots)
		{
			throw std::length_error(name + " has no free texture slots");
		}

		const std::string label = debugName.empty() ? name + " texture" : std::string(debugName);
		Rhi::TextureDesc desc;
		desc.Extent = { texture.Width, texture.Height, 1 };
		desc.PixelFormat = selection->Format;
		desc.MipLevels = static_cast<std::uint32_t>(record.Mips.size());
		desc.Usage = Rhi::TextureUsage::Sampled | Rhi::TextureUsage::TransferDestination | Rhi::TextureUsage::TransferSource;
		desc.DebugName = label;
		record.Texture = device.CreateTexture(desc);

		if (!record.Texture)
		{
			throw std::runtime_error(name + ": texture creation failed");
		}

		Rhi::TextureViewDesc view;
		view.PixelFormat = selection->Format;
		view.MipLevelCount = desc.MipLevels;
		view.DebugName = label;
		record.View = device.CreateTextureView(*record.Texture, view);

		if (!record.View)
		{
			throw std::runtime_error(name + ": texture view creation failed");
		}

		// Last, after every failure point: an adopted payload must stay with the asset on failure.
		std::shared_ptr<std::vector<std::byte>> bytes;

		if (inPlace && adopt)
		{
			bytes = std::make_shared<std::vector<std::byte>>(std::move(adopt->Payloads[selection->Payload].Bytes));
			bytes->resize(static_cast<std::size_t>(packed));
		}
		else if (inPlace)
		{
			bytes = std::make_shared<std::vector<std::byte>>(payload.Bytes.begin(), payload.Bytes.begin() + static_cast<std::ptrdiff_t>(packed));
		}
		else
		{
			bytes = std::make_shared<std::vector<std::byte>>(static_cast<std::size_t>(packed));

			for (std::uint32_t mip = 0; mip < payload.Mips.size(); ++mip)
			{
				std::memcpy(bytes->data() + record.Mips[mip].Offset, payload.Bytes.data() + payload.Mips[mip].OffsetBytes,
					static_cast<std::size_t>(record.Mips[mip].Size));
			}
		}

		record.Bytes = std::move(bytes);

		auto handle = textures.TryCreate(std::move(record));

		if (!handle)
		{
			throw std::logic_error(name + " texture slot disappeared after its capacity check");
		}

		return *handle;
	}

	bool TextureResidency::DestroyTexture(GpuTextureHandle texture, Rhi::TimelinePoint lastUse)
	{
		auto* record = textures.Get(texture);

		if (!record)
		{
			return false;
		}

		if (record->State == GpuUploadState::Recorded)
		{
			throw std::logic_error(name + " texture upload is recorded; commit or abort it before destruction");
		}

		if (record->State == GpuUploadState::Uploading && !IsComplete(record->Upload))
		{
			if (!lastUse.Semaphore)
			{
				lastUse = record->Upload;
			}
			else if (lastUse.Semaphore == record->Upload.Semaphore)
			{
				lastUse.Value = std::max(lastUse.Value, record->Upload.Value);
			}
		}

		return textures.Release(texture, lastUse);
	}

	void TextureResidency::CommitUploads(Rhi::TimelinePoint completion)
	{
		if (!importPending)
		{
			return;
		}

		if (!completion.Semaphore)
		{
			throw std::invalid_argument(name + " upload commit needs the graph's completion point");
		}

		for (auto handle : recorded)
		{
			if (auto* record = textures.Get(handle))
			{
				record->State = GpuUploadState::Uploading;
				record->Upload = completion;
				record->Bytes.reset(); // The executor already copied the mips into staging.
			}
		}

		recorded.clear();
		importPending = false;
	}

	void TextureResidency::AbortUploads()
	{
		for (auto handle : recorded)
		{
			if (auto* record = textures.Get(handle))
			{
				record->State = GpuUploadState::PendingUpload;
			}
		}

		recorded.clear();
		importPending = false;
	}

	std::size_t TextureResidency::Collect()
	{
		std::size_t changed = 0;
		textures.ForEach(
			[&](GpuTextureHandle, Internal::TextureRecord& record)
			{
				if (record.State == GpuUploadState::Uploading && IsComplete(record.Upload))
				{
					record.State = GpuUploadState::Resident;
					record.Upload = {};
					++changed;
				}

			});
		return changed + textures.CollectRetired();
	}

	void TextureResidency::Drain()
	{
		textures.ForEach(
			[&](GpuTextureHandle, Internal::TextureRecord& record)
			{
				if (record.State == GpuUploadState::Uploading && !IsComplete(record.Upload) &&
					!record.Upload.Semaphore->Wait(record.Upload.Value))
				{
					throw std::runtime_error("TextureResidency upload wait failed");
				}

			});
		textures.Drain();
		Collect();
	}

	GpuUploadState TextureResidency::GetState(GpuTextureHandle texture) const
	{
		const auto* record = textures.Get(texture);
		return record ? record->State : GpuUploadState::Invalid;
	}

	Rhi::Texture* TextureResidency::GetTexture(GpuTextureHandle texture) const
	{
		const auto* record = textures.Get(texture);
		return record ? record->Texture.get() : nullptr;
	}

	Rhi::TextureView* TextureResidency::GetView(GpuTextureHandle texture) const
	{
		const auto* record = textures.Get(texture);
		return record ? record->View.get() : nullptr;
	}

	TextureResidencyStats TextureResidency::GetStats() const
	{
		TextureResidencyStats stats;
		textures.ForEach(
			[&](GpuTextureHandle, const Internal::TextureRecord& record)
			{
				switch (record.State)
				{
				case GpuUploadState::PendingUpload:
					++stats.PendingTextures;
					stats.PendingUploadBytes += record.TexelBytes;
					break;
				case GpuUploadState::Recorded:
					++stats.RecordedTextures;
					stats.PendingUploadBytes += record.TexelBytes;
					break;
				case GpuUploadState::Uploading:
					++stats.UploadingTextures;
					break;
				case GpuUploadState::Resident:
					++stats.ResidentTextures;
					stats.ResidentBytes += record.TexelBytes;
					break;
				default:
					break;
				}

			});
		stats.RetiringTextures = textures.GetStats().Retiring;
		return stats;
	}

} // namespace Swim::Render
