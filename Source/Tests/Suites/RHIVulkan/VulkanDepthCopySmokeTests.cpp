#include "Engine/Platform/PlatformSystem.h"
#include "Engine/Systems/Renderer/RHI/RhiFrameLifetime.h"
#include "Tests/Fixtures/SmokeBackend.h"
#include "Tests/Framework/Test.h"

#include <array>
#include <cstdlib>
#include <cstring>
#include <string_view>

namespace
{

	// A depth tile copied between two depth textures (as a shadow atlas copies its static
	// tiles): only the region changes, the rest keeps the destination's depth.
	void RunDepthRegionCopySmoke(const Swim::Rhi::GraphicsSystemDesc& graphicsDesc)
	{
		using namespace Swim;
		using S = Rhi::ResourceState;
		Platform::PlatformSystem platform;
		SWIM_REQUIRE_MESSAGE(platform.Initialize(), "RHI smoke requires a working SDL desktop video driver");
		auto graphics = Swim::Testing::CreateSmokeGraphicsSystem(graphicsDesc);
		auto device = graphics->GetAdapter(0).CreateDevice();
		SWIM_REQUIRE(device);

		constexpr std::uint32_t size = 64;
		Rhi::TextureDesc desc{};
		desc.Extent = { size, size, 1 };
		desc.PixelFormat = Rhi::Format::D32Float;
		desc.Usage = Rhi::TextureUsage::DepthStencilAttachment | Rhi::TextureUsage::Sampled | Rhi::TextureUsage::TransferSource |
			Rhi::TextureUsage::TransferDestination;
		desc.DebugName = "Depth copy source";
		auto source = device->CreateTexture(desc);
		desc.DebugName = "Depth copy destination";
		auto destination = device->CreateTexture(desc);
		SWIM_REQUIRE(source && destination);
		auto sourceView = device->CreateTextureView(*source, {});
		auto destinationView = device->CreateTextureView(*destination, {});
		SWIM_REQUIRE(sourceView && destinationView);
		constexpr std::size_t bytes = std::size_t(size) * size * 4;
		auto readback =
			device->CreateBuffer({ bytes, Rhi::BufferUsage::TransferDestination, Rhi::MemoryPreference::GpuToCpu, "Depth copy readback" });
		SWIM_REQUIRE(readback);
		auto frames = Rhi::FrameContextRing::Create(*device, { Rhi::QueueType::Graphics, 2 });
		SWIM_REQUIRE(frames);

		const auto clear = [&](Rhi::CommandList& commands, Rhi::Texture& texture, Rhi::TextureView& view, float depth)
		{
			commands.Transition(texture, S::Undefined, S::DepthStencilWrite);
			Rhi::DepthStencilAttachmentDesc attachment{};
			attachment.View = &view;
			attachment.DepthLoad = Rhi::LoadOp::Clear;
			attachment.ClearDepth = depth;
			commands.BeginRendering({ {}, &attachment, { size, size } });
			commands.EndRendering();
		};

		frames->BeginFrame();
		auto& commands = frames->CreateCommandList();
		commands.Begin();
		clear(commands, *source, *sourceView, 0.25f);
		clear(commands, *destination, *destinationView, 0.75f);
		commands.Transition(*source, S::DepthStencilWrite, S::CopySource);
		commands.Transition(*destination, S::DepthStencilWrite, S::CopyDestination);
		Rhi::TextureCopyRegion region{};
		region.SourceOffset = { 8, 16, 0 };
		region.DestinationOffset = { 24, 8, 0 };
		region.Extent = { 32, 16, 1 };
		commands.CopyTexture(*source, *destination, region);
		commands.Transition(*destination, S::CopyDestination, S::CopySource);
		commands.Transition(*readback, S::Undefined, S::CopyDestination);
		Rhi::BufferTextureCopyRegion copy{};
		copy.Extent = desc.Extent;
		commands.CopyTextureToBuffer(*destination, *readback, copy);
		commands.Transition(*readback, S::CopyDestination, S::HostRead);
		commands.End();
		frames->SubmitCurrent();
		frames->Drain();

		std::array<float, size * size> depths{};
		readback->Read(0, std::as_writable_bytes(std::span(depths)));
		std::uint32_t wrong = 0;

		for (std::uint32_t y = 0; y < size; ++y)
		{
			for (std::uint32_t x = 0; x < size; ++x)
			{
				const bool inside = x >= 24 && x < 56 && y >= 8 && y < 24;
				wrong += depths[y * size + x] != (inside ? 0.25f : 0.75f) ? 1u : 0u;
			}
		}

		SWIM_CHECK_EQUAL(wrong, 0u);
	}

	[[maybe_unused]] const bool registered = []
	{
		const char* enabled = std::getenv("SWIM_RUN_RHI_SMOKE");

		if (enabled != nullptr && std::string_view(enabled) == "1")
		{
			Swim::Testing::AddSmoke<&RunDepthRegionCopySmoke>("DepthRegionCopyKeepsTheRest", SWIM_TEST_LOCATION);
		}

		return true;
	}();

} // namespace
